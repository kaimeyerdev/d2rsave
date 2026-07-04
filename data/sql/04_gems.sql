-- Diablo II and Diablo II: Resurrected are trademarks of Blizzard
-- Entertainment. This file contains structured data derived from D2
-- modding text tables (gems.txt) that themselves originate from Blizzard
-- game data. Distributed here with no warranty; you may not have
-- permission to use this file. Consult your local copyright law and/or
-- use at your own risk. See d2rsavegameparser/LICENSE for the upstream
-- notice.
--
-- Committed as the canonical seed for the reference SQLite DB;
-- edit this file directly or replace it from a fresh CASC / mod-
-- txt extract if you need to update the data.

DROP TABLE IF EXISTS gems;
CREATE TABLE gems (
    "name" TEXT,
    "letter" TEXT,
    "transform" TEXT,
    "code" TEXT,
    "weaponmod1code" TEXT,
    "weaponmod1param" TEXT,
    "weaponmod1min" TEXT,
    "weaponmod1max" TEXT,
    "weaponmod2code" TEXT,
    "weaponmod2param" TEXT,
    "weaponmod2min" TEXT,
    "weaponmod2max" TEXT,
    "weaponmod3code" TEXT,
    "weaponmod3param" TEXT,
    "weaponmod3min" TEXT,
    "weaponmod3max" TEXT,
    "helmmod1code" TEXT,
    "helmmod1param" TEXT,
    "helmmod1min" TEXT,
    "helmmod1max" TEXT,
    "helmmod2code" TEXT,
    "helmmod2param" TEXT,
    "helmmod2min" TEXT,
    "helmmod2max" TEXT,
    "helmmod3code" TEXT,
    "helmmod3param" TEXT,
    "helmmod3min" TEXT,
    "helmmod3max" TEXT,
    "shieldmod1code" TEXT,
    "shieldmod1param" TEXT,
    "shieldmod1min" TEXT,
    "shieldmod1max" TEXT,
    "shieldmod2code" TEXT,
    "shieldmod2param" TEXT,
    "shieldmod2min" TEXT,
    "shieldmod2max" TEXT,
    "shieldmod3code" TEXT,
    "shieldmod3param" TEXT,
    "shieldmod3min" TEXT,
    "shieldmod3max" TEXT
);

BEGIN;
INSERT INTO gems VALUES ('Chipped Amethyst', NULL, '18', 'gcv', 'att', '0', '40', '40', NULL, NULL, NULL, NULL, NULL, '0', '0', '0', 'str', '0', '3', '3', NULL, '0', '0', '0', NULL, '0', '0', '0', 'ac', '0', '8', '8', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawed Amethyst', NULL, '18', 'gfv', 'att', '0', '60', '60', NULL, NULL, NULL, NULL, NULL, '0', '0', '0', 'str', '0', '4', '4', NULL, '0', '0', '0', NULL, '0', '0', '0', 'ac', '0', '12', '12', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Amethyst', NULL, '18', 'gsv', 'att', '0', '80', '80', NULL, NULL, NULL, NULL, NULL, '0', '0', '0', 'str', '0', '6', '6', NULL, '0', '0', '0', NULL, '0', '0', '0', 'ac', '0', '18', '18', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawless Amethyst', NULL, '18', 'gzv', 'att', '0', '100', '100', NULL, NULL, NULL, NULL, NULL, '0', '0', '0', 'str', '0', '8', '8', NULL, '0', '0', '0', NULL, '0', '0', '0', 'ac', '0', '24', '24', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Perfect Amethyst', NULL, '17', 'gpv', 'att', '0', '150', '150', NULL, NULL, NULL, NULL, NULL, '0', '0', '0', 'str', '0', '10', '10', NULL, '0', '0', '0', NULL, '0', '0', '0', 'ac', '0', '30', '30', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Chipped Sapphire', NULL, '5', 'gcb', 'cold-min', '0', '1', '1', 'cold-max', '0', '3', '3', 'cold-len', '0', '25', '25', 'mana', '0', '10', '10', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-cold', '0', '12', '12', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawed Sapphire', NULL, '5', 'gfb', 'cold-min', '0', '3', '3', 'cold-max', '0', '5', '5', 'cold-len', '0', '35', '35', 'mana', '0', '17', '17', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-cold', '0', '16', '16', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Sapphire', NULL, '5', 'gsb', 'cold-min', '0', '4', '4', 'cold-max', '0', '7', '7', 'cold-len', '0', '50', '50', 'mana', '0', '24', '24', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-cold', '0', '22', '22', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawless Sapphire', NULL, '5', 'glb', 'cold-min', '0', '6', '6', 'cold-max', '0', '10', '10', 'cold-len', '0', '60', '60', 'mana', '0', '31', '31', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-cold', '0', '28', '28', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Perfect Sapphire', NULL, '6', 'gpb', 'cold-min', '0', '10', '10', 'cold-max', '0', '14', '14', 'cold-len', '0', '75', '75', 'mana', '0', '38', '38', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-cold', '0', '40', '40', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Chipped Emerald', NULL, '11', 'gcg', 'pois-min', '0', '34', '34', 'pois-max', '0', '34', '34', 'pois-len', '0', '75', '75', 'dex', '0', '3', '3', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-pois', '0', '12', '12', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawed Emerald', NULL, '11', 'gfg', 'pois-min', '0', '51', '51', 'pois-max', '0', '51', '51', 'pois-len', '0', '100', '100', 'dex', '0', '4', '4', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-pois', '0', '16', '16', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Emerald', NULL, '11', 'gsg', 'pois-min', '0', '82', '82', 'pois-max', '0', '82', '82', 'pois-len', '0', '125', '125', 'dex', '0', '6', '6', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-pois', '0', '22', '22', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawless Emerald', NULL, '11', 'glg', 'pois-min', '0', '101', '101', 'pois-max', '0', '101', '101', 'pois-len', '0', '152', '152', 'dex', '0', '8', '8', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-pois', '0', '28', '28', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Perfect Emerald', NULL, '12', 'gpg', 'pois-min', '0', '143', '143', 'pois-max', '0', '143', '143', 'pois-len', '0', '179', '179', 'dex', '0', '10', '10', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-pois', '0', '40', '40', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Chipped Ruby', NULL, '8', 'gcr', 'fire-min', '0', '3', '3', 'fire-max', '0', '4', '4', NULL, '0', '0', '0', 'hp', '0', '10', '10', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-fire', '0', '12', '12', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawed Ruby', NULL, '8', 'gfr', 'fire-min', '0', '5', '5', 'fire-max', '0', '8', '8', NULL, '0', '0', '0', 'hp', '0', '17', '17', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-fire', '0', '16', '16', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Ruby', NULL, '8', 'gsr', 'fire-min', '0', '8', '8', 'fire-max', '0', '12', '12', NULL, '0', '0', '0', 'hp', '0', '24', '24', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-fire', '0', '22', '22', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawless Ruby', NULL, '8', 'glr', 'fire-min', '0', '10', '10', 'fire-max', '0', '16', '16', NULL, '0', '0', '0', 'hp', '0', '31', '31', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-fire', '0', '28', '28', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Perfect Ruby', NULL, '9', 'gpr', 'fire-min', '0', '15', '15', 'fire-max', '0', '20', '20', NULL, '0', '0', '0', 'hp', '0', '38', '38', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-fire', '0', '40', '40', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Chipped Diamond', NULL, '1', 'gcw', 'dmg-undead', '0', '28', '28', NULL, NULL, NULL, NULL, NULL, '0', '0', '0', 'att', '0', '20', '20', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-all', '0', '6', '6', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawed Diamond', NULL, '1', 'gfw', 'dmg-undead', '0', '34', '34', NULL, NULL, NULL, NULL, NULL, '0', '0', '0', 'att', '0', '40', '40', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-all', '0', '8', '8', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Diamond', NULL, '1', 'gsw', 'dmg-undead', '0', '44', '44', NULL, NULL, NULL, NULL, NULL, '0', '0', '0', 'att', '0', '60', '60', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-all', '0', '11', '11', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawless Diamond', NULL, '1', 'glw', 'dmg-undead', '0', '54', '54', NULL, NULL, NULL, NULL, NULL, '0', '0', '0', 'att', '0', '80', '80', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-all', '0', '14', '14', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Perfect Diamond', NULL, '0', 'gpw', 'dmg-undead', '0', '68', '68', NULL, NULL, NULL, NULL, NULL, '0', '0', '0', 'att', '0', '100', '100', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-all', '0', '19', '19', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Chipped Topaz', NULL, '14', 'gcy', 'ltng-min', '0', '1', '1', 'ltng-max', '0', '8', '8', NULL, '0', '0', '0', 'mag%', '0', '9', '9', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-ltng', '0', '12', '12', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawed Topaz', NULL, '14', 'gfy', 'ltng-min', '0', '1', '1', 'ltng-max', '0', '14', '14', NULL, '0', '0', '0', 'mag%', '0', '13', '13', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-ltng', '0', '16', '16', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Topaz', NULL, '14', 'gsy', 'ltng-min', '0', '1', '1', 'ltng-max', '0', '22', '22', NULL, '0', '0', '0', 'mag%', '0', '16', '16', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-ltng', '0', '22', '22', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawless Topaz', NULL, '14', 'gly', 'ltng-min', '0', '1', '1', 'ltng-max', '0', '30', '30', NULL, '0', '0', '0', 'mag%', '0', '20', '20', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-ltng', '0', '28', '28', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Perfect Topaz', NULL, '13', 'gpy', 'ltng-min', '0', '1', '1', 'ltng-max', '0', '40', '40', NULL, '0', '0', '0', 'mag%', '0', '24', '24', NULL, '0', '0', '0', NULL, '0', '0', '0', 'res-ltng', '0', '40', '40', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Chipped Skull', NULL, '2', 'skc', 'manasteal', '0', '1', '1', 'lifesteal', '0', '2', '2', NULL, NULL, NULL, NULL, 'regen', '0', '2', '2', 'regen-mana', '0', '8', '8', NULL, '0', '0', '0', 'thorns', '0', '4', '4', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawed Skull', NULL, '2', 'skf', 'manasteal', '0', '2', '2', 'lifesteal', '0', '2', '2', NULL, NULL, NULL, NULL, 'regen', '0', '3', '3', 'regen-mana', '0', '8', '8', NULL, '0', '0', '0', 'thorns', '0', '8', '8', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Skull', NULL, '2', 'sku', 'manasteal', '0', '2', '2', 'lifesteal', '0', '3', '3', NULL, NULL, NULL, NULL, 'regen', '0', '3', '3', 'regen-mana', '0', '12', '12', NULL, '0', '0', '0', 'thorns', '0', '12', '12', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Flawless Skull', NULL, '2', 'skl', 'manasteal', '0', '3', '3', 'lifesteal', '0', '3', '3', NULL, NULL, NULL, NULL, 'regen', '0', '4', '4', 'regen-mana', '0', '12', '12', NULL, '0', '0', '0', 'thorns', '0', '16', '16', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('Perfect Skull', NULL, '3', 'skz', 'manasteal', '0', '3', '3', 'lifesteal', '0', '4', '4', NULL, NULL, NULL, NULL, 'regen', '0', '5', '5', 'regen-mana', '0', '19', '19', NULL, '0', '0', '0', 'thorns', '0', '20', '20', NULL, '0', '0', '0', NULL, '0', '0', '0');
INSERT INTO gems VALUES ('El Rune', 'r01L', '18', 'r01', 'light', NULL, '1', '1', 'att', NULL, '50', '50', NULL, NULL, NULL, NULL, 'light', NULL, '1', '1', 'ac', NULL, '15', '15', NULL, NULL, NULL, NULL, 'light', NULL, '1', '1', 'ac', NULL, '15', '15', NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Eld Rune', 'r02L', '18', 'r02', 'att-undead', NULL, '50', '50', 'dmg-undead', NULL, '75', '75', NULL, NULL, NULL, NULL, 'stamdrain', NULL, '15', '15', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'block', NULL, '7', '7', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Tir Rune', 'r03L', '18', 'r03', 'mana-kill', NULL, '2', '2', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'mana-kill', NULL, '2', '2', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'mana-kill', NULL, '2', '2', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Nef Rune', 'r04L', '18', 'r04', 'knock', NULL, '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'ac-miss', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'ac-miss', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Eth Rune', 'r05L', '18', 'r05', 'reduce-ac', NULL, '25', '25', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'regen-mana', NULL, '15', '15', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'regen-mana', NULL, '15', '15', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Ith Rune', 'r06L', '18', 'r06', 'dmg-max', NULL, '9', '9', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'dmg-to-mana', NULL, '15', '15', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'dmg-to-mana', NULL, '15', '15', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Tal Rune', 'r07L', '18', 'r07', 'dmg-pois', '125', '154', '154', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-pois', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-pois', NULL, '35', '35', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Ral Rune', 'r08L', '18', 'r08', 'dmg-fire', NULL, '5', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-fire', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-fire', NULL, '35', '35', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Ort Rune', 'r09L', '18', 'r09', 'dmg-ltng', NULL, '1', '50', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-ltng', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-ltng', NULL, '35', '35', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Thul Rune', 'r10L', '18', 'r10', 'dmg-cold', '75', '3', '14', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-cold', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-cold', NULL, '35', '35', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Amn Rune', 'r11L', '18', 'r11', 'lifesteal', NULL, '7', '7', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'thorns', NULL, '14', '14', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'thorns', NULL, '14', '14', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Sol Rune', 'r12L', '18', 'r12', 'dmg-min', NULL, '9', '9', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'red-dmg', NULL, '7', '7', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'red-dmg', NULL, '7', '7', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Shael Rune', 'r13L', '18', 'r13', 'swing2', NULL, '20', '20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'balance2', NULL, '20', '20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'block2', NULL, '20', '20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Dol Rune', 'r14L', '18', 'r14', 'howl', NULL, '32', '32', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'regen', NULL, '7', '7', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'regen', NULL, '7', '7', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Hel Rune', 'r15L', '18', 'r15', 'ease', NULL, '-20', '-20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'ease', NULL, '-15', '-15', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'ease', NULL, '-15', '-15', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Io Rune', 'r16L', '18', 'r16', 'vit', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'vit', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'vit', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Lum Rune', 'r17L', '18', 'r17', 'enr', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'enr', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'enr', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Ko Rune', 'r18L', '18', 'r18', 'dex', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'dex', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'dex', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Fal Rune', 'r19L', '18', 'r19', 'str', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'str', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'str', NULL, '10', '10', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Lem Rune', 'r20L', '18', 'r20', 'gold%', NULL, '75', '75', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'gold%', NULL, '50', '50', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'gold%', NULL, '50', '50', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Pul Rune', 'r21L', '18', 'r21', 'att-demon', NULL, '100', '100', 'dmg-demon', NULL, '75', '75', NULL, NULL, NULL, NULL, 'ac%', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'ac%', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Um Rune', 'r22L', '18', 'r22', 'openwounds', NULL, '25', '25', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', NULL, '15', '15', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-all', NULL, '22', '22', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Mal Rune', 'r23L', '18', 'r23', 'noheal', NULL, '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'red-mag', NULL, '7', '7', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'red-mag', NULL, '7', '7', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Ist Rune', 'r24L', '18', 'r24', 'mag%', NULL, '30', '30', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'mag%', NULL, '25', '25', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'mag%', NULL, '25', '25', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Gul Rune', 'r25L', '18', 'r25', 'att%', NULL, '20', '20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-pois-max', NULL, '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-pois-max', NULL, '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Vex Rune', 'r26L', '18', 'r26', 'manasteal', NULL, '7', '7', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-fire-max', NULL, '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-fire-max', NULL, '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Ohm Rune', 'r27L', '18', 'r27', 'dmg%', NULL, '50', '50', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-cold-max', NULL, '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-cold-max', NULL, '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Lo Rune', 'r28L', '18', 'r28', 'deadly', NULL, '20', '20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-ltng-max', NULL, '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'res-ltng-max', NULL, '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Sur Rune', 'r29L', '18', 'r29', 'stupidity', NULL, '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'mana%', NULL, '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'mana', NULL, '50', '50', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Ber Rune', 'r30L', '18', 'r30', 'crush', NULL, '20', '20', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'red-dmg%', NULL, '8', '8', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'red-dmg%', NULL, '8', '8', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Jah Rune', 'r31L', '18', 'r31', 'ignore-ac', NULL, '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'hp%', NULL, '5', '5', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'hp', NULL, '50', '50', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Cham Rune', 'r32L', '18', 'r32', 'freeze', NULL, '3', '3', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'nofreeze', NULL, '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'nofreeze', NULL, '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
INSERT INTO gems VALUES ('Zod Rune', 'r33L', '18', 'r33', 'indestruct', NULL, '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'indestruct', NULL, '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 'indestruct', NULL, '1', '1', NULL, NULL, NULL, NULL, NULL, NULL, NULL, '0');
COMMIT;
