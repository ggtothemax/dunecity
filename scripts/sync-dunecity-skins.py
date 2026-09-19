#!/usr/bin/env python3
"""Synchronize accepted Oathkeeper DuneCity Compacts into the playable mod.

The scanner derives package destinations from each unit manifest rather than
from a hand-maintained file list. Packages are prepared in a staging directory
and replace live mod directories only after every selected package succeeds.
"""

from __future__ import annotations

import argparse
import configparser
import importlib.util
import json
import os
import re
import shutil
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
PACKAGER_PATH = SCRIPT_DIR / "package-dunecity-skin.py"
PACKAGER_SPEC = importlib.util.spec_from_file_location("package_dunecity_skin", PACKAGER_PATH)
PACKAGER = importlib.util.module_from_spec(PACKAGER_SPEC)
assert PACKAGER_SPEC.loader is not None
PACKAGER_SPEC.loader.exec_module(PACKAGER)

HOUSE_IDS = {
    "harkonnen": 0,
    "atreides": 1,
    "ordos": 2,
    "fremen": 3,
    "sardaukar": 4,
    "mercenary": 5,
    "neutral": 6,
    "rebels": 7,
    "custom": 8,
}
ZONE_ITEM_IDS = {
    "residential": 20,
    "commercial": 21,
    "industrial": 22,
}
SPECIAL_OBJ_PICS = {
    "nuclear power plant": "NuclearPlant",
    "nuclear plant": "NuclearPlant",
    "police station": "PoliceStation",
    "stadium": "Stadium",
    "airport": "Airport",
    "hospital": "Hospital",
    "church": "Church",
}
SAFE_SLUG = re.compile(r"^[a-z0-9][a-z0-9_]{0,127}$")


@dataclass(frozen=True)
class PackagePlan:
    unit_dir: Path
    slug: str
    kind: str
    destination: Path
    house_id: int
    item_id: int | None = None
    obj_pic: str | None = None


def _read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"Manifest root must be an object: {path}")
    return value


def _read_existing_package(path: Path, section: str) -> configparser.SectionProxy | None:
    if not path.is_file():
        return None
    parser = configparser.ConfigParser()
    parser.optionxform = str
    parser.read(path, encoding="ascii")
    return parser[section] if parser.has_section(section) else None


def _processed_path(state: Any, asset_root: Path) -> Path | None:
    if not isinstance(state, dict):
        return None
    assets = state.get("assets")
    processed = assets.get("processed") if isinstance(assets, dict) else None
    relative = processed.get("file") if isinstance(processed, dict) else None
    if not isinstance(relative, str) or not relative.strip():
        return None
    candidate = (asset_root / relative).resolve()
    try:
        candidate.relative_to(asset_root)
    except ValueError:
        raise ValueError(f"Compact path escapes the Oathkeeper asset root: {relative}")
    return candidate if candidate.is_file() else None


def _accepted_compacts(manifest: dict[str, Any], asset_root: Path) -> list[Path]:
    """Return every accepted Compact, including slots unsupported by a packager."""

    accepted: list[Path] = []
    categories = manifest.get("categories")
    if not isinstance(categories, dict):
        return accepted
    for category in categories.values():
        if not isinstance(category, dict):
            continue
        states = category.get("states")
        if not isinstance(states, dict):
            continue
        for state in states.values():
            candidate = _processed_path(state, asset_root)
            if candidate is not None:
                accepted.append(candidate)
    return accepted


def _packageable_compacts(
    manifest: dict[str, Any], asset_root: Path, kind: str
) -> list[Path]:
    """Mirror the exact slot selection performed by the zone/building packagers."""

    categories = manifest.get("categories")
    idle = categories.get("building_idle") if isinstance(categories, dict) else None
    states = idle.get("states") if isinstance(idle, dict) else None
    if not isinstance(states, dict):
        return []

    selected: list[Any] = []
    if kind == "zone":
        city = manifest.get("dunecity") if isinstance(manifest.get("dunecity"), dict) else {}
        atlas = city.get("zone_atlas") if isinstance(city.get("zone_atlas"), dict) else {}
        density_columns = max(1, min(4, int(atlas.get("density_columns", 1))))
        value_rows = max(1, min(4, int(atlas.get("value_tier_rows", 1))))
        selected = [
            states.get(f"d{density}_v{value}")
            for value in range(value_rows)
            for density in range(density_columns)
        ]
    else:
        numbered = sorted(
            (
                (int(key.removeprefix("frame_")), value)
                for key, value in states.items()
                if key.startswith("frame_") and key.removeprefix("frame_").isdigit()
            ),
            key=lambda item: item[0],
        )
        selected = [value for _number, value in numbered]
        if not selected and "default" in states:
            selected = [states["default"]]

    return [
        candidate
        for state in selected
        if (candidate := _processed_path(state, asset_root)) is not None
    ]


def _unit_matches(manifest: dict[str, Any], unit_dir: Path, requested: str | None) -> bool:
    if not requested:
        return True
    needle = requested.strip().casefold()
    values = {
        unit_dir.name.casefold(),
        str(manifest.get("slug") or "").strip().casefold(),
        str(manifest.get("name") or "").strip().casefold(),
    }
    return needle in values


def _plan_unit(unit_dir: Path, manifest: dict[str, Any], skin_root: Path) -> PackagePlan | None:
    slug = str(manifest.get("slug") or unit_dir.name).strip().lower()
    if not SAFE_SLUG.fullmatch(slug):
        raise ValueError(f"Unsafe DuneCity unit slug: {slug!r}")
    city = manifest.get("dunecity") if isinstance(manifest.get("dunecity"), dict) else {}
    asset_class = str(city.get("asset_class") or manifest.get("asset_class") or "").strip().lower()
    faction = str(city.get("faction") or "").strip().lower()
    source_asset = str(city.get("source_asset") or "").strip()

    if asset_class in ZONE_ITEM_IDS:
        destination = skin_root / "zones" / slug
        existing = _read_existing_package(destination / "zone.ini", "Zone")
        house_id = int(existing.get("HouseID")) if existing is not None else HOUSE_IDS.get(faction, -1)
        item_id = int(existing.get("ItemID")) if existing is not None else ZONE_ITEM_IDS[asset_class]
        if house_id < 0:
            return None
        return PackagePlan(unit_dir, slug, "zone", destination, house_id, item_id=item_id)

    destination = skin_root / "buildings" / slug
    existing = _read_existing_package(destination / "building.ini", "Building")
    house_id = int(existing.get("HouseID")) if existing is not None else HOUSE_IDS.get(faction, -1)
    obj_pic = existing.get("ObjPic") if existing is not None else SPECIAL_OBJ_PICS.get(source_asset.casefold())
    if house_id < 0 or not obj_pic:
        return None
    return PackagePlan(unit_dir, slug, "building", destination, house_id, obj_pic=obj_pic)


def _assert_under(path: Path, root: Path) -> None:
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError as exc:
        raise ValueError(f"Refusing to modify a path outside {root}: {path}") from exc


def _preserve_authored_icon(destination: Path, staged: Path) -> None:
    old_icon = destination / "icon.png"
    new_icon = staged / "icon.png"
    if old_icon.is_file() and not new_icon.exists():
        shutil.copy2(old_icon, new_icon)


def _authored_icon_path(manifest: dict[str, Any], asset_root: Path) -> Path | None:
    """Resolve Oathkeeper's unit-level Icon Sprite Compact, when authored."""

    categories = manifest.get("categories")
    icon_category = categories.get("icon_sprite") if isinstance(categories, dict) else None
    states = icon_category.get("states") if isinstance(icon_category, dict) else None
    default_state = states.get("default") if isinstance(states, dict) else None
    return _processed_path(default_state, asset_root)


def _install_authored_icon(manifest: dict[str, Any], asset_root: Path, staged: Path) -> bool:
    icon = _authored_icon_path(manifest, asset_root)
    if icon is None:
        return False
    staged.mkdir(parents=True, exist_ok=True)
    shutil.copy2(icon, staged / "icon.png")
    return True


def synchronize(
    source_root: Path,
    repo_root: Path,
    requested_unit: str | None = None,
    *,
    plan_only: bool = False,
) -> dict[str, Any]:
    source_root = source_root.resolve()
    repo_root = repo_root.resolve()
    units_root = source_root / "units" if (source_root / "units").is_dir() else source_root
    asset_root = units_root.parent
    skin_root = repo_root / "mods" / "dunecity" / "graphics_skins" / "Dune2"
    if not units_root.is_dir():
        raise FileNotFoundError(f"Oathkeeper Dune2 unit directory not found: {units_root}")
    if not (repo_root / "CMakeLists.txt").is_file() or not skin_root.is_dir():
        raise FileNotFoundError(f"Canonical DuneCity repository or skin root not found: {repo_root}")

    candidates: list[tuple[PackagePlan, dict[str, Any], int]] = []
    unsupported: list[str] = []
    matched = 0
    for unit_dir in sorted((path for path in units_root.iterdir() if path.is_dir()), key=lambda path: path.name):
        manifest_path = unit_dir / "unit.json"
        if not manifest_path.is_file():
            continue
        manifest = _read_json(manifest_path)
        if str(manifest.get("target_game") or "").strip().lower() != "dunecity":
            continue
        if not _unit_matches(manifest, unit_dir, requested_unit):
            continue
        matched += 1
        accepted = _accepted_compacts(manifest, asset_root)
        if not accepted:
            print(f"[SYNC SKIP] {unit_dir.name}: no accepted Compact files", flush=True)
            continue
        plan = _plan_unit(unit_dir, manifest, skin_root)
        if plan is None:
            unsupported.append(unit_dir.name)
            print(f"[SYNC SKIP] {unit_dir.name}: no engine package mapping", flush=True)
            continue
        packageable = _packageable_compacts(manifest, asset_root, plan.kind)
        if not packageable:
            label = "density/value cells" if plan.kind == "zone" else "building frames"
            print(f"[SYNC SKIP] {unit_dir.name}: no package-eligible {label}", flush=True)
            continue
        _assert_under(plan.destination, skin_root)
        candidates.append((plan, manifest, len(packageable)))

    if requested_unit and matched == 0:
        raise ValueError(f"No DuneCity unit matched {requested_unit!r}")
    if unsupported:
        raise ValueError(
            "Accepted DuneCity Compacts have no engine package mapping: "
            + ", ".join(sorted(unsupported))
        )
    if not candidates:
        raise ValueError("No supported DuneCity units with accepted Compacts were found")

    if plan_only:
        for plan, _manifest, accepted_count in candidates:
            print(
                f"[SYNC PLAN] {plan.slug}: {plan.kind}, house={plan.house_id}, accepted={accepted_count}, "
                f"destination={plan.destination}",
                flush=True,
            )
        summary = {
            "packages": len(candidates),
            "zones": sum(plan.kind == "zone" for plan, _manifest, _count in candidates),
            "buildings": sum(plan.kind == "building" for plan, _manifest, _count in candidates),
            "unsupported": unsupported,
            "unit": requested_unit,
            "plan_only": True,
        }
        print("[SYNC PLAN COMPLETE] " + json.dumps(summary, sort_keys=True), flush=True)
        return summary

    skin_root.mkdir(parents=True, exist_ok=True)
    stage_root = Path(tempfile.mkdtemp(prefix=".skin-sync-", dir=skin_root))
    staged: list[tuple[PackagePlan, Path]] = []
    try:
        for index, (plan, manifest, accepted_count) in enumerate(candidates, start=1):
            output = stage_root / "packages" / plan.kind / plan.slug
            print(
                f"[SYNC {index}/{len(candidates)}] {plan.slug}: packaging {accepted_count} accepted Compact(s)",
                flush=True,
            )
            if plan.kind == "zone":
                PACKAGER.package(plan.unit_dir, output, int(plan.item_id), plan.house_id)
            else:
                PACKAGER.package_building(plan.unit_dir, output, str(plan.obj_pic), plan.house_id)
            if not _install_authored_icon(manifest, asset_root, output):
                _preserve_authored_icon(plan.destination, output)
            staged.append((plan, output))

        backups = stage_root / "backups"
        backups.mkdir(parents=True, exist_ok=True)
        applied: list[tuple[PackagePlan, Path | None]] = []
        try:
            for plan, output in staged:
                backup = backups / plan.kind / plan.slug
                backup.parent.mkdir(parents=True, exist_ok=True)
                if plan.destination.exists():
                    os.replace(plan.destination, backup)
                    saved: Path | None = backup
                else:
                    plan.destination.parent.mkdir(parents=True, exist_ok=True)
                    saved = None
                applied.append((plan, saved))
                os.replace(output, plan.destination)
        except Exception:
            for plan, backup in reversed(applied):
                if plan.destination.exists():
                    shutil.rmtree(plan.destination)
                if backup is not None and backup.exists():
                    os.replace(backup, plan.destination)
            raise

        summary = {
            "packages": len(staged),
            "zones": sum(plan.kind == "zone" for plan, _output in staged),
            "buildings": sum(plan.kind == "building" for plan, _output in staged),
            "unsupported": unsupported,
            "unit": requested_unit,
        }
        print("[SYNC COMPLETE] " + json.dumps(summary, sort_keys=True), flush=True)
        return summary
    finally:
        shutil.rmtree(stage_root, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(r"D:\BotServer\GitHub\Discord-AI-Bot\dune2"),
        help="Oathkeeper dune2 root or units directory",
    )
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--unit", help="Limit synchronization to one manifest name or slug")
    parser.add_argument("--plan-only", action="store_true", help="Validate mappings without changing packages")
    args = parser.parse_args()
    synchronize(args.source_root, args.repo_root, args.unit, plan_only=args.plan_only)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
