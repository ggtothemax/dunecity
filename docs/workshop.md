# Workshop and shared content

Workshop is the creation menu: Map Editor, Mod Editor, Asset Editors, and
Community Maps & Mods. Extras contains Replays, How to Play, and About & Credits.
Choose a mod when setting up a game. There is no separate activation screen.

## Creating and sharing

The mod editor starts with a project chooser. Copy a bundled mod or a downloaded
version to make an editable draft. Copies include the complete asset and campaign
tree, metadata, custom houses, and rules. The editor changes unit/building
properties, game rules, AI configuration, and descriptive metadata. It validates
field types and stages changed files before replacing the draft.

The map editor asks which mod supplies its units and rules. Save captures the map
and its required mod. Share uses the save dialog and publishes the resulting
revision. The map format's `[BASIC] Version` remains the existing terrain format;
it is unrelated to the Workshop revision number.

Asset Editors currently provides the existing Dune2R sprite previews, asset-pack
downloads, and animation-render settings. Choose an editable Dune2R copy. Its
render settings now live in the mod's `workshop-render.ini`, so saved and shared
revisions include them. This is not a general pixel-painting editor.

Community Maps & Mods supports browsing and downloading without an online game.
It also lists saved revisions, installed mods, and local maps for sharing. Loose
maps require an explicit mod choice; an unchanged downloaded map retains its
recorded dependency. Downloaded revisions are selectable in game setup. Editing
someone else's saved map or mod creates an independent draft identity.

## Versions and storage

A draft has a permanent item ID. Changed content creates the next numbered
revision; saving identical bytes and dependencies reuses the revision. Display
names are not identities. Renaming a draft keeps its item ID; independent drafts
with the same name remain separate. Older unversioned maps obtain a lineage
namespaced to the local publishing identity and their original name/author.
Bundled mod snapshots derive their IDs from the complete content, so optional
asset packs owned by different players cannot contend for the same publishing
capability.

The user data directory contains:

- `workshop/revisions/<sha256>/`: immutable manifest, version, and verified files.
- `workshop/owner`: private local publishing capability; back this up with drafts.
- `workshop/outbox/`: durable pending automatic publications for LAN hosting.
- `mods/<draft>/workshop-revision.ini`: draft ID and latest saved revision.
- `mods/ws-<sha256>/`: installed exact mod version, with its verified identity.
- `<map>.workshop.ini`: map revision and dependency lookup metadata.

A revision's SHA-256 covers a canonical manifest, including item identity,
display name, engine family, exact map-to-mod dependency, and every file's path,
size and SHA-256. The server deduplicates file blobs. Revision numbers are labels;
hashes remain the permanent references in saves, replays, and network settings.
Publishing is idempotent. If the server assigns an occupied local number, the
unpublished local revision receives the next free number without changing bytes
or hash. Existing published revisions are never overwritten.

Publishing ownership uses a random capability, not a player's display name.
Losing the capability requires an independent copy/new ID for further changes.
There are no verified-author badges or collaboration permissions implied by a
name. The PHP service keeps committed revisions while configured storage quotas
prevent unbounded growth; it does not automatically prune replay dependencies.

## Multiplayer and saves

Approved shipped mods (Vanilla, Dune City, Tornie and Dune2R) use their installed
files. Campaign starts online by default and automatically starts after admission;
it never captures, uploads or downloads a Workshop package. These games permit
hot joining. Join Online -> Create Campaign retains the editable pregame lobby.
Admission and readiness compare a self-identifying approved-mod token based on
the installed rules, alongside the matching app/protocol version and runtime
configuration. A joiner selects the corresponding installed mod locally.

New/authored mods require the pregame lobby, with hot joining disabled even for
copies based on an approved mod. These hosts publish their exact mod and map
before advertising. Joiners resolve the required version through a public listing
or private invitation before admission. Received maps are saved only after
manifest and payload hashes match. Same-name revisions receive separate installed
filenames. Approved custom maps retain local revision metadata for map transfer,
without automatic community publication.

LAN hosts using new mods capture the same revisions and queue publication.
Offline guests can use exact local content or the existing verified peer transfer
for packages up to 10 MiB. Larger missing packages need the community service.
For new mods, the lobby, readiness gate, and game start check the full mod revision
as well as runtime configuration. A missing or mismatched revision cannot silently
start.

MOD4 game settings record exact map/mod hashes and numbers. Older MOD3 settings
remain readable. New saves and replays resolve their original revision; legacy
saves with a known mismatched checksum report a failure rather than loading
unrelated current content. The network protocol is now 10 and save format 9840.

## Service and validation

The new API is part of `tools/p2p-signaling`. Deploy that service before using
community sharing against a production endpoint; building the game does not
deploy it. See its README for endpoints, storage, quotas, ownership, and recovery.
Only HTTPS is allowed, except an explicitly selected loopback development URL.
Transfers are cancellable and chunked, and rate-limited transfers retry without
publishing partial revisions. The browser uses the existing IDBFS persistence
sync hooks.

Validation includes native CTest suites, menu probes at 640/854/1280 widths,
Workshop snapshot/hash/path/version tests, PHP service tests, and a real C++
client-to-PHP transfer test:

```sh
python3 tools/p2p-signaling/test/test_content.py
python3 tools/p2p-signaling/test/test_client_smoke.py
ctest --test-dir build --output-on-failure
```

A local native build does not establish browser/Windows/Android device behavior,
public deployment, or a two-device live multiplayer validation.
