-- Diablo II and Diablo II: Resurrected are trademarks of Blizzard
-- Entertainment. This file contains structured data derived from D2
-- modding text tables (sets.txt) that themselves originate from Blizzard
-- game data. Distributed here with no warranty; you may not have
-- permission to use this file. Consult your local copyright law and/or
-- use at your own risk. See d2rsavegameparser/LICENSE for the upstream
-- notice.
--
-- Committed as the canonical seed for the reference SQLite DB;
-- edit this file directly or replace it from a fresh CASC / mod-
-- txt extract if you need to update the data.

DROP TABLE IF EXISTS sets;
CREATE TABLE sets (
    "index" TEXT,
    "name" TEXT,
    "version" TEXT,
    "pcode2a" TEXT,
    "pparam2a" TEXT,
    "pmin2a" TEXT,
    "pmax2a" TEXT,
    "pcode2b" TEXT,
    "pparam2b" TEXT,
    "pmin2b" TEXT,
    "pmax2b" TEXT,
    "pcode3a" TEXT,
    "pparam3a" TEXT,
    "pmin3a" TEXT,
    "pmax3a" TEXT,
    "pcode3b" TEXT,
    "pparam3b" TEXT,
    "pmin3b" TEXT,
    "pmax3b" TEXT,
    "pcode4a" TEXT,
    "pparam4a" TEXT,
    "pmin4a" TEXT,
    "pmax4a" TEXT,
    "pcode4b" TEXT,
    "pparam4b" TEXT,
    "pmin4b" TEXT,
    "pmax4b" TEXT,
    "pcode5a" TEXT,
    "pparam5a" TEXT,
    "pmin5a" TEXT,
    "pmax5a" TEXT,
    "pcode5b" TEXT,
    "pparam5b" TEXT,
    "pmin5b" TEXT,
    "pmax5b" TEXT,
    "fcode1" TEXT,
    "fparam1" TEXT,
    "fmin1" TEXT,
    "fmax1" TEXT,
    "fcode2" TEXT,
    "fparam2" TEXT,
    "fmin2" TEXT,
    "fmax2" TEXT,
    "fcode3" TEXT,
    "fparam3" TEXT,
    "fmin3" TEXT,
    "fmax3" TEXT,
    "fcode4" TEXT,
    "fparam4" TEXT,
    "fmin4" TEXT,
    "fmax4" TEXT,
    "fcode5" TEXT,
    "fparam5" TEXT,
    "fmin5" TEXT,
    "fmax5" TEXT,
    "fcode6" TEXT,
    "fparam6" TEXT,
    "fmin6" TEXT,
    "fmax6" TEXT,
    "fcode7" TEXT,
    "fparam7" TEXT,
    "fmin7" TEXT,
    "fmax7" TEXT,
    "fcode8" TEXT,
    "fparam8" TEXT,
    "fmin8" TEXT,
    "fmax8" TEXT,
    "uiclass" TEXT,
    "eol" TEXT
);

BEGIN;
INSERT INTO sets VALUES ('Civerb''s Vestments', 'Civerb''s Vestments', '0', 'res-fire', NULL, '25', '25', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'str', '0', '15', '15', 'dmg-undead', '0', '200', '200', 'res-ltng', '0', '25', '25', 'att%', '0', '25', '25', 'ac', NULL, '50', '50', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Hsarus'' Defense', 'Hsarus'' Defense', '0', 'thorns', NULL, '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'dmg-max', '0', '5', '5', 'nofreeze', '0', '1', '1', 'res-ltng', '0', '25', '25', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Cleglaw''s Brace', 'Cleglaw''s Brace', '0', 'ac', NULL, '50', '50', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'ac', '0', '50', '50', 'manasteal', '0', '6', '6', 'crush', '0', '35', '35', 'swing2', NULL, '20', '20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Iratha''s Finery', 'Iratha''s Finery', '0', 'ac', NULL, '50', '50', NULL, NULL, NULL, NULL, 'move2', NULL, '20', '20', 'pierce', NULL, '24', '24', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', '0', '20', '20', 'res-fire-max', '0', '10', '10', 'res-cold-max', '0', '10', '10', 'res-ltng-max', '0', '10', '10', 'res-pois-max', '0', '10', '10', 'dex', '0', '15', '15', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Isenhart''s Armory', 'Isenhart''s Armory', '0', 'str', NULL, '10', '10', NULL, NULL, NULL, NULL, 'dex', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'lifesteal', '0', '5', '5', 'res-all', '0', '10', '10', 'att%', '0', '35', '35', 'block', NULL, '30', '30', 'move2', NULL, '20', '20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Vidala''s Rig', 'Vidala''s Rig', '0', 'att', NULL, '75', '75', 'manasteal', NULL, '7', '7', 'dex', NULL, '15', '15', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'dmg-cold/lvl', '12', NULL, NULL, 'freeze', '0', '1', '1', 'pierce', '0', '50', '50', 'str', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'ama', '0');
INSERT INTO sets VALUES ('Milabrega''s Regalia', 'Milabrega''s Regalia', '0', 'att', NULL, '75', '75', 'dmg-ltng/lvl', '16', NULL, NULL, 'att', NULL, '125', '125', 'nofreeze', NULL, '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'lifesteal', '0', '8', '8', 'pal', '0', '2', '2', 'manasteal', '0', '10', '10', 'res-pois', '0', '15', '15', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'pal', '0');
INSERT INTO sets VALUES ('Cathan''s Traps', 'Cathan''s Traps', '0', 'dmg-fire', NULL, '15', '20', 'regen-mana', NULL, '16', '16', 'res-ltng', NULL, '25', '25', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'att', '0', '60', '60', 'red-mag', '0', '3', '3', 'res-all', '0', '25', '25', 'cast1', '0', '10', '10', 'mana', '0', '20', '20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'sor', '0');
INSERT INTO sets VALUES ('Tancred''s Battlegear', 'Tancred''s Battlegear', '0', 'dmg-ltng', NULL, '15', '15', NULL, NULL, NULL, NULL, 'lifesteal', NULL, '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'gold%', '0', '75', '75', 'res-all', '0', '10', '10', 'slow', '0', '35', '35', 'manasteal', '0', '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Sigon''s Complete Steel', 'Sigon''s Complete Steel', '0', 'lifesteal', NULL, '10', '10', NULL, NULL, NULL, NULL, 'ac', NULL, '100', '100', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-fire', '0', '12', '12', 'thorns', '0', '12', '12', 'red-dmg', '0', '7', '7', 'fire-max', '0', '24', '24', 'mana', NULL, '20', '20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Infernal Tools', 'Infernal Tools', '0', 'dmg-pois', '80', '25', '25', NULL, NULL, NULL, NULL, 'mana', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'att%', '0', '20', '20', 'nec', '0', '1', '1', 'openwounds', '0', '20', '20', 'manasteal', NULL, '6', '6', 'mana%', NULL, '20', '20', 'nofreeze', NULL, '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'nec', '0');
INSERT INTO sets VALUES ('Berserker''s Garb', 'Berserker''s Garb', '0', 'hp', NULL, '50', '50', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-pois-len', '0', '75', '75', 'pois-min', '0', '16', '16', 'pois-max', '0', '32', '32', 'pois-len', '0', '75', '75', 'ac', NULL, '75', '75', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'bar', '0');
INSERT INTO sets VALUES ('Death''s Disguise', 'Death''s Disguise', '0', 'lifesteal', NULL, '8', '8', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'att%', '0', '40', '40', 'dmg-min', '0', '10', '10', 'res-all', '0', '25', '25', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Angelical Raiment', 'Angelical Raiment', '0', 'dex', NULL, '10', '10', NULL, NULL, NULL, NULL, 'mana', NULL, '50', '50', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', '0', '25', '25', 'half-freeze', '0', '1', '1', 'mag%', '0', '40', '40', 'regen-mana', NULL, '8', '8', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Arctic Gear', 'Arctic Gear', '0', 'str', NULL, '5', '5', NULL, NULL, NULL, NULL, 'hp', NULL, '50', '50', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'dmg-cold/lvl', '16', NULL, NULL, 'cold-len', '0', '100', '100', 'nofreeze', '0', '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'ama', '0');
INSERT INTO sets VALUES ('Arcanna''s Tricks', 'Arcanna''s Tricks', '0', 'mana', NULL, '50', '50', NULL, NULL, NULL, NULL, 'hp', NULL, '50', '50', 'regen-mana', NULL, '12', '12', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'cast3', '0', '20', '20', 'manasteal', '0', '5', '5', 'mana', '0', '25', '25', 'allskills', NULL, '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'sor', '0');
INSERT INTO sets VALUES ('Natalya''s Odium', 'Natalya''s Odium', '100', 'red-mag', NULL, '15', '15', NULL, NULL, NULL, NULL, 'ac', NULL, '200', '200', NULL, NULL, NULL, NULL, 'res-pois', NULL, '20', '20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', NULL, '50', '50', 'ass', NULL, '3', '3', 'ac', NULL, '150', '150', 'lifesteal', NULL, '14', '14', 'manasteal', NULL, '14', '14', 'red-dmg%', NULL, '30', '30', 'fade', NULL, '1', '1', NULL, NULL, NULL, NULL, 'ass', '0');
INSERT INTO sets VALUES ('Aldur''s Watchtower', 'Aldur''s Watchtower', '100', 'att%', NULL, '150', '150', NULL, NULL, NULL, NULL, 'mag%', NULL, '50', '50', NULL, NULL, NULL, NULL, 'lifesteal', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', NULL, '50', '50', 'dru', NULL, '3', '3', 'ac', NULL, '150', '150', 'manasteal', NULL, '10', '10', 'mana', NULL, '150', '150', 'dmg%', NULL, '350', '350', 'state', 'fullsetgeneric', '1', '1', NULL, NULL, NULL, NULL, 'dru', '0');
INSERT INTO sets VALUES ('Immortal King', 'Immortal King', '100', 'att', NULL, '50', '50', NULL, NULL, NULL, NULL, 'att', NULL, '75', '75', NULL, NULL, NULL, NULL, 'att', NULL, '125', '125', NULL, NULL, NULL, NULL, 'att', NULL, '200', '200', NULL, NULL, NULL, NULL, 'res-all', NULL, '50', '50', 'bar', NULL, '3', '3', 'hp', NULL, '150', '150', 'red-mag', NULL, '10', '10', 'state', 'fullsetgeneric', '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'bar', '0');
INSERT INTO sets VALUES ('Tal Rasha''s Wrappings', 'Tal Rasha''s Wrappings', '100', 'regen', NULL, '10', '10', NULL, NULL, NULL, NULL, 'mag%', NULL, '65', '65', NULL, NULL, NULL, NULL, 'balance2', NULL, '25', '25', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', NULL, '50', '50', 'sor', NULL, '3', '3', 'ac', NULL, '150', '150', 'hp', NULL, '150', '150', 'ac-miss', NULL, '50', '50', 'state', 'fullsetgeneric', '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'sor', '0');
INSERT INTO sets VALUES ('Griswold''s Legacy', 'Griswold''s Legacy', '100', 'str', NULL, '20', '20', NULL, NULL, NULL, NULL, 'dex', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', NULL, '50', '50', 'pal', NULL, '3', '3', 'att', NULL, '200', '200', 'hp', NULL, '150', '150', 'state', 'fullsetgeneric', '1', '1', 'balance2', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'pal', '0');
INSERT INTO sets VALUES ('Trang-Oul''s Avatar', 'Trang-Oul''s Avatar', '100', 'regen-mana', NULL, '15', '15', 'oskill', 'Fire Ball', '18', '18', 'regen-mana', NULL, '15', '15', 'oskill', 'Fire Wall', '13', '13', 'regen-mana', NULL, '15', '15', 'oskill', 'Meteor', '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', NULL, '50', '50', 'nec', NULL, '3', '3', 'mana', NULL, '100', '100', 'ac', NULL, '200', '200', 'state', 'monsterset', '1', '1', 'regen-mana', NULL, '15', '15', 'oskill', 'Fire Mastery', '3', '3', 'lifesteal', NULL, '20', '20', 'nec', '0');
INSERT INTO sets VALUES ('M''avina''s Battle Hymn', 'M''avina''s Battle Hymn', '100', 'str', NULL, '20', '20', NULL, NULL, NULL, NULL, 'dex', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', NULL, '50', '50', 'ama', NULL, '3', '3', 'ac', NULL, '100', '100', 'att', NULL, '100', '100', 'mag%', NULL, '100', '100', 'state', 'fullsetgeneric', '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'ama', '0');
INSERT INTO sets VALUES ('The Disciple', 'The Disciple', '100', 'ac', NULL, '150', '150', NULL, NULL, NULL, NULL, 'dmg-pois', '75', '75', '75', NULL, NULL, NULL, NULL, 'str', NULL, '10', '10', NULL, NULL, NULL, NULL, 'dex', NULL, '10', '10', NULL, NULL, NULL, NULL, 'res-all', NULL, '50', '50', 'allskills', NULL, '2', '2', 'mana', NULL, '100', '100', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Heaven''s Brethren', 'Heaven''s Brethren', '100', 'lifesteal', NULL, '10', '10', NULL, NULL, NULL, NULL, 'regen', NULL, '30', '30', 'dmg-fire/lvl', '24', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', NULL, '50', '50', 'allskills', NULL, '2', '2', 'nofreeze', NULL, '1', '1', 'light', NULL, '5', '5', 'red-dmg%', NULL, '24', '24', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Orphan''s Call', 'Orphan''s Call', '100', 'hp', NULL, '35', '35', NULL, NULL, NULL, NULL, 'thorns', NULL, '5', '5', NULL, NULL, NULL, NULL, 'ac', NULL, '100', '100', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'hp', NULL, '50', '50', 'dex', NULL, '10', '10', 'str', NULL, '20', '20', 'ac', NULL, '100', '100', 'res-all', NULL, '15', '15', 'mag%', '0', '80', '80', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Hwanin''s Majesty', 'Hwanin''s Majesty', '100', 'ac', NULL, '100', '100', NULL, NULL, NULL, NULL, 'ac', NULL, '200', '200', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'allskills', NULL, '2', '2', 'lifesteal', NULL, '20', '20', 'move3', NULL, '30', '30', 'res-all', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Sazabi''s Grand Tribute', 'Sazabi''s Grand Tribute', '100', 'move3', NULL, '40', '40', 'res-pois-len', NULL, '75', '75', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', NULL, '30', '30', 'lifesteal', NULL, '15', '15', 'hp%', NULL, '27', '27', 'allskills', NULL, '1', '1', 'red-dmg%', NULL, '16', '16', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Bul-Kathos'' Children', 'Bul-Kathos'' Children', '100', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'dmg-fire', NULL, '200', '200', 'allskills', NULL, '2', '2', 'att', NULL, '200', '200', 'ac', NULL, '200', '200', 'dmg-undead', NULL, '200', '200', 'dmg-demon', NULL, '200', '200', 'lifesteal', NULL, '10', '10', 'deadly', NULL, '20', '20', 'bar', '0');
INSERT INTO sets VALUES ('Cow King''s Leathers', 'Cow King''s Leathers', '100', 'res-pois', NULL, '25', '25', 'ac', NULL, '100', '100', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'stam', NULL, '100', '100', 'str', NULL, '20', '20', 'gold%', NULL, '100', '100', 'mag%', NULL, '100', '100', 'gethit-skill', '42', '25', '5', 'swing3', NULL, '30', '30', 'hp', NULL, '100', '100', 'allskills', NULL, '1', '1', NULL, '0');
INSERT INTO sets VALUES ('Naj''s Ancient Set', 'Naj''s Ancient Set', '100', 'ac', NULL, '175', '175', 'mag%/lvl', '12', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'allskills', NULL, '1', '1', 'regen', NULL, '20', '20', 'dex', NULL, '15', '15', 'res-all', NULL, '50', '50', 'str', NULL, '20', '20', 'mana', NULL, '100', '100', 'fireskill', NULL, '2', '2', 'hp%', NULL, '12', '12', 'sor', '0');
INSERT INTO sets VALUES ('McAuley''s Folly', 'McAuley''s Folly', '100', 'ac', NULL, '50', '50', NULL, NULL, NULL, NULL, 'att', NULL, '75', '75', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'allskills', NULL, '1', '1', 'mag%', NULL, '50', '50', 'mana', NULL, '50', '50', 'lifesteal', NULL, '4', '4', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO sets VALUES ('Warlord''s Glory', 'Warlord''s Glory', '100', 'lifesteal', NULL, '15', '15', NULL, NULL, NULL, NULL, 'ac', NULL, '150', '150', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'allskills', NULL, '1', '1', 'aura', 'Meditation', '10', '10', 'red-dmg', '0', '9', '9', 'res-fire', '0', '15', '15', 'mana', NULL, '20', '20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'war', '0');
INSERT INTO sets VALUES ('Bane''s Garments', 'Bane''s Garments', '100', 'dmg%', NULL, '80', '80', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'allskills', NULL, '1', '1', 'dex', NULL, '15', '15', 'cast1', NULL, '25', '25', 'mag%', NULL, '40', '40', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'war', '0');
INSERT INTO sets VALUES ('Horazon''s Splendor', 'Horazon''s Splendor', '100', 'dex', NULL, '20', '20', NULL, NULL, NULL, NULL, 'enr', NULL, '30', '30', NULL, NULL, NULL, NULL, 'ac', NULL, '300', '300', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', NULL, '50', '50', 'war', NULL, '3', '3', 'dmg%', NULL, '350', '350', 'mag%', NULL, '100', '100', 'oskill', 'Enchant', '30', '30', 'state', 'fullsetgeneric', '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'war', '0');
COMMIT;
