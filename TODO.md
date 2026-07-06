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

## Build / packaging

* Add a top-level `LICENSE` file for this port. Because we vendor
  LGPL-2.1 test data under `tests/fixtures/vendor/`, at minimum the
  redistribution terms of that subtree need to be preserved; the port
  code itself can be whatever we choose (MIT / Apache-2.0 preferred).
* `data/sql/*.sql` files carry Blizzard-copyright text; keep the
  attribution headers when refactoring.
* Consider a `--version` flag on the CLI that prints commit hash +
  build type. Useful when someone reports weird chronicle output.
