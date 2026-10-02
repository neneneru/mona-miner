"""CPU-only integrity tests for the public object helper."""
import tempfile
import unittest
from pathlib import Path
import embed_images as embed


class ImageObjectTests(unittest.TestCase):
    def test_qualified_payload_and_object(self):
        root = Path(__file__).resolve().parent.parent
        embed.check_contract(root / "source/include/image_contract.hpp")
        blobs = embed.read_images(root / "images")
        payload = embed.make_payload(blobs)
        obj = embed.make_object(payload)
        embed.verify_object(obj, payload)
        for offset, blob in zip(embed.OFFSETS, blobs, strict=True):
            self.assertEqual(payload[offset:offset + len(blob)], blob)
        for position in (0, 20, 60, 60 + embed.OFFSETS[1], len(obj) - 1):
            corrupted = bytearray(obj)
            corrupted[position] ^= 1
            with self.assertRaises(ValueError):
                embed.verify_object(bytes(corrupted), payload)

    def test_bad_contract_and_image_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            header = directory / "image_contract.hpp"
            header.write_text("payload_bytes=1; offsets[3]={0,1,2};", encoding="utf-8")
            with self.assertRaises(ValueError):
                embed.check_contract(header)
            (directory / embed.IMAGES[0][0]).write_bytes(b"invalid image")
            with self.assertRaises(ValueError):
                embed.read_images(directory)
        with self.assertRaises(ValueError):
            embed.make_payload([b"short"] * 3)


if __name__ == "__main__":
    unittest.main()
