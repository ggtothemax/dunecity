#!/usr/bin/env python3
"""Package accepted Oathkeeper Compact cells for DuneCity's Dune2 skin.

This intentionally refuses the large source sprite. DuneCity consumes the
already-approved Compact PNG verbatim, or fits it to the explicitly selected
Compact resolution, preserving the QA result seen in Discord while keeping the
logical building footprint separate.
"""

from __future__ import annotations

import argparse
import configparser
import io
import json
from pathlib import Path

from PIL import Image


def package(source_unit: Path, output: Path, item_id: int, house_id: int) -> int:
    source_unit = source_unit.resolve()
    metadata = json.loads((source_unit / "unit.json").read_text(encoding="utf-8"))
    if str(metadata.get("target_game", "")).lower() != "dunecity":
        raise SystemExit("The source asset is not namespaced for DuneCity")

    city = metadata.get("dunecity", {})
    atlas = city.get("zone_atlas", {})
    density_columns = max(1, min(4, int(atlas.get("density_columns", 1))))
    value_rows = max(1, min(4, int(atlas.get("value_tier_rows", 1))))
    footprint = metadata.get("render_profile", {}).get("logical_footprint_tiles", [2, 2])
    footprint_width = max(1, int(footprint[0]))
    footprint_height = max(1, int(footprint[1]))
    render_profile = metadata.get("render_profile", {})
    declared_size = render_profile.get("compact_frame_pixels", [])
    if (
        isinstance(declared_size, list)
        and len(declared_size) == 2
        and all(isinstance(value, int) and 0 < value <= 2048 for value in declared_size)
    ):
        target_size = (declared_size[0], declared_size[1])
    else:
        target_size = (footprint_width * 16, footprint_height * 16)
    pixels_per_tile = max(16, min(64, int(
        city.get("compact_pixels_per_tile")
        or render_profile.get("compact_pixels_per_tile")
        or 16
    )))
    asset_root = source_unit.parent.parent

    manifest = configparser.ConfigParser()
    manifest.optionxform = str
    manifest["Zone"] = {
        "ItemID": str(item_id),
        "HouseID": str(house_id),
        "SourceUnit": str(metadata.get("slug", source_unit.name)),
        "DensityColumns": str(density_columns),
        "ValueTierRows": str(value_rows),
        "FootprintWidth": str(footprint_width),
        "FootprintHeight": str(footprint_height),
    }
    manifest["Render"] = {
        "PixelsPerTile": str(pixels_per_tile),
        "LogicalPixelsPerTile": "16",
        "Source": "accepted-compact",
        "Sizing": "high-detail-source-fixed-engine-footprint",
    }

    states = metadata.get("categories", {}).get("building_idle", {}).get("states", {})
    packaged = 0
    output.mkdir(parents=True, exist_ok=True)
    for value in range(value_rows):
        for density in range(density_columns):
            slot = f"d{density}_v{value}"
            assets = states.get(slot, {}).get("assets", {})
            compact_value = assets.get("processed", {}).get("file", "")
            compact_path = asset_root / compact_value
            if not compact_path.is_file():
                continue

            destination = Path("atlases") / "idle" / slot / "00.png"
            (output / destination).parent.mkdir(parents=True, exist_ok=True)
            with Image.open(compact_path) as source:
                compact = source.convert("RGBA")
                if compact.size != target_size:
                    compact = compact.resize(target_size, Image.Resampling.LANCZOS)
                compact.save(output / destination, optimize=True)

            section = f"Cell.{density}.{value}.Idle"
            manifest[section] = {
                "Frames": "1",
                "FrameMs": "1",
                "FrameWidth": str(target_size[0]),
                "FrameHeight": str(target_size[1]),
                "AnchorX": str(target_size[0] // 2),
                "AnchorY": str(target_size[1]),
                "Loop": "false",
                "AtlasCount": "1",
                "Atlas.0": destination.as_posix(),
                "FirstFrame.0": "0",
                "ChunkFrames.0": "1",
                "Columns.0": "1",
                "Rows.0": "1",
                "Fallback": "exact-simcity-cell",
            }
            packaged += 1

            # Industrial's native renderer advances eight powered smoke rows
            # for every developed density/value cell.  Keep those authored
            # frames separate from the static growth sprite so the engine can
            # fall back to Idle until a complete, ordered phase chain exists.
            if str(city.get("asset_class") or metadata.get("asset_class") or "").lower() == "industrial" and density > 0:
                activity_states = [states.get(f"d{density}_v{value}_phase_{phase}", {}) for phase in range(1, 9)]
                activity_paths = [
                    _path
                    for activity_state in activity_states
                    if (_value := activity_state.get("assets", {}).get("processed", {}).get("file", ""))
                    and (_path := asset_root / _value).is_file()
                ]
                if len(activity_paths) == 8:
                    activity_section = f"Cell.{density}.{value}.Active"
                    manifest[activity_section] = {
                        "Frames": "8",
                        "FrameMs": "128",
                        "FrameWidth": str(target_size[0]),
                        "FrameHeight": str(target_size[1]),
                        "AnchorX": str(target_size[0] // 2),
                        "AnchorY": str(target_size[1]),
                        "Loop": "true",
                        "AtlasCount": "8",
                        "Fallback": "idle-growth-cell",
                    }
                    for phase_index, activity_path in enumerate(activity_paths):
                        destination = Path("atlases") / "active" / slot / f"{phase_index:02d}.png"
                        (output / destination).parent.mkdir(parents=True, exist_ok=True)
                        with Image.open(activity_path) as source:
                            compact = source.convert("RGBA")
                            if compact.size != target_size:
                                compact = compact.resize(target_size, Image.Resampling.LANCZOS)
                            compact.save(output / destination, optimize=True)
                        suffix = str(phase_index)
                        manifest[activity_section][f"Atlas.{suffix}"] = destination.as_posix()
                        manifest[activity_section][f"FirstFrame.{suffix}"] = str(phase_index)
                        manifest[activity_section][f"ChunkFrames.{suffix}"] = "1"
                        manifest[activity_section][f"Columns.{suffix}"] = "1"
                        manifest[activity_section][f"Rows.{suffix}"] = "1"
                        packaged += 1

    if packaged == 0:
        raise SystemExit("No accepted Compact density/value cells were eligible for packaging")
    text = io.StringIO()
    manifest.write(text, space_around_delimiters=False)
    (output / "zone.ini").write_text(text.getvalue().rstrip() + "\n", encoding="ascii", newline="\n")
    print(f"wrote {output / 'zone.ini'} with {packaged} accepted Compact cell(s)")
    return packaged


def package_building(source_unit: Path, output: Path, obj_pic: str, house_id: int) -> int:
    """Package accepted static/activity Compacts for a non-zone DuneCity building."""

    source_unit = source_unit.resolve()
    metadata = json.loads((source_unit / "unit.json").read_text(encoding="utf-8"))
    if str(metadata.get("target_game", "")).lower() != "dunecity":
        raise SystemExit("The source asset is not namespaced for DuneCity")

    asset_root = source_unit.parent.parent
    states = metadata.get("categories", {}).get("building_idle", {}).get("states", {})
    numbered = sorted(
        (
            (int(key.removeprefix("frame_")), key, value)
            for key, value in states.items()
            if key.startswith("frame_") and key.removeprefix("frame_").isdigit()
        ),
        key=lambda item: item[0],
    )
    selected = [(key, value) for _, key, value in numbered]
    if not selected and "default" in states:
        selected = [("default", states["default"])]

    manifest = configparser.ConfigParser()
    manifest.optionxform = str
    manifest["Building"] = {
        "ObjPic": obj_pic,
        "HouseID": str(house_id),
        "SourceUnit": str(metadata.get("slug", source_unit.name)),
    }
    packaged = 0
    output.mkdir(parents=True, exist_ok=True)
    for key, state in selected:
        compact_value = state.get("assets", {}).get("processed", {}).get("file", "")
        compact_path = asset_root / compact_value
        if not compact_path.is_file():
            continue
        destination = Path("frames") / f"{packaged:02d}_{key}.png"
        (output / destination).parent.mkdir(parents=True, exist_ok=True)
        with Image.open(compact_path) as source:
            source.convert("RGBA").save(output / destination, optimize=True)
        manifest[f"Frame.{packaged}"] = {"File": destination.as_posix(), "SourceSlot": key}
        packaged += 1

    if packaged == 0:
        raise SystemExit("No accepted Compact building frames were eligible for packaging")
    manifest["Building"]["Frames"] = str(packaged)
    text = io.StringIO()
    manifest.write(text, space_around_delimiters=False)
    (output / "building.ini").write_text(text.getvalue().rstrip() + "\n", encoding="ascii", newline="\n")
    print(f"wrote {output / 'building.ini'} with {packaged} accepted Compact frame(s)")
    return packaged


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_unit", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--item-id", type=int)
    parser.add_argument("--obj-pic", choices=("NuclearPlant", "PoliceStation", "Stadium", "Airport", "Hospital", "Church"))
    parser.add_argument("--house-id", required=True, type=int)
    args = parser.parse_args()
    if args.obj_pic:
        package_building(args.source_unit, args.output, args.obj_pic, args.house_id)
    elif args.item_id is not None:
        package(args.source_unit, args.output, args.item_id, args.house_id)
    else:
        parser.error("--item-id is required for a zone, or use --obj-pic for a special building")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
