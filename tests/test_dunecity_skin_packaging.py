#!/usr/bin/env python3

import configparser
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "package-dunecity-skin.py"
SPEC = importlib.util.spec_from_file_location("package_dunecity_skin", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)

SYNC_SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "sync-dunecity-skins.py"
SYNC_SPEC = importlib.util.spec_from_file_location("sync_dunecity_skins", SYNC_SCRIPT)
SYNC_MODULE = importlib.util.module_from_spec(SYNC_SPEC)
assert SYNC_SPEC.loader is not None
sys.modules[SYNC_SPEC.name] = SYNC_MODULE
SYNC_SPEC.loader.exec_module(SYNC_MODULE)


class DuneCitySkinPackagingTests(unittest.TestCase):
    def test_high_detail_compact_keeps_pixels_and_logical_footprint(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            asset_root = root / "dune2"
            unit = asset_root / "units" / "dunecity_harkonnen_residential_zone"
            compact = unit / "categories" / "building_idle" / "states" / "d0_v0" / "processed.png"
            compact.parent.mkdir(parents=True)
            Image.new("RGBA", (64, 64), (100, 80, 40, 255)).save(compact)
            icon = unit / "categories" / "icon_sprite" / "states" / "default" / "processed.png"
            icon.parent.mkdir(parents=True)
            Image.new("RGBA", (182, 110), (10, 20, 30, 255)).save(icon)
            metadata = {
                "target_game": "dunecity",
                "slug": "dunecity_harkonnen_residential_zone",
                "dunecity": {
                    "compact_pixels_per_tile": 32,
                    "zone_atlas": {"density_columns": 4, "value_tier_rows": 4},
                },
                "render_profile": {
                    "logical_footprint_tiles": [2, 2],
                    "compact_frame_pixels": [64, 64],
                },
                "categories": {
                    "icon_sprite": {"states": {"default": {"assets": {
                        "processed": {"file": icon.relative_to(asset_root).as_posix()}
                    }}}},
                    "building_idle": {
                        "states": {
                            "d0_v0": {
                                "assets": {
                                    "processed": {
                                        "file": compact.relative_to(asset_root).as_posix(),
                                    }
                                }
                            }
                        }
                    }
                },
            }
            (unit / "unit.json").write_text(json.dumps(metadata), encoding="utf-8")
            output = root / "output"

            self.assertEqual(MODULE.package(unit, output, 20, 0), 1)

            with Image.open(output / "atlases" / "idle" / "d0_v0" / "00.png") as packaged:
                self.assertEqual(packaged.size, (64, 64))
            with Image.open(output / "icon.png") as packaged_icon:
                self.assertEqual(packaged_icon.size, (182, 110))
            manifest = configparser.ConfigParser()
            manifest.optionxform = str
            manifest.read(output / "zone.ini", encoding="ascii")
            self.assertEqual(manifest.getint("Zone", "FootprintWidth"), 2)
            self.assertEqual(manifest.getint("Zone", "FootprintHeight"), 2)
            self.assertEqual(manifest.getint("Render", "PixelsPerTile"), 32)
            self.assertEqual(manifest.getint("Render", "LogicalPixelsPerTile"), 16)
            self.assertEqual(manifest.getint("Cell.0.0.Idle", "FrameWidth"), 64)
            self.assertEqual(manifest.getint("Cell.0.0.Idle", "AnchorX"), 32)
            self.assertEqual(manifest.getint("Cell.0.0.Idle", "AnchorY"), 64)

    def test_high_detail_special_building_frames_are_copied_verbatim(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            asset_root = root / "dune2"
            unit = asset_root / "units" / "dunecity_harkonnen_stadium"
            compact = unit / "categories" / "building_idle" / "states" / "frame_0" / "processed.png"
            compact.parent.mkdir(parents=True)
            Image.new("RGBA", (192, 192), (120, 60, 30, 255)).save(compact)
            icon = unit / "categories" / "icon_sprite" / "states" / "default" / "processed.png"
            icon.parent.mkdir(parents=True)
            Image.new("RGBA", (91, 55), (10, 20, 30, 255)).save(icon)
            metadata = {
                "target_game": "dunecity",
                "slug": "dunecity_harkonnen_stadium",
                "categories": {
                    "icon_sprite": {"states": {"default": {"assets": {
                        "processed": {"file": icon.relative_to(asset_root).as_posix()}
                    }}}},
                    "building_idle": {
                        "states": {
                            "frame_0": {
                                "assets": {
                                    "processed": {
                                        "file": compact.relative_to(asset_root).as_posix(),
                                    }
                                }
                            }
                        }
                    }
                },
            }
            (unit / "unit.json").write_text(json.dumps(metadata), encoding="utf-8")
            output = root / "building-output"

            self.assertEqual(MODULE.package_building(unit, output, "Stadium", 0), 1)

            with Image.open(output / "frames" / "00_frame_0.png") as packaged:
                self.assertEqual(packaged.size, (192, 192))
            self.assertTrue((output / "icon.png").is_file())
            manifest = configparser.ConfigParser()
            manifest.optionxform = str
            manifest.read(output / "building.ini", encoding="ascii")
            self.assertEqual(manifest.getint("Building", "Frames"), 1)
            self.assertEqual(manifest.get("Building", "ObjPic"), "Stadium")
            self.assertEqual(manifest.get("Frame.0", "SourceSlot"), "frame_0")

    @staticmethod
    def _industrial_unit(root: Path, phases: dict, compact_size=(64, 64), phase_size=(64, 64)) -> Path:
        """Author a two-density industrial zone whose d1_v0 cell has `phases`.

        `phases` maps a phase number to True (author the PNG) or False (declare
        the state but leave the file missing), so a test can describe a
        complete, partial, or deliberately reordered chain.
        """
        asset_root = root / "dune2"
        unit = asset_root / "units" / "dunecity_harkonnen_industrial_zone"
        states = {}
        for slot in ("d0_v0", "d1_v0"):
            compact = unit / "categories" / "building_idle" / "states" / slot / "processed.png"
            compact.parent.mkdir(parents=True)
            Image.new("RGBA", compact_size, (60, 60, 60, 255)).save(compact)
            states[slot] = {"assets": {"processed": {"file": compact.relative_to(asset_root).as_posix()}}}
        for phase, authored in phases.items():
            name = f"d1_v0_phase_{phase}"
            frame = unit / "categories" / "building_idle" / "states" / name / "processed.png"
            frame.parent.mkdir(parents=True)
            if authored:
                # Distinct flat colour per phase so the packaged frame order is
                # observable from the written PNGs alone.
                Image.new("RGBA", phase_size, (phase * 10, 0, 0, 255)).save(frame)
            states[name] = {"assets": {"processed": {"file": frame.relative_to(asset_root).as_posix()}}}
        metadata = {
            "target_game": "dunecity",
            "slug": unit.name,
            "dunecity": {
                "asset_class": "industrial",
                "compact_pixels_per_tile": 32,
                "zone_atlas": {"density_columns": 2, "value_tier_rows": 1},
            },
            "render_profile": {
                "logical_footprint_tiles": [2, 2],
                "compact_frame_pixels": [64, 64],
            },
            "categories": {"building_idle": {"states": states}},
        }
        (unit / "unit.json").write_text(json.dumps(metadata), encoding="utf-8")
        return unit

    def test_complete_industrial_phase_chain_packages_ordered_active_frames(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            unit = self._industrial_unit(root, {phase: True for phase in range(1, 9)})
            output = root / "output"

            # Only the two Idle cells count as packaged Compact cells; the smoke
            # chain is an activity variant of the developed cell.
            self.assertEqual(MODULE.package(unit, output, 24, 1), 2)

            manifest = configparser.ConfigParser()
            manifest.optionxform = str
            manifest.read(output / "zone.ini", encoding="ascii")
            self.assertTrue(manifest.has_section("Cell.1.0.Active"))
            self.assertEqual(manifest.getint("Cell.1.0.Active", "Frames"), 8)
            self.assertEqual(manifest.getint("Cell.1.0.Active", "AtlasCount"), 8)
            self.assertTrue(manifest.getboolean("Cell.1.0.Active", "Loop"))
            self.assertEqual(manifest.getint("Cell.1.0.Active", "FrameWidth"), 64)
            self.assertEqual(manifest.getint("Cell.1.0.Active", "AnchorY"), 64)
            # The engine requires every chunk to start exactly where the
            # previous one ended, in order, and to cover Frames in total.
            covered = 0
            for index in range(8):
                self.assertEqual(manifest.getint("Cell.1.0.Active", f"FirstFrame.{index}"), covered)
                covered += manifest.getint("Cell.1.0.Active", f"ChunkFrames.{index}")
                self.assertEqual(
                    manifest.get("Cell.1.0.Active", f"Atlas.{index}"),
                    f"atlases/active/d1_v0/{index:02d}.png",
                )
            self.assertEqual(covered, 8)
            # Frame N holds authored phase N+1.
            for index in range(8):
                with Image.open(output / "atlases" / "active" / "d1_v0" / f"{index:02d}.png") as frame:
                    self.assertEqual(frame.convert("RGBA").getpixel((0, 0)), ((index + 1) * 10, 0, 0, 255))
            # An undeveloped cell never smokes.
            self.assertFalse(manifest.has_section("Cell.0.0.Active"))

    def test_partial_industrial_phase_chain_falls_back_to_idle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            phases = {phase: True for phase in range(1, 9)}
            phases[5] = False
            unit = self._industrial_unit(root, phases)
            output = root / "output"

            self.assertEqual(MODULE.package(unit, output, 24, 1), 2)

            manifest = configparser.ConfigParser()
            manifest.optionxform = str
            manifest.read(output / "zone.ini", encoding="ascii")
            # No partial chain: the engine keeps the static Idle Compact.
            self.assertFalse(manifest.has_section("Cell.1.0.Active"))
            self.assertTrue(manifest.has_section("Cell.1.0.Idle"))
            self.assertFalse((output / "atlases" / "active").exists())

    def test_reordered_industrial_phase_metadata_still_packages_authored_order(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            # Declare the states back to front; lookup is by phase number, so
            # the packaged frame order must not follow the metadata order.
            unit = self._industrial_unit(root, {phase: True for phase in range(8, 0, -1)})
            metadata = json.loads((unit / "unit.json").read_text(encoding="utf-8"))
            self.assertEqual(
                [key for key in metadata["categories"]["building_idle"]["states"] if "_phase_" in key],
                [f"d1_v0_phase_{phase}" for phase in range(8, 0, -1)],
            )
            output = root / "output"

            self.assertEqual(MODULE.package(unit, output, 24, 1), 2)

            for index in range(8):
                with Image.open(output / "atlases" / "active" / "d1_v0" / f"{index:02d}.png") as frame:
                    self.assertEqual(frame.convert("RGBA").getpixel((0, 0)), ((index + 1) * 10, 0, 0, 255))

    def test_industrial_phase_resize_keeps_pixel_art_edges(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            unit = self._industrial_unit(
                root, {phase: True for phase in range(1, 9)}, phase_size=(32, 32)
            )
            # Author one phase as a hard two-colour edge at half the target size.
            edge = unit / "categories" / "building_idle" / "states" / "d1_v0_phase_1" / "processed.png"
            image = Image.new("RGBA", (32, 32), (0, 0, 0, 255))
            for x in range(16, 32):
                for y in range(32):
                    image.putpixel((x, y), (255, 255, 255, 255))
            image.save(edge)
            output = root / "output"

            MODULE.package(unit, output, 24, 1)

            with Image.open(output / "atlases" / "active" / "d1_v0" / "00.png") as frame:
                scaled = frame.convert("RGBA")
                self.assertEqual(scaled.size, (64, 64))
                # Nearest neighbour introduces no blended intermediate pixels.
                self.assertEqual(
                    {pixel for pixel in scaled.getdata()},
                    {(0, 0, 0, 255), (255, 255, 255, 255)},
                )

    def test_sync_scans_manifests_and_packages_zone_and_building(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "oathkeeper" / "dune2"
            repo = root / "dunecity"
            skin_root = repo / "mods" / "dunecity" / "graphics_skins" / "Dune2"
            (skin_root / "zones").mkdir(parents=True)
            (skin_root / "buildings").mkdir()
            (repo / "CMakeLists.txt").write_text("project(test)\n", encoding="utf-8")

            zone = source / "units" / "dunecity_atreides_residential_zone"
            zone_compact = zone / "categories" / "building_idle" / "states" / "d0_v0" / "processed.png"
            zone_compact.parent.mkdir(parents=True)
            Image.new("RGBA", (64, 64), (10, 20, 30, 255)).save(zone_compact)
            zone_manifest = {
                "target_game": "dunecity",
                "name": "DuneCity Atreides Residential Zone",
                "slug": zone.name,
                "asset_class": "residential",
                "dunecity": {
                    "asset_class": "residential",
                    "faction": "atreides",
                    "source_asset": "Residential Zone",
                    "compact_pixels_per_tile": 32,
                    "zone_atlas": {"density_columns": 4, "value_tier_rows": 4},
                },
                "render_profile": {
                    "logical_footprint_tiles": [2, 2],
                    "compact_frame_pixels": [64, 64],
                },
                "categories": {
                    "building_idle": {
                        "states": {
                            "d0_v0": {
                                "assets": {"processed": {"file": zone_compact.relative_to(source).as_posix()}}
                            }
                        }
                    }
                },
            }
            (zone / "unit.json").write_text(json.dumps(zone_manifest), encoding="utf-8")

            building = source / "units" / "dunecity_harkonnen_stadium"
            building_compact = building / "categories" / "building_idle" / "states" / "frame_0" / "processed.png"
            building_compact.parent.mkdir(parents=True)
            Image.new("RGBA", (96, 96), (40, 50, 60, 255)).save(building_compact)
            building_manifest = {
                "target_game": "dunecity",
                "name": "DuneCity Harkonnen Stadium",
                "slug": building.name,
                "asset_class": "dunelegacy",
                "dunecity": {
                    "asset_class": "dunelegacy",
                    "faction": "harkonnen",
                    "source_asset": "Stadium",
                },
                "categories": {
                    "building_idle": {
                        "states": {
                            "frame_0": {
                                "assets": {
                                    "processed": {"file": building_compact.relative_to(source).as_posix()}
                                }
                            }
                        }
                    }
                },
            }
            (building / "unit.json").write_text(json.dumps(building_manifest), encoding="utf-8")

            legacy_zone = source / "units" / "dunecity_rebels_industrial_zone"
            legacy_compact = (
                legacy_zone / "categories" / "building_idle" / "states" / "default" / "processed.png"
            )
            legacy_compact.parent.mkdir(parents=True)
            Image.new("RGBA", (32, 32), (70, 80, 90, 255)).save(legacy_compact)
            legacy_manifest = {
                "target_game": "dunecity",
                "name": "DuneCity Rebels Industrial Zone",
                "slug": legacy_zone.name,
                "asset_class": "industrial",
                "dunecity": {
                    "asset_class": "industrial",
                    "faction": "rebels",
                    "source_asset": "Industrial Zone",
                    "zone_atlas": {"density_columns": 4, "value_tier_rows": 2},
                },
                "categories": {
                    "building_idle": {
                        "states": {
                            "default": {
                                "assets": {
                                    "processed": {"file": legacy_compact.relative_to(source).as_posix()}
                                }
                            }
                        }
                    }
                },
            }
            (legacy_zone / "unit.json").write_text(json.dumps(legacy_manifest), encoding="utf-8")

            old_building = skin_root / "buildings" / building.name
            old_building.mkdir(parents=True)
            Image.new("RGBA", (91, 55), (1, 2, 3, 255)).save(old_building / "icon.png")

            summary = SYNC_MODULE.synchronize(source, repo)

            self.assertEqual(summary["packages"], 2)
            self.assertEqual(summary["zones"], 1)
            self.assertEqual(summary["buildings"], 1)
            zone_output = skin_root / "zones" / zone.name
            with Image.open(zone_output / "atlases" / "idle" / "d0_v0" / "00.png") as image:
                self.assertEqual(image.size, (64, 64))
            zone_ini = configparser.ConfigParser()
            zone_ini.read(zone_output / "zone.ini", encoding="ascii")
            self.assertEqual(zone_ini.getint("Zone", "ItemID"), 20)
            self.assertEqual(zone_ini.getint("Zone", "HouseID"), 1)
            building_output = skin_root / "buildings" / building.name
            with Image.open(building_output / "frames" / "00_frame_0.png") as image:
                self.assertEqual(image.size, (96, 96))
            self.assertTrue((building_output / "icon.png").is_file())

    def test_sync_rejects_accepted_unit_without_engine_mapping_before_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "oathkeeper" / "dune2"
            repo = root / "dunecity"
            skin_root = repo / "mods" / "dunecity" / "graphics_skins" / "Dune2"
            (skin_root / "zones").mkdir(parents=True)
            (skin_root / "buildings").mkdir()
            (repo / "CMakeLists.txt").write_text("project(test)\n", encoding="utf-8")
            sentinel = skin_root / "zones" / "keep.txt"
            sentinel.write_text("unchanged", encoding="utf-8")

            unit = source / "units" / "dunecity_atreides_unknown_monument"
            compact = unit / "categories" / "building_idle" / "states" / "default" / "processed.png"
            compact.parent.mkdir(parents=True)
            Image.new("RGBA", (64, 64), (10, 20, 30, 255)).save(compact)
            manifest = {
                "target_game": "dunecity",
                "name": "DuneCity Atreides Unknown Monument",
                "slug": unit.name,
                "asset_class": "dunelegacy",
                "dunecity": {
                    "asset_class": "dunelegacy",
                    "faction": "atreides",
                    "source_asset": "Unknown Monument",
                },
                "categories": {
                    "building_idle": {
                        "states": {
                            "default": {
                                "assets": {"processed": {"file": compact.relative_to(source).as_posix()}}
                            }
                        }
                    }
                },
            }
            (unit / "unit.json").write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "no engine package mapping"):
                SYNC_MODULE.synchronize(source, repo)
            self.assertEqual(sentinel.read_text(encoding="utf-8"), "unchanged")


if __name__ == "__main__":
    unittest.main()
