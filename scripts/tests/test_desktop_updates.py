import base64
import importlib.util
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.exceptions import InvalidSignature

spec = importlib.util.spec_from_file_location("updates", Path(__file__).resolve().parents[1] / "prepare-desktop-updates.py")
updates = importlib.util.module_from_spec(spec)
spec.loader.exec_module(updates)


class UpdateFeedTests(unittest.TestCase):
    def test_signed_manifest_and_native_archive_signatures(self):
        key = Ed25519PrivateKey.generate()
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            for platform, suffix in [("macos-arm64", ".zip"), ("windows-x64", ".exe"), ("linux-x86_64", ".AppImage")]:
                archive = root / ("DuneCity-1.0.731" + suffix)
                archive.write_bytes(b"fixture update archive")
                updates.prepare("1.0.731", platform, archive, "https://example.com/v1.0.731", key, root)
                text = (root / f"updates-{platform}.txt").read_bytes()
                payload, signature = text.rsplit(b"\n", 2)[:2]
                key.public_key().verify(base64.b64decode(signature), payload + b"\n")
                with self.assertRaises(InvalidSignature):
                    key.public_key().verify(base64.b64decode(signature), payload.replace(b"1.0.731", b"1.0.732") + b"\n")
                if not platform.startswith("linux"):
                    enclosure = ET.parse(root / f"appcast-{platform}.xml").find("channel/item/enclosure")
                    key.public_key().verify(base64.b64decode(enclosure.attrib[f"{{{updates.NS}}}edSignature"]), archive.read_bytes())

    def test_rejects_wrong_format_insecure_url_and_invalid_version(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); archive = root / "DuneCity-1.0.731.zip"; archive.write_bytes(b"fixture")
            key = Ed25519PrivateKey.generate()
            for version, platform, url in [("1.0.731", "windows-x64", "https://example.com"),
                                           ("1.0.731", "macos-arm64", "http://example.com"),
                                           ("1.0.731", "macos-arm64", "https://user:password@example.com"),
                                           ("01.0.731", "macos-arm64", "https://example.com")]:
                with self.assertRaises(ValueError): updates.prepare(version, platform, archive, url, key, root)


if __name__ == "__main__": unittest.main()
