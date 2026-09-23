# DuneCity graphics skins

DuneCity treats graphics skins as presentation data. They do not alter object
statistics, simulation rules, AI, maps, or the active gameplay mod.

## Selection rules

- Campaign uses the global **Campaign Graphics Skin** option.
- Custom, skirmish, and multiplayer lobbies expose a skin selector on each
  house slot. A shared-control house therefore has one shared skin.
- SimCity is always the default.
- The host controls AI slots. A human client may change the selector belonging
  to its own house through the same ownership rules as that house's other lobby
  controls.
- The selection is serialized and synchronized. Old settings and saves default
  to SimCity.

## Dune2 fallback contract

Mounted assets live below:

```text
<dunecity mod>/graphics_skins/Dune2/zones/<asset>/zone.ini
```

Each R/C/I density and value-tier cell is optional. The renderer starts from
the native SimCity atlas for every house and replaces only exact accepted Dune2
cells. Replacement clears the complete destination cell, disables any source
PNG colour key, and copies RGBA (including transparent pixels); this prevents
the native house-coloured SimCity lot border from showing through transparent
Compact pixels. Missing factions, assets, and individual cells therefore remain
native SimCity graphics.

`scripts/package-dunecity-skin.py` accepts only Oathkeeper's `processed.png`
(the approved Compact output). It never silently substitutes the large source
sprite. The selected Compact dimensions are retained as high-detail source
pixels while the manifest separately records the immutable logical footprint.

At runtime, DuneCity Dune2 skin PNGs use a dedicated alpha-aware RGBA Scale2x
and Scale3x implementation. It normalizes fully transparent pixels and scales
each atlas cell independently, preventing colour fringes and cross-frame pixel
bleeding. Classic DuneLegacy's indexed 8-bit sprites continue to use their
existing palette-based tiled scaler unchanged. Nearest-neighbour remains the
safe fallback if an RGBA atlas does not divide cleanly into its declared cells.

The renderer integration covers Residential, Commercial, and Industrial growth
atlases plus individually packaged Nuclear Plant, Police Station, Stadium,
Airport, Hospital, and Church graphics. Every missing faction, cell, building,
or frame continues to use the native SimCity art. Roads retain SimCity art until
their 16 topology-mask package consumer is added.

Special-building packages live below:

```text
<dunecity mod>/graphics_skins/Dune2/buildings/<asset>/building.ini
```

The package may contain several authored activity frames. The current engine
mounts as many as its native atlas exposes and repeats frame zero for missing
native slots, so a partial test package never leaves transparent holes.

## Automated local play-test deployment

`scripts/sync-dunecity-skins.py` scans Oathkeeper's DuneCity unit manifests,
derives faction, zone item ID or special-building object picture, and stages all
selected packages before replacing the live Dune2 skin tree. Existing authored
`icon.png` files are preserved. Unsupported or source-less units are reported
without guessing an engine mapping; an accepted but unsupported unit stops the
deployment before any live package is replaced.

The normal Windows-host workflow is one command:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\deploy-dunecity-skin-test.ps1 `
  -Scope all -InstallAndroidIfConnected
```

Use `-Scope current -Unit "DuneCity Harkonnen Stadium"` to deploy one unit.
The wrapper waits for synchronization, the `build-windows-skins` game target,
and the native Android/APK build in order. It installs the resulting APK when
ADB has an authorized device, or leaves the successfully built APK in place
when no phone is attached. `-InstallAndroid` is the strict variant that treats
a missing device as an error. `-SkipWindows` and `-SkipAndroid` are available
for deliberate platform-specific iteration. `-PlanOnly` validates every current
manifest-to-engine mapping without changing packages or starting either build.

## Installed-cache upgrades and recovery

Desktop profiles refresh the bundled DuneCity `graphics_skins` presentation
tree during startup without reseeding the mod's gameplay configuration. Android
packages compute a content fingerprint for that tree at build time. On launch,
the app compares the bundled fingerprint with the marker in app storage and
atomically replaces the exact graphics-cache subtree when an APK contains newer
art. This check is independent of `versionName`, so an asset-only test APK also
upgrades an existing installation. Saves, settings, and mod configuration are
outside the replaced directory.

If app storage is damaged or was populated by an older test build, use
**Options > Advanced > Clear DuneCity Asset Cache**, then restart. The button
removes only `<dunecity mod>/graphics_skins`; the current bundled payload is
restored on the next launch.

## Icon sprites

The construction list and selected-object properties panel resolve portraits
by house and active graphics skin. Until a dedicated icon is authored, the
Dune2 skin derives a 91x55 portrait from that house's accepted Compact; SimCity
continues using its native portrait. An authored file at either of these paths
takes priority for both UI consumers:

```text
zones/<asset>/icon.png
buildings/<asset>/icon.png
```

Authored icon sprites use the original 91x55 Dune II sidebar aspect. Missing
faction icons fall back safely to the derived Dune2 portrait or native SimCity
portrait. Oathkeeper's `~dune2config DuneCity` **Icon Sprite** lane stores its
accepted Compact at `categories/icon_sprite/states/default`; synchronization
installs that file as the package's `icon.png`. Native 91x55, 2x, 3x, 4x, and
custom 91:55 sources are supported, and the renderer fits all of them into the
same logical 91x55 UI rectangle with nearest-neighbor sampling. Without a new
Icon Sprite Compact, synchronization preserves any older authored `icon.png`.

## Canonical-main integration

The selectable graphics-skin implementation was ported onto Stefan's canonical
`ggtothemax/dunecity` `main` rather than merging the older QBot simulator
checkout over current gameplay work. The integration consists of:

1. Presentation-only skin state in game initialization, saves, lobby change
   events, campaign options, and per-house custom/multiplayer controls.
2. `GFXManager` SimCity snapshots, per-house skin switching, exact R/C/I cell
   replacement, special-building frame replacement, nearest-neighbour zooms,
   and texture-cache invalidation.
3. The complete-cell transparency rule above. A Dune2 Compact replaces its
   native cell; it is never alpha-composited over the SimCity artwork.
4. The native per-tile green/blue/yellow R/C/I zoning fill and border remain
   enabled for SimCity skins but are suppressed for Dune2-skinned houses. The
   overlay is rendered by `Tile`, independently of the sprite atlas, and would
   otherwise show through transparent Compact pixels.
5. The `mods/dunecity/graphics_skins/Dune2` package layout and
   `scripts/package-dunecity-skin.py` packaging workflow.
6. Native SimCity fallback for every absent faction, density/value state,
   special building, or activity frame.
7. Android packaging of the complete authored skin set, followed by runtime
   verification at all zoom levels and multiplayer serialization compatibility.
