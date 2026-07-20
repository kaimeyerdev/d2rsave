# d2rsavegameparser_cpp

A C++20 parser, inspector, and toolbox for **Diablo II: Resurrected** save
files (`.d2s` characters and `.d2i` shared stashes). Provides:

- **`d2rsave`** — the main CLI. Parses save files; lists items, quests,
  and attributes; renames characters in-place; changes map seeds;
  runs a chronicle/completion report against uniques, sets, and
  runewords; and offers a TUI dashboard for live-monitoring the save
  directory while D2R is running.
- **`d2r_refdb_gen`** — regenerates the SQLite reference database from
  a D2R CASC extract (unique-item names, set names, base types, etc.).
- **`d2r_casc_dump`** — one-shot CASC dumper, mainly used to bootstrap
  the reference database.
- **`d2r_tz_forecast`** — offline terror-zone forecaster (see the
  TODO for the reverse-engineering write-up).
- **`d2rsave_tests`** — Catch2 test binary exercising the parser
  against a committed fixture corpus.

## What `d2rsave` does

`d2rsave` complements D2R in a few areas the game itself doesn't cover:

- **Backups.** The tool maintains a local SQLite database of save-file
  snapshots (default `$XDG_DATA_HOME/d2rsave/backups.sqlite`),
  automatically capturing new snapshots as files change when the
  dashboard is running, and on demand from the CLI otherwise. Each
  snapshot is tagged with the save's state (save-and-exit, autosave,
  startup, external write). Historical snapshots can be listed,
  byte-diffed against one another, searched for items that appeared
  in prior states, and restored back to disk — a rollback path for
  save corruption, mistaken vendor sales, hardcore deaths, or "when
  did I last have that unique?" investigations, all without touching
  the live save file.

- **Live dashboard.** A terminal UI (built on ftxui) that watches the
  save directory while D2R is running and refreshes automatically as
  the game writes new save state. Panels cover the currently-active
  character, long-tail quest state, the current Terror Zone rotation,
  and an account-wide chronicle table showing uniques and sets found
  versus remaining. A `--print` mode renders the current view once
  as ANSI and exits, for pipes and screenshots.

- **Parser and query CLI.** The parser decodes `.d2s` characters and
  `.d2i` shared stashes — headers, attributes, skills, quest flags,
  waypoints, and the item bit-stream — into a structured in-memory
  representation. Subcommands expose that data as account-wide item
  queries (filterable by character, stash tab, quality, tier, and
  substring), collection-completion reports against known uniques
  and sets, and safe in-place edits (rename, map-seed change,
  checksum recompute) that preserve save-file invariants.

The remaining executables (`d2r_refdb_gen`, `d2r_casc_dump`,
`d2r_tz_forecast`) are supporting tools that seed the reference
SQLite database from a D2R CASC install or run stand-alone
terror-zone forecasting.

## Attribution

This project is a **C++ port of** [paladijn's Java
`d2rsavegameparser`](https://github.com/paladijn/d2rsavegameparser),
licensed under the **GNU Lesser General Public License v2.1**
(LGPL-2.1). The upstream project provided:

- The reverse-engineered `.d2s` / `.d2i` bit-stream layout that the
  parser here mirrors.
- The vendored cross-version regression corpus under
  [tests/fixtures/vendor/](tests/fixtures/vendor/) (see the
  [NOTICE](tests/fixtures/vendor/NOTICE.md) and
  [LICENSE-upstream](tests/fixtures/vendor/LICENSE-upstream) files
  in that directory for the upstream copy of the LGPL text).

If you redistribute this project, preserve those attribution files
alongside the vendored fixtures.

D2R itself, its game data, and all trademarks are property of
Blizzard Entertainment. This project ships **no game data**; it
requires you to point the CASC-derived tools at your own D2R
installation when regenerating the reference database.

## Building

The project standardises on **vcpkg (manifest mode)** for third-party
dependencies (`sqlite3`, `fmt`, `casclib`, `nlohmann-json`,
`ftxui`, `catch2`) and **CMake presets** for configure/build/test
flows. The primary presets are:

| Preset  | `CMAKE_BUILD_TYPE` | Purpose                                              |
| ------- | ------------------ | ---------------------------------------------------- |
| `Debug` | `Debug`            | Daily development. `-g`, no optimisation.            |
| `Release` | `RelWithDebInfo` | Optimised binary that still carries debug info.      |
| `ASan`  | Debug + sanitizer  | AddressSanitizer + UndefinedBehaviorSanitizer runs.  |

There are two supported ways to build.

### Option A — VS Code with the devcontainer (recommended)

If you have Docker and the "Dev Containers" VS Code extension
installed, this is the turnkey path:

1. Clone this repository.
2. Open the folder in VS Code and accept the prompt to **Reopen in
   Container** (or run `Dev Containers: Reopen in Container` from
   the command palette).
3. Wait for the container image to build. The devcontainer's
   `postCreateCommand` will:
   - Fix ownership on the persistent `vcpkg-cache` Docker volume
     (Docker creates named volumes root-owned by default).
   - Run [`./setup-vcpkg.sh`](setup-vcpkg.sh), which clones
     `microsoft/vcpkg` into `.vcpkg/` and builds the vcpkg binary
     from source.
4. Once the container is ready, in a container terminal:
   ```bash
   cmake --preset Debug
   cmake --build --preset Debug -j
   ctest --preset Debug
   ```
   The first `cmake --preset Debug` compiles every vcpkg
   dependency — expect several minutes. Subsequent runs restore
   from the `vcpkg-cache` Docker volume in seconds, even across
   `Rebuild Container`.

The container also ships preconfigured C/C++ and CMake VS Code
extensions and points `C_Cpp.default.compileCommands` at
`build/Debug/compile_commands.json`, so IntelliSense works
correctly for the vcpkg-enabled build once you've configured
`Debug` at least once.

See [.devcontainer/COLLABORATION.md](.devcontainer/COLLABORATION.md)
for the container-based collaboration workflow (where live save
files go, how debug artifacts are shared, etc.).

### Option B — Native build (without VS Code or a container)

The build system is portable to any Linux/macOS host with a modern
toolchain, though only Ubuntu 24.04 is regularly exercised.

**Prerequisites** (Ubuntu 24.04 package names; adapt for your
distribution):

```
build-essential  # gcc/g++ 13, make
cmake            # >= 3.25
git
sqlite3
autoconf automake autoconf-archive libtool pkg-config
zip unzip tar curl ca-certificates
bison flex gperf
python3
nasm yasm
ninja-build
libncurses-dev
```

The set of build-time tools mirrors the container image; see
[.devcontainer/Dockerfile](.devcontainer/Dockerfile) for the
authoritative list.

**Build:**

```bash
git clone https://github.com/<your-fork>/d2rsavegameparser_cpp.git
cd d2rsavegameparser_cpp

# One-time: clone microsoft/vcpkg into ./.vcpkg and compile the
# vcpkg binary. Takes ~1 minute of git + a few seconds of C++.
./setup-vcpkg.sh

cmake --preset Debug
cmake --build --preset Debug -j
ctest --preset Debug
```

`./setup-vcpkg.sh` is idempotent: it skips the clone if `.vcpkg/`
already exists and skips the vcpkg-binary build if `.vcpkg/vcpkg`
is already present. Re-run it any time the vcpkg checkout gets
into a bad state, or delete `.vcpkg/` first for a completely fresh
start.

The first `cmake --preset Debug` triggers vcpkg to compile every
manifest dependency in `vcpkg.json`. On a cold machine this is
the slow step (several minutes). vcpkg caches the resulting
binaries under `~/.cache/vcpkg` (or wherever
`VCPKG_DEFAULT_BINARY_CACHE` points), so subsequent presets
(`Release`, `ASan`) and rebuilds after `rm -rf build/` reuse
that cache and finish in seconds.

### Running

After a successful build:

```bash
./build/Debug/d2rsave --help          # main tool
./build/Debug/d2r_casc_dump --help
./build/Debug/d2r_refdb_gen --help
./build/Debug/d2r_tz_forecast --help
```

The `d2rsave` binary is safe to run against any `.d2s` / `.d2i`
file; parsing is read-only unless you invoke a subcommand that
explicitly writes (`rename`, `set-seed`, `backup restore`, etc.).

### Optional presets

`Release` is a `RelWithDebInfo` build (`-O2 -g -DNDEBUG`); use it
when you want an optimised binary that still gives usable stack
traces. `ASan` is a `Debug` build with AddressSanitizer and
UndefinedBehaviorSanitizer enabled; useful when chasing memory
errors or UB in the parser.

```bash
cmake --preset Release && cmake --build --preset Release -j
cmake --preset ASan    && cmake --build --preset ASan    -j
ctest --preset Release
ctest --preset ASan
```

Each preset lives in its own build tree under `build/<PresetName>/`,
so switching between them doesn't reconfigure the others.

## Repository layout

```
include/d2r/              Parser headers (public API)
src/                      Parser + CLI + subcommand implementations
tests/                    Catch2 unit tests
tests/fixtures/vendor/    Upstream regression corpus (LGPL, see NOTICE)
tests/fixtures/live-snapshot/  Personal-save fixtures scheduled for replacement (see TODO.md)
data/sql/                 SQL schema + seed data for the reference DB
legacy/                   Historical C rename_d2r tool, kept for regression comparison
.devcontainer/            Dockerfile + devcontainer.json + collaboration doc
.vcpkg/                   vcpkg checkout (created by setup-vcpkg.sh; gitignored)
build/                    CMake output, one subdir per preset (gitignored)
debug-artifacts/          Local drop zone for save files & scratch DBs (gitignored)
```

## License

The vendored test corpus under [tests/fixtures/vendor/](tests/fixtures/vendor/)
is licensed under LGPL-2.1 as documented in that directory. The
license for the C++ port itself is not yet declared at the top level;
this will be resolved before any public redistribution.
