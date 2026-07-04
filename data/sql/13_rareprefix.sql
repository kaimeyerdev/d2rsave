-- Diablo II and Diablo II: Resurrected are trademarks of Blizzard
-- Entertainment. This file contains structured data derived from D2
-- modding text tables (rareprefix.txt) that themselves originate from Blizzard
-- game data. Distributed here with no warranty; you may not have
-- permission to use this file. Consult your local copyright law and/or
-- use at your own risk. See d2rsavegameparser/LICENSE for the upstream
-- notice.
--
-- Committed as the canonical seed for the reference SQLite DB;
-- edit this file directly or replace it from a fresh CASC / mod-
-- txt extract if you need to update the data.

DROP TABLE IF EXISTS rareprefix;
CREATE TABLE rareprefix (
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
INSERT INTO rareprefix VALUES ('Beast', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Eagle', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Raven', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Viper', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('GhoulRI', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Skull', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Blood', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Dread', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Doom', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Grim', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Bone', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Death', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Shadow', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Storm', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Rune', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('PlagueRI', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Stone', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Wraithra', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Spirit', '0', 'armo', 'rod', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Storm', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Demon', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Cruel', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Empyrion', '0', 'rod', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Bramble', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Pain', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Loath', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Glyph', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Imp', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Fiendra', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Hailstone', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Gale', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Dire', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Soul', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Brimstone', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Corpse', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Carrion', '0', 'armo', 'weap', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Holocaust', '0', 'tors', 'helm', 'shld', 'swor', 'axe', NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Havoc', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Bitter', '0', 'armo', 'weap', 'misc', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Entropy', '0', 'ring', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Chaos', '0', 'ring', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Order', '0', 'ring', 'amul', 'scep', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Rule', '0', 'rod', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Warp', '0', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Rift', '0', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
INSERT INTO rareprefix VALUES ('Corruption', '0', 'ring', 'amul', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
COMMIT;
