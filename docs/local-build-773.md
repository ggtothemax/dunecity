# Integrated local build 1.0.773

2026-09-24. User requested one playable MacBook Air build containing the local
branch changes. Integration checkout: `/Users/stefan/Documents/projects/dunecity-all-local`,
branch `integrate/all-local-773`. No public push, PR, tag or release is requested.

## Included work

| Local branch | Integration evidence |
|---|---|
| `fix/vanilla-ai-progression` | Base `610ddca4`, including AI economy/progression, urgent aircraft defence, reachable Hunt targeting and Dune2R depleted-field handling |
| `fix/map-duplicates-repair-layout` | Ancestor of the base: map deduplication/collection, repair layout and mature AI storage |
| `fix/remember-play-mode` | Both patches are equivalent to base commits `b4578eb7` / `7f2bc6a7` (`git cherry` reports both as already present) |
| `investigate/aircraft-aa` | Merged at `2c260400`: engine-adjusted turret/launcher cadence, health boundary, interrupted burst handling, all-mode tests and Tornie data/checksum |
| `feat/compact-city-scenarios` | Merged at `52da93bc`: all 12 scenarios, repaired SimCity map, generators and validation |
| `fix/ai-starport-mcv` | Recovery block and probe already present; block verified byte-identical to `66624b15`, probe diff empty; newer version/protocol fields retained |
| `test/all-local-760` | Its only missing content was historical release notes, merged at `40f82be9` |
| Older campaign, Windows cache and helper-population worktrees | Already ancestors of the integrated base |

Version 1.0.773 was committed at `cf537620`; protocol 27 combines the previously
separate lockstep AI and weapon changes. Save layout is unchanged. The optional
`platform/web/node_modules` dependency is reused from the existing local checkout;
no dependency runtime is shipped through that symlink.

## Packaging

Fresh native Release build with Homebrew dependencies and tests enabled. The app
was packaged through `cmake --install` into a new staging prefix, which bundles
dynamic dependencies and applies a verified ad-hoc signature. All 36 packaged
Mach-O files were checked for absolute workstation/Homebrew library dependencies.
The packaged binary SHA-256 is
`125a45097557c76f97af732eca3dc8c7a950673b926f72bc7832cda9798b6e3d`.

A ditto ZIP was transferred over SSH to the Air. The package passed SDL runtime
initialization and hidden-window rendering on both computers with only bundled
SDL libraries. These checks do not start a game or touch the user's game profile.

Full receipts, the package and the read-only Claude branch audit are under
`/Users/stefan/Documents/projects/outputs/all-local-773/`.

## Verified deployment

- Full CTest: **37/37 passed** in 339.10 seconds, including combined AI, Hunt,
  menu, Starport, carryall, weapon timing, projectile and save/observer checks.
- City-map validator passed; all 227 packaged map files are byte-identical to
  tracked source. All 552 additional aircraft encounter rows passed and match
  the previously verified cadence-fix outputs across the three base modes.
- Pre/post dependency audits passed; source version fields all agree on 1.0.773.
- Installed on `Stefans-MacBook-Air.local` at `/Applications/dunecity.app`.
  Version, SHA-256, deep/strict signature and packaged runtime/rendering all
  verified **after** installation. The binary matches the package hash above.
- Previous 1.0.768 app retained at
  `/Applications/.dunecity-backup-before-773-cf537620/dunecity.app`.
  The game was not running, no user profile/saves were modified, and no match
  was launched. Open the usual installed app to play.

This is a local arm64 macOS installation. Browser, Windows and Linux packages
were not built or published. No public release or update feed was changed.
