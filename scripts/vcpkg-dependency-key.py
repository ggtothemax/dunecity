#!/usr/bin/env python3
"""Print a short, stable hash of the vcpkg dependency set.

The CI dependency cache has to survive a release version bump. `vcpkg.json`
also carries the application version (`scripts/bump-version.sh` keeps it in
sync with CMakeLists.txt and include/config.h), so a cache key built from
`hashFiles('vcpkg.json')` changed on every release even when nothing about the dependencies moved.
Restore-key fallback could recover packages, but the primary key still churned.

Only the fields that change what vcpkg actually builds are hashed here:
the dependency list, the registry baseline, version overrides and feature
selection.
"""

import hashlib
import json
import sys
from pathlib import Path

DEPENDENCY_KEYS = (
    "builtin-baseline",
    "default-features",
    "dependencies",
    "features",
    "overrides",
)


def dependency_key(manifest_path: Path) -> str:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    relevant = {key: manifest[key] for key in DEPENDENCY_KEYS if key in manifest}
    payload = json.dumps(relevant, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()[:16]


def main(argv: list[str]) -> int:
    if len(argv) > 2:
        print(f"usage: {argv[0]} [vcpkg.json]", file=sys.stderr)
        return 2
    manifest_path = (
        Path(argv[1])
        if len(argv) == 2
        else Path(__file__).resolve().parent.parent / "vcpkg.json"
    )
    if not manifest_path.is_file():
        print(f"manifest not found: {manifest_path}", file=sys.stderr)
        return 1
    print(dependency_key(manifest_path))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
