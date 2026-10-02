#!/usr/bin/env python3
"""Embed qualified cubins in an AMD64 COFF object; no CUDA compiler required."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

IMAGES = (
    ("N02_EXP01_PRODUCER.cubin", "9463bb3d1242371f7ebaef19aa528b63debeb6fdc2fc900f8f1a0d8e7d85ec19"),
    ("R02_ACCEPTED.cubin", "c9a9a6872df92f529b88cf6a80eabb925b515c76a1c4a3791788a52afb7e8a84"),
    ("R02_CUBE2_TAIL_FUSION.cubin", "84f3b1b105a7cb4df7dec50968e3ea81e2c4a0c97f83afe4872e413376f5e2bd"),
)
OFFSETS = (0, 8935424, 12931328)
PAYLOAD_BYTES = 17758728


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_images(directory):
    blobs = []
    for name, expected in IMAGES:
        data = (directory / name).read_bytes()
        if digest(data) != expected:
            raise ValueError(f"qualified image SHA256 mismatch: {name}")
        blobs.append(data)
    return blobs


def check_contract(header):
    text = header.read_text(encoding="utf-8")
    size = re.search(r"payload_bytes\s*=\s*(\d+)\s*;", text)
    offsets = re.search(r"offsets\[3\]\s*=\s*\{([^}]+)\}\s*;", text)
    if not size or not offsets:
        raise ValueError("unrecognized image contract")
    actual = tuple(int(n.strip()) for n in offsets.group(1).split(","))
    if int(size.group(1)) != PAYLOAD_BYTES or actual != OFFSETS:
        raise ValueError("frozen payload/offset contract mismatch")


def make_payload(blobs):
    payload = bytearray()
    for offset, blob in zip(OFFSETS, blobs, strict=True):
        aligned = (len(payload) + 255) & ~255
        if aligned != offset:
            raise ValueError("image lengths violate 256-byte offset contract")
        payload.extend(bytes(offset - len(payload)))
        payload.extend(blob)
    if len(payload) != PAYLOAD_BYTES:
        raise ValueError("payload length mismatch")
    return bytes(payload)


def make_object(payload):
    # One read-only, 256-byte-aligned section, one external symbol, no relocations.
    raw_offset = 20 + 40
    symbol_offset = raw_offset + len(payload)
    header = struct.pack("<HHIIIHH", 0x8664, 1, 0, symbol_offset, 1, 0, 0)
    section = struct.pack("<8sIIIIIIHHI", b".rdata\0\0", 0, 0, len(payload),
                          raw_offset, 0, 0, 0, 0, 0x40900040)
    symbol = struct.pack("<IIIhHBB", 0, 4, 0, 1, 0, 2, 0)
    name = b"mona2_images\0"
    strings = struct.pack("<I", 4 + len(name)) + name
    return header + section + payload + symbol + strings


def verify_object(data, payload):
    # Require the entire deterministic object, not only a matching subrange.
    if data != make_object(payload):
        raise ValueError("COFF object structure or embedded payload mismatch")


def verify_executable(data, payload):
    """Verify the linked payload in a read-only AMD64 PE section, without running it."""
    if data[:2] != b"MZ":
        raise ValueError("not a PE executable")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("invalid PE signature")
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    if machine != 0x8664:
        raise ValueError("executable is not AMD64")
    matches = []
    for i in range(count):
        section = pe + 24 + optional + 40 * i
        virtual_size, rva, size, offset = struct.unpack_from("<IIII", data, section + 8)
        flags = struct.unpack_from("<I", data, section + 36)[0]
        raw = data[offset:offset + size]
        position = raw.find(payload)
        if position >= 0:
            if raw.find(payload, position + 1) >= 0:
                raise ValueError("duplicate payload")
            if not flags & 0x40000000 or flags & 0x80000000:
                raise ValueError("payload section is not read-only")
            if (rva + position) % 256 or position + len(payload) > virtual_size:
                raise ValueError("linked payload alignment/range mismatch")
            matches.append(rva + position)
    if len(matches) != 1:
        raise ValueError("qualified payload not uniquely embedded in executable")
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--executable", type=Path, help="also verify linked PE payload")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    check_contract(root / "source/include/image_contract.hpp")
    blobs = read_images(root / "images")
    payload = make_payload(blobs)
    if not args.verify_only:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(make_object(payload))
    data = args.output.read_bytes()
    verify_object(data, payload)
    # Recheck source files and contract after generation/verification.
    if read_images(root / "images") != blobs:
        raise ValueError("image changed during generation")
    check_contract(root / "source/include/image_contract.hpp")
    cursor = 0
    for offset, blob in zip(OFFSETS, blobs, strict=True):
        if any(payload[cursor:offset]) or payload[offset:offset + len(blob)] != blob:
            raise ValueError("payload slice or zero padding mismatch")
        cursor = offset + len(blob)
    executable_rva = verify_executable(args.executable.read_bytes(), payload) if args.executable else None
    print(json.dumps({"status": "PASS", "machine": "AMD64", "symbol": "mona2_images",
                      "executable_payload_rva": executable_rva,
                      "payload_bytes": len(payload), "offsets": OFFSETS,
                      "payload_sha256": digest(payload), "object_sha256": digest(data),
                      "images": [{"name": name, "bytes": len(blob), "sha256": sha}
                                 for (name, sha), blob in zip(IMAGES, blobs, strict=True)]}, indent=2))


if __name__ == "__main__":
    main()
