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
            metadata = {
                "target_game": "dunecity",
                "slug": "dunecity_harkonnen_stadium",
                "categories": {
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
            manifest = configparser.ConfigParser()
            manifest.optionxform = str
            manifest.read(output / "building.ini", encoding="ascii")
            self.assertEqual(manifest.getint("Building", "Frames"), 1)
            self.assertEqual(manifest.get("Building", "ObjPic"), "Stadium")
            self.assertEqual(manifest.get("Frame.0", "SourceSlot"), "frame_0")

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
            zone_icon = zone / "categories" / "icon_sprite" / "states" / "default" / "processed.png"
            zone_icon.parent.mkdir(parents=True)
            Image.new("RGBA", (182, 110), (7, 8, 9, 255)).save(zone_icon)
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
                    },
                    "icon_sprite": {
                        "states": {
                            "default": {
                                "assets": {"processed": {"file": zone_icon.relative_to(source).as_posix()}}
                            }
                        }
                    },
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
            with Image.open(zone_output / "icon.png") as image:
                self.assertEqual(image.size, (182, 110))
                self.assertEqual(image.getpixel((0, 0)), (7, 8, 9, 255))
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
