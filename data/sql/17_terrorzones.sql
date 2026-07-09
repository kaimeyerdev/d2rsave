-- Diablo II and Diablo II: Resurrected are trademarks of Blizzard
-- Entertainment. Terror-zone catalogue extracted from
-- Data\hd\global\excel\desecratedzones.json inside the D2R CASC storage.
-- Ships as SQL so downstream consumers don't need CASC access at runtime.
--
-- Column semantics:
--   id       - pool index (0..33) matching the ORDER OF `zones[i]` in the
--              JSON file. This is the input to (some form of) modulo N in
--              the game's slot-picking RNG. If a future patch reorders the
--              zones array, this file must be regenerated.
--   game_id  - internal Blizzard zone identifier (e.g. "Act2-ArcaneSanctuary").
--              Stable across patches; use for correlation with the game's
--              own metadata (level lists, waypoint IDs, boss tables).
--   name     - human-readable display string. Matched verbatim against the
--              names on d2tz.info/offline so the oracle test can cross-
--              check the schedule.
--   act      - '1'..'5', derived from the Act prefix of game_id.
--
-- Regenerate with: d2r_casc_dump --d2r-install <path>
--                       --file "data:data\hd\global\excel\desecratedzones.json"
--                       --out /tmp/dz.json
-- Then rebuild this table by hand from the "zones" array. (Automating that
-- inside d2r_refdb_gen is TODO -- see TODO.md under Terror Zones.)
--
-- Extraction snapshot (also lives as constants in include/d2r/TerrorZones.hpp):
--   start_time_utc         = 2025-12-05 00:00:00 UTC
--   zone_duration_minutes  = 30
--   break_duration_minutes = 0
--   seed                   = 16665365343970128666  (0xE747457BC371F31A)

DROP TABLE IF EXISTS terrorzones;
CREATE TABLE terrorzones (
    "id"      TEXT,
    "game_id" TEXT,
    "name"    TEXT,
    "act"     TEXT
);

-- Pool order matches desecratedzones.json > desecrated_zones[0] > zones.
-- Act 1 (indices 0-9)
INSERT INTO terrorzones VALUES ('0',  'Act1-BurialGrounds',     'Burial Grounds, The Crypt, and The Mausoleum',                                                                                                                                    '1');
INSERT INTO terrorzones VALUES ('1',  'Act1-Catacombs',         'Cathedral, Inner Cloister, and Catacombs',                                                                                                                                        '1');
INSERT INTO terrorzones VALUES ('2',  'Act1-ColdPlains',        'Cold Plains and The Cave',                                                                                                                                                        '1');
INSERT INTO terrorzones VALUES ('3',  'Act1-DarkWood',          'Dark Wood and Underground Passage',                                                                                                                                               '1');
INSERT INTO terrorzones VALUES ('4',  'Act1-BloodMoor',         'Blood Moor and Den of Evil',                                                                                                                                                      '1');
INSERT INTO terrorzones VALUES ('5',  'Act1-Jail',              'Jail and Barracks',                                                                                                                                                               '1');
INSERT INTO terrorzones VALUES ('6',  'Act1-MooMooFarm',        'Moo Moo Farm',                                                                                                                                                                    '1');
INSERT INTO terrorzones VALUES ('7',  'Act1-Tristram',          'Stony Field, Tristram',                                                                                                                                                           '1');
INSERT INTO terrorzones VALUES ('8',  'Act1-Tower',             'Black Marsh, The Hole, and The Forgotten Tower',                                                                                                                                  '1');
INSERT INTO terrorzones VALUES ('9',  'Act1-Monastery',         'Tamoe Highland, Pit, Monastery Gate, and Outer Cloister',                                                                                                                         '1');

-- Act 2 (indices 10-16)
INSERT INTO terrorzones VALUES ('10', 'Act2-Sewers',            'Lut Gholein Sewers',                                                                                                                                                              '2');
INSERT INTO terrorzones VALUES ('11', 'Act2-RockyWaste',        'Rocky Waste and Stony Tomb',                                                                                                                                                      '2');
INSERT INTO terrorzones VALUES ('12', 'Act2-DryHills',          'Dry Hills and Halls of the Dead',                                                                                                                                                 '2');
INSERT INTO terrorzones VALUES ('13', 'Act2-FarOasis',          'Far Oasis and Maggot Lair',                                                                                                                                                       '2');
INSERT INTO terrorzones VALUES ('14', 'Act2-LostCity',          'Lost City, Valley of Snakes, Claw Viper Temple, and Ancient Tunnels',                                                                                                             '2');
INSERT INTO terrorzones VALUES ('15', 'Act2-TalRashas',         'Tal Rasha''s Tomb, Tal Rasha''s Chamber, and Canyon of the Magi',                                                                                                                 '2');
INSERT INTO terrorzones VALUES ('16', 'Act2-ArcaneSanctuary',   'Arcane Sanctuary, Harem, and Palace Cellar',                                                                                                                                      '2');

-- Act 3 (indices 17-22)
INSERT INTO terrorzones VALUES ('17', 'Act3-SpiderForest',      'Spider Forest, Arachnid Lair, and Spider Cavern',                                                                                                                                 '3');
INSERT INTO terrorzones VALUES ('18', 'Act3-GreatMarsh',        'Great Marsh',                                                                                                                                                                     '3');
INSERT INTO terrorzones VALUES ('19', 'Act3-FlayerJungle',      'Flayer Jungle, Flayer Dungeon, and Swampy Pit',                                                                                                                                   '3');
INSERT INTO terrorzones VALUES ('20', 'Act3-Kurast',            'Kurast Bazaar, Kurast Causeway, Kurast Sewers, Ruined Temple, Disused Fane, Forgotten Reliquary, Forgotten Temple, Ruined Fane, and Disused Reliquary',                            '3');
INSERT INTO terrorzones VALUES ('21', 'Act3-Travincal',         'Travincal',                                                                                                                                                                       '3');
INSERT INTO terrorzones VALUES ('22', 'Act3-DuranceOfHate',     'Durance of Hate',                                                                                                                                                                 '3');

-- Act 4 (indices 23-25)
INSERT INTO terrorzones VALUES ('23', 'Act4_OuterSteppes',      'Outer Steppes and Plains of Despair',                                                                                                                                             '4');
INSERT INTO terrorzones VALUES ('24', 'Act4-RiverOfFlame',      'River of Flame and City of the Damned',                                                                                                                                           '4');
INSERT INTO terrorzones VALUES ('25', 'Act4-ChaosSanctuary',    'Chaos Sanctuary',                                                                                                                                                                 '4');

-- Act 5 (indices 26-33)
INSERT INTO terrorzones VALUES ('26', 'Act5-BloodyFoothils',    'Bloody Foothills, Frigid Highlands and Abaddon',                                                                                                                                  '5');
INSERT INTO terrorzones VALUES ('27', 'Act5-ArreatPlateau',     'Arreat Plateau and Pit of Acheron',                                                                                                                                               '5');
INSERT INTO terrorzones VALUES ('28', 'Act5-CrystallinePassage','Crystalline Passage and Frozen River',                                                                                                                                            '5');
INSERT INTO terrorzones VALUES ('29', 'Act5-Halls',             'Nihlathak''s Temple and Temple Halls',                                                                                                                                            '5');
INSERT INTO terrorzones VALUES ('30', 'Act5-GlacialTrail',      'Glacial Trail and Drifter Cavern',                                                                                                                                                '5');
INSERT INTO terrorzones VALUES ('31', 'Act5-AncientsWay',       'Ancient''s Way and Icy Cellar',                                                                                                                                                   '5');
INSERT INTO terrorzones VALUES ('32', 'Act5-FrozenTundra',      'Frozen Tundra and Infernal Pit',                                                                                                                                                  '5');
INSERT INTO terrorzones VALUES ('33', 'Act5-WorldstoneKeep',    'Worldstone Keep, Throne of Destruction, and Worldstone Chamber',                                                                                                                  '5');
