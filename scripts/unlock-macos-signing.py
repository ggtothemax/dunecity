#!/usr/bin/env python3
"""Unlock the locally provisioned signing keychain without logging its password."""
from pathlib import Path
import subprocess
import sys

root = Path.home() / "Library/Application Support/DuneCity Signing/34X7AYJZ93"
password_file = root / "signing-keychain-password"
if not password_file.is_file():
    sys.exit("Mac signing has not been provisioned for this runner account.")
password = password_file.read_text().strip()
result = subprocess.run(["security", "unlock-keychain", "-p", password,
                         str(Path.home() / "Library/Keychains/DuneCity-Signing.keychain-db")],
                        capture_output=True)
if result.returncode:
    sys.exit("The local signing keychain could not be unlocked.")
