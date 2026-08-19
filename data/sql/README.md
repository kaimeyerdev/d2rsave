# Reference Database SQL Schema & Seed Data

This directory contains the SQL schema and data for the D2R reference database, automatically generated from the Diablo II: Resurrected CASC files.

## What's in here

Each `.sql` file corresponds to a table in the reference database:

- `00_schema.sql` — Table definitions and base schema
- `01_itemstatcost.sql` through `17_item_runes.sql` — Game data tables (item properties, uniques, sets, magic affixes, etc.)
- `MANIFEST` — Ordered list of files used during generation

The reference database is built at compile time by concatenating these files and feeding them to SQLite:

```bash
cat data/sql/*.sql | sqlite3 reference.sqlite
```

## Relationship to CASC utilities

The `d2r_refdb_gen` and `d2r_casc_dump` utilities read directly from a D2R game install (via CascLib) and regenerate these `.sql` files. They exist to keep this directory synchronized with the current D2R version.

**Important**: These SQL files are the *committed* source of truth. Users don't need a D2R install to build and run `d2rsave`—the SQL is already here. The CASC tools are for *maintainers* only, to refresh the data when D2R patches are released.

## When to regenerate

Run `d2r_refdb_gen` when:

- D2R receives a major content patch that changes item properties, unique names, or other game tables
- You need to verify the current data matches a fresh D2R install
- The build fails to find an expected item or property

Usage:

```bash
./build/Debug/d2r_refdb_gen --d2r-install /path/to/Diablo\ II\ Resurrected --out data/sql
```

See the [main README](../../README.md) for more details on the build process and configuration.

## License

Files in this directory carry Blizzard Entertainment copyright notices, as documented in the table sources themselves. These files are committed and distributed as part of the build process for `d2rsave`.
