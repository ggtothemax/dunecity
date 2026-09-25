# DuneCity skin integration: 1.0.786

Integration base: current main `6d8e2a92` (1.0.783). Newer implementation:
PR #77 at `bb2c1481`; older comparison: PR #75 at `6c878987`.
The two #77 commits were replayed onto main. Only the useful #75 differences
were ported. No old gameplay, QuantBot, map, or protocol changes were imported.

## Feature and file disposition

This accounts for #75's authored changes relative to its own merge base, rather
than treating intervening changes on main as work from #75.

| #75 change | Disposition | Integrated result |
| --- | --- | --- |
| All 395 files under `mods/dunecity/graphics_skins` | Already present in #77 | Every file has identical content in #77; retain #77's complete 397-file payload. |
| High-detail Compact sizing and landscape icons | Already present in main/#77 | Source resolution is independent of the unchanged logical footprint. Both UI consumers use the house portrait. |
| `scripts/sync-dunecity-skins.py` icon lookup/copy | Superseded | #77 puts icon packaging in `package-dunecity-skin.py`, called by the synchronizer. Keep one implementation, including preservation of existing icons when no new one is authored. |
| Industrial activity in `scripts/package-dunecity-skin.py` | Ported and adapted | Require all eight phases in numeric order, use nearest-neighbor if resizing, omit incomplete Active chains. Original Compacts remain unchanged. |
| `src/structures/ZoneStructure.cpp` activity selection | Ported | Developed, powered industrial zones request Active frames; others request Idle. Animation uses game time. Missing Active cells fall back to Idle. |
| Desktop refresh in `src/mod/ModManager.cpp`, header | Ported and deduplicated | #77's content fingerprint drives a dedicated graphics-only refresh, not a gameplay reseed. No per-file size/mtime shortcut. Equivalent source/destination trees are not copied onto themselves. |
| Advanced action in `OptionsMenu.cpp`, header | Ported | Clear only the installed `graphics_skins` subtree; restart restores it. Desktop refuses to delete a sole shared/symlinked source. Android can recover from its APK. |
| Android fingerprint/extraction in `package-android-apk.ps1` | Adapted | Keep the native payload marker from #77; add a separate content marker inside `graphics_skins`. Artwork-only updates and manual recovery extract only that subtree. Unchanged markers skip copying. |
| Python packaging tests | Retained and extended | Eight tests cover logical sizing, icon/special-building packaging, synchronization, complete/missing/reordered industrial phases, and pixel-art resizing. |
| #75 graphics documentation and HANDOVER additions | Superseded | This current integration report and the updated graphics guide replace stale branch-specific deployment claims. No phone result from either old PR is claimed as a test of this integration. |

#77's campaign skin selector, campaign/co-op propagation, and selected R/C/I
portrait correction (`bb2c1481`) are retained. Hospital/Church civic overlays
and SimCity zones retain live previews. Cache metadata is excluded from stock
mod approval; actual artwork and gameplay files remain integrity-checked.
Workshop already excludes hidden local metadata from snapshots.

## Asset audit

| Snapshot | Files | Zone/building packages | Authored icons |
| --- | ---: | ---: | ---: |
| Main | 288 | 20 | 0 |
| #75 | 395 | 33 | 32 |
| #77 and integration | 397 | 34 (23 zones, 11 buildings) | 32 |

All #75 assets are byte-identical in #77. The two added files are the Atreides
Stadium manifest and its accepted frame, explaining 33 versus 34 packages. The integration does not change any
#77 asset bytes. All 397 source files match the local app bundle and the restored
isolated profile by SHA-256. Every referenced atlas/frame exists. All 32 icon
PNGs have 91:55 aspect. Atreides Hospital and Atreides Stadium have no authored
icon and retain fallback behavior. There is no difference from #77's 34/32 baseline.

The original Oathkeeper source tree is not available on this Mac. The deployment
script identifies `D:\BotServer\GitHub\Discord-AI-Bot\dune2`; its source hashes
have not been independently verified here. #77's handover reports two units
skipped for lack of eligible Compacts, but does not name them. Their identities
cannot be inferred from the packaged payload.

**Artwork blocker:** Neither PR contains any `Cell.*.Active` manifest section
or packaged industrial activity PNG. The integration preserves the runtime and
packager support, but the current delivered art remains static. Obtain the
accepted eight-phase Compacts before claiming visible smokestack animation or
merging this as complete. Do not manufacture frames or replace newer growth art.

## Validation and limits

- Native Apple-silicon Release build, with direct P2P enabled.
- Dependency audits before and after builds.
- Eight Python packaging tests.
- Real SDL skin probe: Atreides/Harkonnen R/C/I selected portraits match the
  construction portrait; 2x2 gameplay footprints; Hospital/Church and SimCity
  live previews; missing-cell fallback; campaign choice/retry and co-op override.
- Real cache probe: fresh installation, stale same-size/newer-timestamp files,
  same-version equal-length source edit with preserved timestamp, no rewrite on
  the second refresh, actual Advanced action and recovery, unchanged saves,
  settings, gameplay configuration and separate authored mod; same-tree safety.
- All 43 CTest targets pass across the full run (41/43) and focused reruns.
  The two initial failures shared the cache-marker content-approval cause; both
  pass after the fix. Final unit, skin/cache and menu reruns pass, including
  640/854/1280 menu layouts. Integration CI is recorded in the PR.

The cache tests cover graphics refresh, not changes to main's existing policy
for updating engine-managed stock gameplay defaults on a version upgrade.
Authored custom mods remain outside the cache operation.
No Android SDK/JDK/ADB or phone is available on this Mac. No Android build or
on-device result is claimed. The repository has no Android CI job. No actual
second-player session or interactive full-match balance test was performed.
Headless renderer probes are not a substitute for phone or two-player acceptance.

## Play-test guide

1. Launch the locally built `build/bin/dunecity.app` and check version **1.0.786**.
   In Campaign setup, choose the **Dune City** mod and **DuneCity skin → Dune2**.
   Start offline first. For Custom Game, set each desired house's **Skin** to
   **Dune2** in the player setup screen.
2. Test Atreides and Harkonnen. Build road-connected Residential, Commercial and
   Industrial zones with sufficient power; let them grow. Inspect zoom levels,
   transparency and the unchanged 2x2 placement footprint. Compare construction
   icons with selected built-zone portraits: the selected panel must show the
   landscape icon, not the old top-down lot preview.
3. Switch to **SimCity** for a comparison. Its zones keep live previews. Missing
   Dune2 cells/buildings use fallback art; Hospital and Church civic overlays
   keep their live previews. Atreides Hospital/Stadium lack authored icons.
4. Host an online campaign with Dune2 selected. With a second client, join the
   co-op lobby and change the joining house's **Skin**. Start and confirm the
   choice is retained. Skin is per house: players sharing one house share its
   skin. This still needs a real two-player test.
5. From **Settings → Advanced → Clear DuneCity Asset Cache**, clear, fully quit,
   and relaunch. Artwork should return and saves/settings should remain. A second
   unchanged launch should log `DuneCity graphics cache is current` on desktop.
   Android recovery requires the updated APK and a process restart, not resuming
   the existing activity.
6. Industrial powered smoke is **not yet a visual acceptance test**: the accepted
   phase artwork is missing from both PRs. Once supplied and packaged, verify
   phases 1–8 loop in order only for powered, developed industrial cells, then
   remove power and check Idle fallback.

Run `ctest --test-dir build -R 'graphics_skin_probe|menu_navigation_probe'
--output-on-failure` for the executable UI/cache checks. The skin probe writes
six zone screenshots under `build/skin-probe/` using isolated dummy-SDL profiles.
