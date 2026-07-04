-- Diablo II and Diablo II: Resurrected are trademarks of Blizzard
-- Entertainment. This file contains structured data derived from D2
-- modding text tables (raresuffix.txt) that themselves originate from Blizzard
-- game data. Distributed here with no warranty; you may not have
-- permission to use this file. Consult your local copyright law and/or
-- use at your own risk. See d2rsavegameparser/LICENSE for the upstream
-- notice.
--
-- Committed as the canonical seed for the reference SQLite DB;
-- edit this file directly or replace it from a fresh CASC / mod-
-- txt extract if you need to update the data.

DROP TABLE IF EXISTS raresuffix;
CREATE TABLE raresuffix (
    "name" TEXT,
    "version" TEXT,
    "itype1" TEXT,
    "itype2" TEXT,
    "itype3" TEXT,
    "itype4" TEXT,
    "itype5" TEXT,
    "itype6" TEXT,
    "itype7" TEXT,
    "etype1" TEXT,
    "etype2" TEXT,
    "etype3" TEXT,
    "etype4" TEXT
);

BEGIN;
INSERT INTO raresuffix VALUES ('bite', '0', 'swor', 'knif', 'spea', 'pole', 'axe', 'h2h', NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('scratch', '0', 'swor', 'knif', 'spea', 'pole', 'h2h', NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('scalpel', '0', 'swor', 'knif', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('fang', '0', 'swor', 'knif', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('gutter', '0', 'swor', 'knif', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('thirst', '0', 'swor', 'knif', 'spea', 'pole', 'axe', 'h2h', NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('razor', '0', 'swor', 'knif', 'axe', 'h2h', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('scythe', '0', 'swor', 'axe', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('edge', '0', 'swor', 'knif', 'axe', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('saw', '0', 'swor', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('splitter', '0', 'axe', 'mace', 'club', 'hamm', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('cleaver', '0', 'swor', 'axe', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('sever', '0', 'swor', 'axe', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('sunder', '0', 'axe', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('rend', '0', 'axe', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('mangler', '0', 'axe', 'mace', 'club', 'hamm', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('slayer', '0', 'axe', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('reaver', '0', 'axe', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('spawn', '0', 'axe', 'hamm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('gnash', '0', 'axe', 'club', 'hamm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('star', '0', 'mace', 'hamm', 'scep', 'wand', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('blow', '0', 'mace', 'club', 'hamm', 'scep', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('smasher', '0', 'mace', 'club', 'hamm', 'scep', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('Bane', '0', 'mace', 'scep', 'wand', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('crusher', '0', 'mace', 'club', 'hamm', 'scep', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('breaker', '0', 'mace', 'club', 'hamm', 'scep', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('grinder', '0', 'mace', 'club', 'hamm', 'scep', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('crack', '0', 'mace', 'club', 'hamm', 'scep', 'wand', NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('mallet', '0', 'hamm', 'club', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('knell', '0', 'mace', 'club', 'scep', 'wand', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('lance', '0', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('spike', '0', 'swor', 'knif', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('impaler', '0', 'swor', 'knif', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('skewer', '0', 'swor', 'knif', 'spea', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('prod', '0', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('scourge', '0', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('wand', '0', 'wand', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('wrack', '0', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('barb', '0', 'swor', 'knif', 'axe', 'spea', 'pole', 'h2h', NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('needle', '0', 'swor', 'knif', 'spea', 'miss', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('dart', '0', 'spea', 'miss', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('bolt', '0', 'miss', 'jave', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('quarrel', '0', 'miss', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('fletch', '0', 'miss', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('flight', '0', 'miss', 'jave', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('nock', '0', 'miss', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('horn', '0', 'helm', 'miss', 'knif', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('stinger', '0', 'swor', 'knif', 'spea', 'miss', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('quill', '0', 'knif', 'miss', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('goad', '0', 'spea', 'pole', 'staf', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('branch', '0', 'spea', 'staf', 'bow', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('spire', '0', 'staf', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('song', '0', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('call', '0', 'rod', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('cry', '0', 'rod', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('spell', '0', 'rod', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('chant', '0', 'rod', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('weaver', '0', 'rod', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('gnarl', '0', 'club', 'wand', 'staf', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('visage', '0', 'helm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('crest', '0', 'helm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('circlet', '0', 'helm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('veil', '0', 'helm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('hood', '0', 'helm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('mask', '0', 'helm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('brow', '0', 'helm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('casque', '0', 'helm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('visor', '0', 'helm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('cowl', '0', 'helm', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('hide', '0', 'tors', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('Pelt', '0', 'tors', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('carapace', '0', 'tors', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('coat', '0', 'tors', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('wrap', '0', 'tors', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('suit', '0', 'tors', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('cloak', '0', 'tors', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('shroud', '0', 'tors', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('jack', '0', 'tors', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('mantle', '0', 'tors', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('guard', '0', 'shld', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('badge', '0', 'shld', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('rock', '0', 'shld', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('aegis', '0', 'shld', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('ward', '0', 'shld', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('tower', '0', 'shld', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('shield', '0', 'shld', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('wing', '0', 'shld', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('mark', '0', 'shld', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('emblem', '0', 'shld', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('hand', '0', 'glov', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('fist', '0', 'glov', 'h2h', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('claw', '0', 'glov', 'h2h', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('clutches', '0', 'glov', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('grip', '0', 'glov', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('grasp', '0', 'glov', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('hold', '0', 'glov', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('touch', '0', 'glov', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('finger', '0', 'glov', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('knuckle', '0', 'glov', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('shank', '0', 'boot', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('spur', '0', 'boot', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('tread', '0', 'boot', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('stalker', '0', 'boot', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('greave', '0', 'boot', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('blazer', '0', 'boot', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('nails', '0', 'boot', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('trample', '0', 'boot', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('Brogues', '0', 'boot', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('track', '0', 'boot', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('slippers', '0', 'boot', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('clasp', '0', 'belt', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('buckle', '0', 'belt', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('harness', '0', 'belt', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('lock', '0', 'belt', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('fringe', '0', 'belt', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('winding', '0', 'belt', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('chain', '0', 'belt', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('strap', '0', 'belt', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('lash', '0', 'belt', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('cord', '0', 'belt', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('knot', '0', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('circle', '0', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('loop', '0', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('eye', '0', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('turn', '0', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('spiral', '0', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('coil', '0', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('gyre', '0', 'ring', 'orb', 'jewl', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('band', '0', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('whorl', '0', 'ring', 'orb', 'jewl', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('talisman', '0', 'amul', 'jewl', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('heart', '0', 'amul', 'orb', 'jewl', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('noose', '0', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('necklace', '0', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('collar', '0', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('beads', '0', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('torc', '0', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('gorget', '0', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('scarab', '0', 'amul', 'jewl', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('wood', '0', 'spea', 'pole', 'wand', 'staf', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('brand', '0', 'blun', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('bludgeon', '0', 'blun', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('cudgel', '0', 'club', 'wand', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('loom', '0', 'miss', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('harp', '0', 'miss', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('master', '0', 'ring', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('barRI', '0', 'blun', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('hew', '0', 'swor', 'knif', 'axe', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('crook', '0', 'staf', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('mar', '0', 'swor', 'knif', 'mace', 'club', 'hamm', 'spea', NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('shell', '0', 'tors', 'helm', 'shld', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('stake', '0', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('picket', '0', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('pale', '0', 'spea', 'pole', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO raresuffix VALUES ('flange', '0', 'tors', 'mace', 'hamm', 'scep', NULL, NULL, NULL, NULL, NULL, NULL, NULL);
COMMIT;
