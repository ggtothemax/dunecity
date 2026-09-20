# Workshop release candidate validation — 1.0.738

Tested locally on 20 September 2026 in `dunecity-pr53`.

## Scope

Fresh remote inspection found that `origin/main` and published `v1.0.737` both
resolve to `5848c510d09d6a95b4ddfc4a49cf4cb5f9d13382`. There were no additional
unreleased commits on main at inspection time. The local Workshop candidate
incorporates that complete main revision, including browser matchmaking and
packet pacing, public/solo campaigns, graphics skins and packaging, campaign
menus, screenshot handling, and current hot-join behavior. Candidate metadata
is 1.0.738 to avoid reusing a published version. Nothing was pushed or deployed.

## Corrections found during validation

- Integrated the new transport API while retaining full revision checksums.
- Browser Find Match publishes exact mod/map revisions before game setup;
  missing mods resolve through the content service after lobby callbacks exist.
- Fixed first received-map installation when a fresh profile has no multiplayer
  maps directory; added a real native empty-folder regression.
- Matched C++ and PHP validation for UTF-8/nonblank names and required mod.ini.
- Replaced recursive directory copying with verified per-file copies for Android;
  failed editable-copy creation cleans up its incomplete draft.
- Refreshed the optional Dune2R catalog against published 5848c510: current gravel
  and the previously omitted Sand pack. Catalog content uses committed bytes.
- Updated live probes to supply exact revision descriptors and browser tests to
  wait for content readiness before starting gameplay.

## Results

- Native Release app builds; dependency audit and source/app versions pass.
- Eight CTest groups pass. Main suite: 784 passed, three explicitly skipped.
  Includes saves/replays, gameplay/AI, packet bounds, authorization, revision
  storage, downloader security, and real menu probes at 640, 854 and 1280 widths.
- 206 PHP service cases pass; the compiled production C++ client/PHP integration
  passes upload resumption, duplicate assets, ownership, dependencies, versions,
  damaged-cache recovery and HTTP 429 retry.
- 42 asset/packaging/release Python tests pass. All 1,832 PNGs decode, 381 asset
  references resolve, and 769 Tornie checksums match. All 189 catalog files match
  local bytes and the public revision's Git tree.
- Browser build, generated JS verification, and bundled mod audit pass: 769 Tornie
  files, 288 DuneCity skin files, and six Dune2R seed files.
- Browser glue: 29 tests; direct bridge/SDK: 18; shell: five; framing: nine.
- Wasm32 wire harnesses: network 80 checks, relay 236 checks. The production
  WebRTC peer lifecycle passes the wasm AddressSanitizer harness.
- Five real local PHP/native WebRTC scenarios pass with exact checkpoint state:
  three-peer replacement (cycle 150), spectator promotion (cycle 1800), busy
  spectator catch-up/departure (55 objects), stalled spectator isolation (40s),
  and Dune City 128x128 shared AI (290 objects, cycle 150).
- Real-engine controls pass for vanilla and DuneCity. Screenshot tests verify
  physical output under logical scaling and a separate 257x193 render target.
- Two real Chromium clients pass pairing, cancel/retry, content publication before
  setup, exact-version readiness, gameplay command exchange and guest quit. A
  second run adds a host-only 11 MiB asset, exceeding the peer transfer ceiling:
  the guest downloads it through the community API, verifies every byte and plays
  successfully. Both runs report zero dropped commands and no browser exceptions.
- A third real-browser run confirms received map bytes and numbered revision
  metadata persist through IndexedDB and remain identical after a full reload.

## Limits

The candidate was built and run on Apple silicon macOS and Chromium/Wasm.
Windows/Linux release CI is green for published 1.0.737, not this local candidate.
Android device execution and cross-device/NAT play were not performed here.
The matching community PHP API still requires deployment before public use.
Signing, notarization, platform installers and automatic update delivery for
1.0.738 remain release-pipeline checks.

Detailed local logs and screenshots are under
`/tmp/dunecity-workshop/release-tests/`; these temporary artifacts are not shipped.
