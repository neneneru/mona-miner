#!/usr/bin/env python3
"""Embed the original producer and the measured SM120 preview kernel."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from embed_images import digest, make_object, read_images, verify_executable, verify_object


CANDIDATE_SHA256 = "ad2bd0a1adf7f1921ec157caeae10f93dbdfee2d57971093f64118e0cf605c8e"


def contract(offset):
    return f"""// Generated experimental image ABI.
#pragma once
#include <cstddef>
namespace preview_image {{
inline constexpr std::size_t middle_offset={offset};
inline constexpr const char* symbols[]={{
    "n02_blake_job_columns_b64", "r02_cube1_l2_skein_cube2_b64",
    "mona_cube2_bmw", "mona_cube2_bmw_debug"
}};
inline constexpr unsigned blocks[]={{64,32,256,256}};
inline constexpr int registers[]={{72,167,64,64}};
}}
""".encode()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sm120 = Path(__file__).resolve().parents[2]
    p.add_argument("--candidate", type=Path, default=sm120 / "images" / "V210_LYRA_REGISTER.cubin")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--verify-only", action="store_true")
    p.add_argument("--executable", type=Path)
    args = p.parse_args()

    producer = read_images(sm120 / "images")[0]
    candidate = args.candidate.read_bytes()
    if digest(candidate) != CANDIDATE_SHA256:
        raise ValueError("Preview candidate SHA256 mismatch")
    offset = (len(producer) + 255) & ~255
    payload = producer + bytes(offset - len(producer)) + candidate
    object_path = args.out / "mona2_preview.obj"
    header_path = args.out / "preview_image.hpp"
    if not args.verify_only:
        args.out.mkdir(parents=True, exist_ok=True)
        object_path.write_bytes(make_object(payload))
        header_path.write_bytes(contract(offset))
    verify_object(object_path.read_bytes(), payload)
    if header_path.read_bytes() != contract(offset):
        raise ValueError("Preview image header mismatch")
    metadata = {"producer_sha256": digest(producer), "candidate_sha256": digest(candidate),
                "candidate_offset": offset, "payload_bytes": len(payload)}
    if args.executable:
        exe = args.executable.read_bytes()
        metadata["payload_rva"] = verify_executable(exe, payload)
        metadata["executable_sha256"] = digest(exe)
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
