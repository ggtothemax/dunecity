#!/usr/bin/env python3
"""Prepare signed release assets locally; never publishes or uploads a key.

Requires cryptography. One Ed25519 key signs both the strict update manifests
and Sparkle/WinSparkle archives. Only the public key is checked into the game.
"""
import argparse
import base64
import hashlib
from pathlib import Path
import re
import xml.etree.ElementTree as ET
from urllib.parse import urlsplit

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"
ET.register_namespace("sparkle", NS)
ROOT = Path(__file__).resolve().parents[1]


def prepare(version, platform, archive, base_url, key, output):
    if not re.fullmatch(r"(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})", version):
        raise ValueError("Version must be X.Y.Z, without leading zeros")
    extensions = {"macos-arm64": ".zip", "macos-x86_64": ".zip", "windows-x64": ".exe", "linux-x86_64": ".AppImage", "linux-arm64": ".AppImage"}
    if platform not in extensions or archive.suffix != extensions[platform]:
        raise ValueError("Wrong archive format for this platform")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", archive.name) or version not in archive.name:
        raise ValueError("Archive name must contain its version and use plain filename characters")
    url = base_url.rstrip("/") + "/" + archive.name
    parsed = urlsplit(url)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password or parsed.fragment or any(c.isspace() for c in url):
        raise ValueError("An HTTPS asset URL is required")
    if not 0 < archive.stat().st_size <= 536870912:
        raise ValueError("Archive exceeds the updater size limit")
    data = archive.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    payload = f"DuneCityUpdate1\n{version}\n{platform}\n{url}\n{len(data)}\n{digest}\n".encode()
    signature = base64.b64encode(key.sign(payload)).decode()
    output.mkdir(parents=True, exist_ok=True)
    (output / f"updates-{platform}.txt").write_bytes(payload + signature.encode() + b"\n")
    if platform.startswith(("macos", "windows")):
        rss = ET.Element("rss", version="2.0")
        channel = ET.SubElement(rss, "channel")
        ET.SubElement(channel, "title").text = "Dune City updates"
        item = ET.SubElement(channel, "item")
        ET.SubElement(item, "title").text = f"Dune City {version}"
        ET.SubElement(item, f"{{{NS}}}version").text = version
        ET.SubElement(item, f"{{{NS}}}shortVersionString").text = version
        attrs = {"url": url, "length": str(len(data)), "type": "application/octet-stream",
                 f"{{{NS}}}version": version,
                 f"{{{NS}}}edSignature": base64.b64encode(key.sign(data)).decode()}
        if platform.startswith("macos"):
            ET.SubElement(item, f"{{{NS}}}minimumSystemVersion").text = "14.0"
        ET.SubElement(item, "enclosure", attrs)
        ET.ElementTree(rss).write(output / f"appcast-{platform}.xml", encoding="utf-8", xml_declaration=True)
    return digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", required=True, choices=["macos-arm64", "macos-x86_64", "windows-x64", "linux-x86_64", "linux-arm64"])
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--base-url", required=True, help="Versioned HTTPS directory containing the archive")
    parser.add_argument("--key-file", type=Path, required=True)
    parser.add_argument("--password-file", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    key = serialization.load_pem_private_key(args.key_file.read_bytes(), args.password_file.read_bytes().rstrip(b"\r\n"))
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError("An Ed25519 update key is required")
    public = base64.b64encode(key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)).decode()
    if public != (ROOT / "cmake/update-public-key.txt").read_text().strip():
        raise ValueError("Signing key does not match the public key embedded in the app")
    digest = prepare(args.version, args.platform, args.archive, args.base_url, key, args.output)
    print(f"Prepared signed {args.platform} update {args.version}; archive SHA256 {digest}")


if __name__ == "__main__":
    main()
