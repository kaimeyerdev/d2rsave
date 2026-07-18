# TODO / Future work

Living list of deferred items. Ordered by rough priority within each group.
Prune entries as they land; add a link to the commit that closes them.

## Testing infrastructure

### Tier 3: synthesised save fixtures (blocked on a writer)

The current suite has two fixture tiers:

* `tests/fixtures/live-snapshot/` -- coupled to my personal D2R saves.
  Drifts every time I play; expected values must be re-captured.
* `tests/fixtures/vendor/` -- committed upstream corpus from paladijn's
  Java project. Stable but the coverage is whatever upstream happened to
  need for their tests.

Neither tier lets us assert byte-exact parser output for hand-designed
scenarios (e.g. "a level-1 fresh Warlock with exactly 4 minor healing
potions", or "a rare amulet with prefixes A and B and suffix C"). To get
there we need a **save writer**: enough of an inverse of `CharacterParser`
+ `ItemParser` to produce a valid `.d2s` byte stream from a small
in-memory description.

Rough sub-plan:

1. `d2r::SaveBuilder` (or extend `Character` with an emit path) that
   round-trips: `parse(build(x)) == x` for a hand-constructed `x`.
2. A minimal builder able to emit:
   * Header (magic, version, class byte, level, name, checksum) -- most
     of this already exists in `rename` / `checksum` paths.
   * Empty item section (JM + count = 0).
   * A single simple item (potion, tome).
3. `tests/synth/` directory with hand-crafted scenarios that build in
   code, write to a temp file, parse, and assert every relevant field.
4. Once the round-trip is working, migrate the "starter items" style
   assertions from `test_items.cpp` onto synthesised inputs so those
   tests no longer depend on `live-snapshot/`.

### Follow-ups on the existing suite

* Tighten `test_vendor.cpp` incrementally. Right now it only asserts
  the character header parses; the natural next step is to require the
  item section decodes when `itemsOffset != 0` and that
  `parser.parseItems(...)` matches `ch.itemCount`. Currently at least
  one vendored file (`3.1.91735/Chronicle.d2s`?) likely regresses this
  -- investigate before turning the assert on.
* Consider deleting the corrupt vendored `1.6.77312/Fjoerich.d2s` file
  entirely and dropping the skip list, once we're confident the file
  really is unused upstream (a `grep` in the Java repo showed no test
  references, but a manual pass would help).
* Add a CI harness (GitHub Actions or similar) that runs
  `cmake --preset debug && ctest --preset debug` in a container with
  vcpkg pre-bootstrapped. Right now the suite runs only on my machine.

## Chronicle reporting

### Item display: embellishments

A single `formatItem(name, type, location)` helper (in `src/main.cpp`)
now renders per-item lines uniformly across `items`, `chronicle`, and
`reconcile`. The three visible fields are deliberately minimal. Ideas
worth adding later, most requesting a `--verbose` / `--fields=...` /
`--no-color` style knob rather than changing the default:

  * **Catalog id.** Previously chronicle showed `[#297 Steel Shade]`;
    handy for cross-referencing `uniqueitems.txt` / `setitems.txt` rows
    and grep-friendly. Reintroduce as `--show-ids`.
  * **Quality colour.** Terminal colour on the name column keyed by
    quality (grey=normal, blue=magic, gold=unique, green=set,
    orange=rune, yellow=craft). Gate behind `isatty(stdout)` so pipes
    stay clean, plus `--color=always|never|auto`.
  * **Item level / character level.** Useful when scanning for
    twinkable finds. Column or trailing `(ilvl 92)` note.
  * **Sockets / ethereal / runeword indicator.** The current `items`
    diagnostic flags column (`ISERC`) is decent but cryptic; consider
    replacing with named suffixes (`+3 sockets, ethereal, runeword`).
  * **Location for chronicle "found" rows.** `chronicle` currently
    shows no location because it doesn't fully scan `.d2s` files for
    every item -- only for bit-29 blobs. Extending the scan to record
    per-id locations (like `reconcile` does) would let a `--full`
    listing show WHERE each `[X]` item lives.
  * **Chronicle timestamp column for `reconcile` discrepancy B.** The
    date is currently in a trailing note; a `--sort-by=date` view would
    want it as a proper field so it sorts sensibly.

### `items` command: additional filters

The `items` command currently supports scope (`--character NAME`,
`--shared-stash`), quality (`--unique`, `--set`, `--normal`, ...), and
trailing substring queries that match against name + base type.
Follow-ups worth adding as opt-in flags rather than default output:

  * **Sort order.** Currently forced alphabetical by name. Add
    `--sort-by=location|quality|ilvl|name` (default remains name).
  * **Narrower scope slicing.** When `--character X` is given we
    include X's merc / corpse / iron-golem too. Add
    `--exclude-merc`, `--exclude-corpse`, `--exclude-golem` (or
    `--character-only`) for finer control.
  * **Narrower stash slicing.** `--shared-stash` currently includes
    every tab. Add `--stash-tab N` (repeatable) for a single-tab view.
  * **Per-quality display grouping.** `--group-by=quality` prints one
    section per quality with its own count, instead of one flat list.
  * **Split name / type queries.** Trailing positionals currently
    match both name AND base type (union). Add explicit `--name STR`
    and `--type STR` (repeatable, AND across families) when a user
    needs to narrow just one axis.
  * **Character glob.** `--character "Amazon*"` or `--character
    "*Kai*"` for wildcarded selection instead of exact stems.

### Runewords: name mapping is missing


The runewords section in `cmdChronicle` is gated behind the
`D2R_ENABLE_RUNEWORDS_WIP` CMake option because it can only tell you
*how many* runewords the account has chronicled, not *which* ones.

Root cause: chronicle entries store a numeric itemId (~20500-range) that
indexes into D2R's `item-runes.json` string table. Our
`item-names.json` snapshot (from `d2rsavegameparser-examples`) doesn't
include the runewords table.

Options, in decreasing order of preference:

1. **Ship a runewords string table extract.** Get a fresh CASC extract
   of `item-runes.json` from a current D2R install and add it as a
   third table load (alongside `item-names.json`) in `data/sql`. Attribution and copyright story matches
   `item-names.json`. Once loaded, we can join chronicle IDs against
   both names + `runes.txt` metadata and print per-runeword found/missing
   exactly like uniques and sets.
2. **Reverse-engineer the ID scheme.** If the mapping turns out to be
   simply `chronicleId - 20500 = runes.txt row index` (or similar), we
   can derive names from `runes.txt` alone without shipping another
   string table. Worth spending an hour comparing observed IDs against
   `runes.txt` row order before pursuing option 1.
3. **Drop runeword name mapping entirely and just print counts.** The
   fallback we already have; keep it behind the WIP flag until (1) or
   (2) lands.

### Sunder charm names

`item-names.json` predates Blizzard's rename of the six sunder charms
(Latent / Renewed variants). The `SunderCharms.hpp` pair map is correct
but the display names shown to users come from the string table and are
stale. Fresh CASC extract fixes this at the same time as the runewords
table -- track together with (1) above.

### Chronicle output polish

Small stuff that came up in live use:

* Per-category coverage lines are nice; consider adding a
  `--summary-only` flag that prints just the coverage block (no
  remaining-items lists) for quick "did anything change" checks.
* When `--watch`-ing, colour-diff the coverage numbers vs. the previous
  iteration so newly-picked-up items are obvious.

## Parser correctness

### Merc / corpse / iron-golem item parsing

The marker-search approach for `jf` (merc) and `kf` (iron golem) has
guard bytes to avoid false positives from the item bit stream, but it's
still heuristic. A cleaner fix is to compute the exact end-of-character-
items offset from the item bit stream and require the mercenary marker
to sit immediately after it. Blocked on writing a bit-accurate item
walker (which is also on the path to the save writer above).

### Item bit stream: unhandled edge cases

Track under this heading as we find them. Currently known:

* Sunder charm crafted variants (`PreCrafted Bone Break` etc.) parse
  fine but their base-item lookup via `code` misses the "crafted"
  qualifier -- cosmetic only, doesn't affect chronicle logic.

## Dashboard

### Named map-seed aliases

The legacy `rename_d2r` C tool exists partly to switch between a handful
of favourite map seeds (well-rolled maps for a particular character or
farming route). The TUI dashboard currently shows the seed as a bare
32-bit decimal you can paste into `d2rsave set-seed`, but the number
itself is meaningless -- you have to remember which one is "the good
Baal seed" and which one is "the good cows seed".

Desired shape:

* User-editable table mapping `(name, seed)` -> label. Suggested
  storage: a `seed_aliases` table in the existing dashboard config DB
  (`$XDG_DATA_HOME/d2rsave/dashboard.sqlite`), keyed by seed with a
  free-form label column. Optional character-scoped column so the
  same seed can be labelled differently per character.
* Active Player pane: if the current map seed has an alias in the
  table, render `Map Seed  Cows-2A  (2839181721)` -- alias first, raw
  number second. Fall back to the raw decimal when no alias exists.
* Editing UI: pane-config-mode gains an entry for the Active Player
  panel (currently no config) that opens an inline editor: type an
  alias for the current seed, Enter to save. Or a global keybind like
  `A` in the Active Player panel when focused.
* CLI parity: `d2rsave seed-alias set <NAME> <SEED>` and
  `d2rsave seed-alias list`; `d2rsave set-seed <NAME>` should accept
  either an alias or a raw number. Sits alongside the existing
  `set-seed`, driven from the same SQLite table.
* Nice-to-have: on Character.d2s save events, if the character's new
  map seed matches an existing alias, print (or badge in the TUI) the
  alias so the user knows the current game is on a saved seed.

Interaction with legacy: `rename_d2r` writes seeds directly to the
.d2s file; the alias table is purely a UI convenience layered on top
of `d2rsave set-seed`, so nothing about the legacy tool changes.

### Terror Zones -- reverse-engineer the offline schedule

**Status: paused, pending disassembly of D2R.exe.** Everything we
could learn from oracle observations alone has been extracted; the
picker function itself is not derivable from black-box data at any
practical scraping volume. Pick this back up when we're ready to open
Ghidra.

#### What's in the repo (keep it all)

Public API + placeholder implementation:

* [`include/d2r/TerrorZones.hpp`](include/d2r/TerrorZones.hpp) --
  API surface, `TerrorZone` struct (with `gameId` matching Blizzard's
  internal name), `forecastTerrorZones`, `kDefaultTerrorZoneAnchor`,
  `kDefaultTerrorZoneSeed`, and `kTerrorZoneSlotLength`.
* [`src/terror_zones.cpp`](src/terror_zones.cpp) -- current placeholder
  `mix64()` (SplitMix64 finalizer). Slot-alignment / edge-case logic
  is correct; only the picker call is wrong. Replace `mix64()` with the
  reversed function.
* [`data/sql/17_terrorzones.sql`](data/sql/17_terrorzones.sql) --
  the 34 pool zones with `id` = JSON-order pool index, `game_id` =
  Blizzard's internal name, `name` = d2tz.info display string. Refresh
  this from CASC every time the game patches (see below).

Tooling:

* [`src/casc_dump_main.cpp`](src/casc_dump_main.cpp) -- `d2r_casc_dump`
  CLI. Given a D2R install path and a CASC-side file path, dumps the
  file to stdout or `--out`. Use this to re-pull
  `Data\hd\global\excel\desecratedzones.json` after any D2R patch.
* [`src/tz_forecast_main.cpp`](src/tz_forecast_main.cpp) --
  `d2r_tz_forecast` CLI. Prints a slot-by-slot forecast (uses the
  current placeholder). Useful for eyeballing changes to the picker.
* [`tests/test_terror_zones.cpp`](tests/test_terror_zones.cpp) plus
  [`tests/tz_oracle_data.cpp`](tests/tz_oracle_data.cpp) /
  [`tests/tz_oracle_data.hpp`](tests/tz_oracle_data.hpp) -- see
  "Tests" below.

Everything above should be preserved through the disassembly work; the
only file whose *contents* will change is `src/terror_zones.cpp` (swap
the `mix64` for the real function + adjust any per-slot rejection
loop).

#### Authoritative constants (from `desecratedzones.json`)

Extracted via `d2r_casc_dump --file "data:data\hd\global\excel\desecratedzones.json"`
and cached in `include/d2r/TerrorZones.hpp` and `data/sql/17_terrorzones.sql`.

| field                    | value                                    |
|--------------------------|------------------------------------------|
| `start_time_utc`         | `2025-12-05 00:00:00 UTC`                |
| `zone_duration_minutes`  | 30                                       |
| `break_duration_minutes` | 0                                        |
| `seed`                   | `16665365343970128666` (`0xE747457BC371F31A`) |
| pool size                | 34                                       |
| pool order               | matches `zones[i]` array in the JSON     |

Pool internal indices are NON-contiguous (`zones_0..9`, `_11`,
`_14..36`) -- Blizzard drops zones between patches without renumbering
existing ones. Keep the SQL file's row order aligned with `zones[i]`
in the JSON; any reshuffle silently invalidates every forecast.

The JSON also carries `manual_zones` (5 act-scoped pools) and a
`warnings` table (announcement tiers). We don't need those to forecast
zone identity, but the disassembly work should look at how they're
consumed since the picker may read them as part of its loop.

#### Empirical findings from the 2-week oracle

Two weeks (14 days x 48 slots = 672 obs), pasted from
d2tz.info/offline in America/Denver local time, are encoded in
[`tests/tz_oracle_data.cpp`](tests/tz_oracle_data.cpp) as a
`std::vector<d2r::TerrorZoneSlot>` matching the exact shape the
forecaster returns. Tests compare a forecast directly to a slice of
this oracle. Structural rules derived from those observations, all
encoded as `[!mayfail]` tests:

1. **K=1 no-repeat**: 0 self-transitions in 671 pairs.
   P(0 | uniform iid) = (33/34)^671 ~= 2e-9. Hard rule.
2. **K=2 no-repeat**: 0 A-B-A patterns in 670 triples. Same rejection
   window; also hard.
3. **K=3 suppressed but not zero**: 1 hit in 669. K=4..K=11 also
   remain well below chance. Interpretation: the picker maintains a
   ring buffer of the last 2 zones and rejects candidates in it; small
   K > 2 gets soft-suppressed by the anti-clustering step below.
4. **Anti-clustering**: zone-frequency variance is 5.89 over 672 obs;
   Poisson (uniform iid) would give ~19.8. Every one of 34 zones shows
   up 16..25 times. The picker is picking against a "recent frequency"
   score, not just a plain modulo.
5. **Minimum observed gap between same-zone repeats**: 3 slots.
   1.3% of gaps are <=5. So even with the K=2 rejection, short gaps
   are rare -- consistent with an "avoid last N" queue where N ~ 4-5,
   or a weighted picker that penalises very recent zones.
6. **Mutual information between any slot bit and pool_idx is
   0.01-0.05 bits** (max possible log2(34) = 5.09). No bit of `slot`
   alone leaks the output; the RNG is well-mixed.

#### Algorithms already ruled out (chance-level match rate)

Exhausted with two now-deleted Python probing scripts (search this
file's history under `scripts/tz_rng_search.py`,
`scripts/tz_rng_iterated.py`, `scripts/tz_2week_analysis.py`). All
listed candidates scored 5-25 / 672 vs. the ~19.8/672 chance rate
over 2 weeks of data:

* **Stateless mixers of `(seed, slot)`**: identity, XOR, add, SplitMix64
  finalizer, wyhash, PCG-style xor-fold, `seed*A + slot*B` sweep over
  {1, 0x9E37..., 0xBF58..., 0xC6BC..., FNV-64 offset} squared, byte-wise
  SHA-256 and MD5 over `struct.pack('<QQ', seed, slot)` and
  `struct.pack('>QQ', ...)` at every 32-bit word offset.
* **Iterated streams from `seed`**: xorshift64 (13/7/17),
  `lcg64_pcg` (mult=6364136223846793005, inc=1442695040888963407),
  `lcg_msvc` (214013/2531011), `lcg_nr` (1664525/1013904223), wyrand,
  SplitMix64-step, with 1..14 advances per slot and output extractions
  at bit offsets 0/16/32/xor-fold. Also tried seeding via
  `splitmix64_finalizer(seed)` and via `seed_low32`, `seed_high32`.
* **Fisher-Yates block shuffle**: no 34-slot window in 672 slots
  contains all 34 pool indices; longest strictly-non-repeating run is
  25. So the picker is NOT drawing without replacement from a deck.
* **Same but wrapped in K=1 rejection sampling**: adds a "keep drawing
  until output != last" loop around each of the above iterated
  candidates. Still chance rate.

None of this rules out RNGs we didn't test (e.g. Storm.dll's PRNG,
which is a Wichmann-Hill variant plus a lagged Fibonacci generator;
Blizzard's SFMT wrappers; some proprietary picker with per-slot state
outside the seed). The set is finite but big enough that guessing is
worse than looking.

#### Next: disassembly plan

The productive path is Ghidra/IDA on `D2R.exe`:

1. Load D2R.exe.
2. Grep the static strings for `"desecratedzones"` (there will be at
   least one hit -- the code that opens the CASC JSON references it by
   name).
3. Follow XREFs to the function that parses `desecratedzones.json`
   into a runtime struct. Note offsets for `zones`, `seed`,
   `start_time_utc`, `zone_duration_minutes`.
4. Find the function that reads "current active zone" -- likely called
   once per minute or on level entry. It's a pure function of
   `(now, seed, zones[], last_N_history)`. Read it out and translate
   to C++.
5. Replace the placeholder `mix64()` in
   `src/terror_zones.cpp::forecastTerrorZones` with the real picker.
   The API doesn't need to change.
6. Drop the `[!mayfail]` tag off each `[terrorzones]` test as it
   starts passing:
     * K=1 no-repeat  -- picker's rejection loop is right
     * K=2 no-repeat  -- history buffer length >= 2
     * Frequency variance -- anti-clustering weights are right
     * Big lookup (672/672) -- ALL of the above + the actual RNG

Once the algorithm is in, `d2r_tz_forecast` starts printing the true
schedule, and the dashboard's `Active Player` pane can populate
`snap.terrorZones.currentZone` / `.nextZone` directly.

#### Tests

The 6 `[terrorzones]` tests without `!mayfail` are API guard rails and
should never regress:

* Forecast is deterministic for identical inputs
* Each slot has zonesPerSlot distinct zones (synthetic-catalog API check)
* Slots align to :00 / :30 marks
* Slots exactly on a boundary don't shift
* Edge cases: empty inputs return empty forecasts
* Oracle vector is well-formed and every pool_idx is in range

The 5 `[terrorzones][!mayfail]` tests are the reversal signals:

* Forecast matches the 672-slot d2tz.info oracle    (`fcst == kD2tzInfoOracle`)
* Forecast matches the first 48 slots of the oracle (small-slice variant)
* No-repeat rule K=1
* No-repeat rule K=2
* Zone frequency is tightly bounded

Each `!mayfail` test runs on every build, reports its shortfall via
`INFO`, and reports "failed as expected" to Catch2 without turning
the run red. When you plug in the real picker, they'll pass one at a
time -- delete the `!mayfail` tag off each as it goes green.

To add more oracle weeks: paste the CSV into a scratch file, run the
one-off generator (see the "Regeneration" comment at the top of
[`tests/tz_oracle_data.hpp`](tests/tz_oracle_data.hpp)), overwrite
`tz_oracle_data.cpp`, and update the size / first / last assertions in
the "Oracle vector is well-formed" test.

### Backups -- follow-ups

The dashboard now writes automatic backups to
`$XDG_DATA_HOME/d2rsave/backups.sqlite` on watcher events, with a full
sweep at bootstrap, session-aware retention, and a CLI story for list
/ show / sessions / recover / prune / snapshot. A `PaneType::Backups`
renderer covers the interactive summary + detail views plus recovery
and retention modals. The following items build on that foundation
and are all deferred.

* **CLI parity for retention config.** The dashboard now persists
  retention edits to a `backup_retention (days, sessions)` table in
  `dashboard.sqlite`. The `d2rsave backups prune` verb still ignores
  it and takes flag defaults; teach it (and `snapshot`) to read the
  stored config when no flags are given.
* **`.d2s` diff (character-level).** Given two rows for the same
  filename, walk the `Character` structs (level, experience, quests,
  waypoints, items by fingerprint) and emit a typed diff.
* **`.d2i` diff (shared-stash).** Same idea for
  `SharedStashParser::parse()` output -- diff by tab, chronicle
  additions, item movements.
* **File-history browser pane.** Timeline per file with drill-in;
  overlaps with the Backups pane detail view.
* **Optional `.ma0` .. `.ma3` backup.** The scheduler currently
  persists only `.d2s` / `.d2i`. Map state might be worth capturing
  for waypoint recovery; low-priority.
* **CLI shell completions** for the new `backups` subcommands.
* **Dedicated hotkeys for the BackupLog + Session panes.** Right
  now clearing the backup-log ring buffer and resetting the session
  anchor both live under `[c] config` (they're the only entries in
  the config menu for those pane types). Common actions like clear/
  reset feel more natural as top-level keybinds, but the full
  dashboard keybinding table needs a coherent review before adding
  more: today's set (`c` config, `/` search, `r` refresh, `?` help,
  `Tab` focus, `q` quit, `Enter`/`Esc`/`R`/`E` in Backups) leaves
  few free letters. Revisit alongside a keybinding pass that also
  documents the config-mode hotkeys in `renderHelpModal`.

## Build / packaging

* Add a top-level `LICENSE` file for this port. Because we vendor
  LGPL-2.1 test data under `tests/fixtures/vendor/`, at minimum the
  redistribution terms of that subtree need to be preserved; the port
  code itself can be whatever we choose (MIT / Apache-2.0 preferred).
* `data/sql/*.sql` files carry Blizzard-copyright text; keep the
  attribution headers when refactoring.
* Consider a `--version` flag on the CLI that prints commit hash +
  build type. Useful when someone reports weird chronicle output.
