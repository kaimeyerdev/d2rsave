# Vendored test corpus

The `.d2s` and `.d2i` save files in this directory tree were copied verbatim
from the upstream Java project **d2rsavegameparser** by paladijn:

  https://github.com/paladijn/d2rsavegameparser

Original location in that repository:

  `src/test/resources/<version>/`

The upstream project is licensed under the **GNU Lesser General Public
License, Version 2.1** (LGPL-2.1). A copy of that license is provided in
`LICENSE-upstream` next to this NOTICE.

## Why we vendor these files

They are stable, version-tagged save files curated by the upstream author to
exercise specific parser behaviour (character progression, item variants,
NG+ stash, crashes, quest state, etc.). Because they live inside the
upstream test suite rather than any player's live save folder, their
byte-level contents will not change unless the upstream project deliberately
updates them. This gives us a reproducible cross-implementation regression
corpus that does not depend on any local D2R installation.

## Attribution requirement

If this C++ port is redistributed, credit paladijn's project as the source
of these test files and preserve `LICENSE-upstream` alongside them. Do not
modify the vendored files in place; if a fixture needs adjustment, keep
the upstream copy untouched and add a separate override rather than
overwriting.

## Directory layout

Each subdirectory is named after the D2R game version the save was produced
under (matching the upstream layout):

  * 1.6.77312, 1.6.80273, 1.6.84219 -- Resurrected patch 1.6.x
  * 1.7.91403                        -- patch 1.7
  * 2.7, 2.8                         -- patches 2.7 and 2.8
  * 3.1.91636, 3.1.91735             -- patch 3.1 builds

New versions may be added as the upstream project accumulates them; do so
by copying the whole `src/test/resources/<version>/` subdirectory intact.
