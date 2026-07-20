# Collaboration protocol

How Copilot and I work together in this repo, now that everything
runs inside the devcontainer.

A machine-readable mirror of this document lives at
`/memories/repo/collaboration.md` and is loaded automatically at
the start of every Copilot session in this workspace. Keep both
files in sync when either changes.

## Environment contract

- Copilot operates exclusively inside the devcontainer at
  `/workspaces/d2rsavegameparser_cpp/`.
- Copilot never references host paths (`/home/kai/...`, `C:\...`).
  If a stale build cache surfaces one, the cache is bad and gets
  nuked.
- The VS Code chat sandbox stays off inside the container
  (`chat.agent.sandbox.enabled = off` in `devcontainer.json`).

## Directory roles

| Path | Role | Copilot may edit? |
|---|---|---|
| `src/`, `include/`, `tests/` (except fixtures), `data/`, `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `.devcontainer/`, `setup-vcpkg.sh`, `TODO.md`, `.gitignore` | Tracked source of truth | Yes, freely |
| `legacy/` | Historical C `rename_d2r` tool | Only on request |
| `tests/fixtures/live-snapshot/` | Committed `.d2s` / `.d2i` binaries; slated for replacement (see TODO) | Read-only for now |
| `tests/fixtures/vendor/` | Upstream corpus from paladijn's Java project | Read-only |
| `build/**` | CMake output (gitignored) | Sandbox; may `rm -rf` any subdir |
| `.vcpkg/` | vcpkg checkout (gitignored) | Off-limits except `./setup-vcpkg.sh` |
| `debug-artifacts/` | Shared drop zone (gitignored) | May create subdirs/files; never bulk-delete without asking |

### The `debug-artifacts/` layout

A single top-level gitignored directory holds both the files I
copy in from the host **and** anything Copilot produces for me to
inspect. Subdirectories carry meaning:

| Subdirectory | Purpose |
|---|---|
| `debug-artifacts/D2R_Saves/` | Mirror of my live D2R save directory. Copilot has read/write access with a safety confirmation before touching live files. |
| `debug-artifacts/sqlite/` | SQLite files I deliver for inspection (chronicle snapshots, dashboard DBs, reference DB copies). |
| `debug-artifacts/Test_Saves/` | Reserved for future user-produced deterministic test fixtures per the TODO "Replace tests/fixtures/live-snapshot/ with a user-scripted recipe". Empty for now. |
| `debug-artifacts/<other>/` | Added ad-hoc; when introducing a new subdir, update this file and `/memories/repo/collaboration.md`. |

## File transfer conventions

**Host → container (I do this):**
- Drag the file into VS Code's Explorer, dropping it under
  `debug-artifacts/<subdir>/`, **or**
- From a host terminal:
  `docker cp <src> <container>:/workspaces/d2rsavegameparser_cpp/debug-artifacts/<subdir>/`
- Then tell Copilot the filename in chat.

**Container → host (I do this):**
- Anything Copilot produces lands under a named path in
  `debug-artifacts/<subdir>/` that it announces in its reply.
- I copy it out the same two ways in reverse (drag out, or
  `docker cp <container>:<path> <dst>`).

No scp / rsync / network transfers — the bind-mounted workspace
is the only channel.

## Live-save safety rules

Because `debug-artifacts/D2R_Saves/` mirrors real save state,
Copilot follows two rules before any write:

1. **Confirm D2R is closed.** Copilot asks; I answer.
2. **Announce the overwrite target.** Copilot states the exact
   path(s) it's about to modify and why, in chat, before doing
   so.

For destructive experiments Copilot defaults to a sibling scratch
copy (e.g. `debug-artifacts/D2R_Saves/scratch/`) and only touches
a live file when I explicitly ask.

## Build / cache hygiene

- Standard reset: **`rm -rf build/<preset>`**. Never delete parts
  of a CMake cache in place — deleting `CMakeCache.txt` alone
  while `CMakeFiles/` remains is the failure mode that motivated
  this whole reorganization.
- Before wiping a build tree, Copilot moves any scratch DBs from
  `build/**` into `debug-artifacts/sqlite/`.
- The `vcpkg-cache` Docker volume (mounted at
  `/home/vscode/.cache/vcpkg`) persists across container
  rebuilds. Copilot won't clear it without permission — losing
  it means re-downloading all vcpkg ports.
- Preset lineup is standardized on **`Debug`** (daily driver,
  `-g` no `-O`) and **`Release`** (`RelWithDebInfo`: `-O2 -g
  -DNDEBUG`, so production binaries stay debuggable). `ASan`
  is kept as a specialized side preset for memory-safety runs
  (Debug + `-fsanitize=address,undefined`). Preset names are
  CamelCased to match the `CMAKE_BUILD_TYPE` convention; the
  obsolete `minimal-debug` preset has been removed.

## What I do outside the container

- Copy files in/out of `debug-artifacts/**`.
- Run D2R (produces / consumes live saves and, eventually,
  Test_Saves fixtures).
- Follow the `docs/test-fixtures.md` recipe once it lands, and
  drop the results into `debug-artifacts/Test_Saves/`.
- Own all remote git operations. Copilot runs local git commands
  freely (status, diff, add, commit-with-confirmation);
  `push`, force-push, amending published commits, and anything
  else that mutates a remote requires my explicit confirmation.

## Cold-start checklist

After a container rebuild or a long break, Copilot can get back
to green with:

```bash
ls -la .vcpkg/vcpkg          # if missing, run ./setup-vcpkg.sh
cmake --preset Debug         # configure (vcpkg restores from cache volume)
cmake --build --preset Debug -j
ctest --preset Debug         # 73 tests expected as of the last green run
```
