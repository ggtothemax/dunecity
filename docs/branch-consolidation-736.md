# Combined branch audit for release 1.0.737

Snapshot: 20 September 2026. Every listed tip is an ancestor of candidate db1a9a7.
The release must be tagged from the subsequent main merge commit. Published
v1.0.735 at f836940 remains unchanged and is included in that history.

The final 1.0.737 integration additionally includes local branch
`fix/public-campaign-default` at a50938a26bf8abefceb20adca15cde5efa7dcb0e.
It changes campaign defaults and solo readiness and adds a privacy explanation.
The 1.0.736 candidate was never published.

## Integration decisions

- PR49: browser matchmaking and reviewed lifecycle, bounds, retry and menu fixes.
- PR28 ebfff71: direct-P2P command pacing. Integrated into current room and
  hot-join behavior; relay batching remains specific to RoomRelay.
- PR62 12768a2: persistent Windows dependency cache, retaining the SDK build setup.
- PR45 b739bbf: merged the fork tip. Current main already has the more complete
  physical render-target screenshot guard; kept that implementation rather than
  reverting to a logical-size fallback after a renderer-query failure.
- release-pr24-1.0.662: no unique patches under git cherry against main; recorded
  its history with an ours merge because the newer main already supersedes it.
- tornie-beta / tornie-1.0.517: unrelated old history, both at f66756c. Main imports
  the newer 1.0.520 snapshot (5a172ce, integration 3f45ab5). An ours merge preserves
  the earlier history without restoring obsolete 517 files over 520.
- All remaining branches were already included in main or became ancestors when
  the corresponding current PR was integrated.

Before removing any branch, refresh its remote tip and require ancestry against
merged main. Delete with an expected-tip lease, preserving any branch that advances
concurrently. Keep all release tags and other checkouts' uncommitted work.

| Origin branch | Audited tip |
| --- | --- |
| `codex/integrate-android-dune2r-tornie-1.0.520` | `3f45ab5695662e9e556c845472e45ea128cab92a` |
| `codex/release-1.0.530-test-builds` | `e7a5a1be3ec65926aa84ce4689bd4e3ccf4c28d8` |
| `codex/restore-city-save-infantry-fixes` | `80e6ad38bf838b31447bd7c6e132294cb9cf57e8` |
| `docs/map-browser-681-published` | `f2973a4770bee77471c528037e847ccc3a2a54d0` |
| `docs/release-679-outcome` | `0eaf289063e8a3b8df83f3150853f2d67ea6957a` |
| `docs/release-682-outcome` | `c3ddbe438fba3294520b79dd8ddb0162490558a2` |
| `docs/release-729-verification` | `f4538519e966e9b9a2a5ca9c8630ee9b0b8e54d7` |
| `feat/dunecity-skins-android` | `2b5092826c9c279f6642fd2bae39c963acd4a5f4` |
| `feat/menu-navigation` | `b0b9eb5ceaa0cdadfdf19b1d676baa76f38e0440` |
| `feat/p2pkit-direct-crossplay` | `8f7b57eb48b69d9eafd93957bf92e81a66efbcc6` |
| `feature/budget-integration` | `7439210b4e19afced2cf5785f8e7688c22187892` |
| `feature/campaign-controls` | `ca767449be82bd6fe26089a4c9e8538cfafe2fdd` |
| `feature/disaster-notification` | `4748bc9a9f2aca9f7e357b9abd0b38bc534ff43a` |
| `feature/fire-spread-combat` | `fbbb5adf9ed9b1848cc5a18630e71df4ddf3179f` |
| `feature/phase2-city-visuals` | `85f4b2cf2f4245af4665c70c32ca2bf323e2b589` |
| `feature/phase3-zone-structures` | `964b44f9343891420b56175df8c3d5765932855d` |
| `feature/road-network-growth` | `b9a43c886842e0ad606492750ea453c43e44d5b1` |
| `fix/browser-bundled-mods` | `8dd266565ea7341db349bdd942ef0408e94e77fa` |
| `fix/campaign-active-attacks` | `cdd06d36861df64ed0ef31f343d2d3d2b8244fed` |
| `fix/campaign-ai-attack-limits` | `f117665666920a6e5808b66e8b60c388059c2830` |
| `fix/campaign-dropdown-735` | `acc7951953751bc498717de77486281d45cb0b91` |
| `fix/campaign-repair-behavior` | `d4488e063fd4bb78cb5fa6bafbd9b44292c46f98` |
| `fix/city-spice-placement-709` | `241dea1679b908124f2797d4bf77d1c0dce5d40f` |
| `fix/commercial-demand-707` | `4f8a5f265f7aeb76860b3a6745ef5dc16278282e` |
| `fix/custom-map-browser` | `bed183452568fc27dec14f501a5720ea49ac209f` |
| `fix/direct-p2p-command-pacing` | `ebfff711688f661a55225d480b274159284bac85` |
| `fix/dunecity-ui-quantbot` | `2944fb6b3da042e5f627273189b9e90c75c650de` |
| `fix/hot-join-progress-733` | `ed0c9a0d43d983bfe71a109d4501a9d9dd2f0aff` |
| `fix/macos-notarization-setup` | `85b7c3f982d0ae7bc0d6727a434036a05e4cb1e0` |
| `fix/network-hardening` | `36d37f889a49561fdaca20adc7eff98e174010c2` |
| `fix/p2p-discord-events` | `fe2f03a3db3c287f8004b1f3017a3aba163ad45b` |
| `fix/quantbot-difficulty-balance` | `23b34c7f8e7488b57ebcd8bff56ffb0eef78bce5` |
| `fix/windows-vcpkg-cache` | `12768a2f221b4cac1c36f240f27236bb2efd876a` |
| `main` | `f8369407d3a7efd023d88b912b9a5479c3dd1bb2` |
| `release-1.0.657` | `bd41c7635093fbbd84351e628721e935f295ba27` |
| `release-1.0.659` | `7ef5c26926eec31a01ee3be5022a258ee34ce09c` |
| `release-1.0.660` | `52ac12eec38f4a1801d6f1c4952eebe711c9f4f6` |
| `release-1.0.661` | `43ec1dc0684462018e3747d26ee36ea7b311a80b` |
| `release-pr24-1.0.662` | `03d7bdb2f8c4260974d7d0e502c9159bb4dbfb26` |
| `release-pr24-final-1.0.662` | `b4d147122582b95952c9301120def7a7a13a2c6f` |
| `tornie` | `db3b070c22874732e38ae7fd944474c4922ce592` |
| `tornie-1.0.517` | `f66756c7ce84f1ce283f1615a70c78c05a48d2f7` |
| `tornie-beta` | `f66756c7ce84f1ce283f1615a70c78c05a48d2f7` |
