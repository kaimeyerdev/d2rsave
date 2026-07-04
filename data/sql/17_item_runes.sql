-- Diablo II and Diablo II: Resurrected are trademarks of Blizzard
-- Entertainment. This file contains D2R display strings extracted
-- from the game's CASC storage (data/local/lng/strings/item-runes.json)
-- via CascLib. Distributed here with no warranty; you may not have
-- permission to use this file. Consult your local copyright law and/or
-- use at your own risk. See d2rsavegameparser-examples/src/main/
-- resources/COPYRIGHT for the upstream notice from the paladijn examples
-- project.
--
-- Committed as the canonical seed for the reference SQLite DB;
-- regenerate with `cmake --build --target regen-refdb-sql` against a
-- fresh D2R install if the upstream data changes.

DROP TABLE IF EXISTS item_runes;
CREATE TABLE item_runes (
    "key" TEXT PRIMARY KEY,
    "en_us" TEXT,
    "de_de" TEXT,
    "es_es" TEXT,
    "es_mx" TEXT,
    "fr_fr" TEXT,
    "it_it" TEXT,
    "ja_jp" TEXT,
    "ko_kr" TEXT,
    "pl_pl" TEXT,
    "pt_br" TEXT,
    "ru_ru" TEXT,
    "zh_cn" TEXT,
    "zh_tw" TEXT
);

BEGIN;
INSERT OR REPLACE INTO item_runes VALUES ('r16', 'Io Rune', '[fs]Io-Rune', 'Runa Io', 'Runa Io', '[fs]Rune Io', '[fs]Runa Io', 'イオ・ルーン', '이오 룬', 'Runa Io', 'Runa Io', 'Руна Ио', '艾欧符文', '符文：埃歐');
INSERT OR REPLACE INTO item_runes VALUES ('r16L', 'Io', 'Io', 'Io', 'Io', 'Io', 'Io', 'イオ', '이오', 'Io', 'Io', 'Ио', '艾欧', '埃歐');
INSERT OR REPLACE INTO item_runes VALUES ('r13', 'Shael Rune', '[fs]Shael-Rune', 'Runa Shael', 'Runa Shael', '[fs]Rune Shael', '[fs]Runa Shael', 'シャエル・ルーン', '샤엘 룬', 'Runa Shael', 'Runa Shael', 'Руна Шаэль', '沙伊符文', '符文：夏');
INSERT OR REPLACE INTO item_runes VALUES ('r13L', 'Shael', 'Shael', 'Shael', 'Shael', 'Shael', 'Shael', 'シャエル', '샤엘', 'Shael', 'Shael', 'Шаэль', '沙伊', '夏');
INSERT OR REPLACE INTO item_runes VALUES ('r31', 'Jah Rune', '[fs]Jah-Rune', 'Runa Jah', 'Runa Jah', '[fs]Rune Jah', '[fs]Runa Jah', 'ジャー・ルーン', '자 룬', 'Runa Jah', 'Runa Jah', 'Руна Джа', '扎哈符文', '符文：喬');
INSERT OR REPLACE INTO item_runes VALUES ('r31L', 'Jah', 'Jah', 'Jah', 'Jah', 'Jah', 'Jah', 'ジャー', '자', 'Jah', 'Jah', 'Джа', '扎哈', '喬');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword22', 'Delirium', 'Delirium', 'Delirio', 'Delirio', 'Délire', 'Delirio', 'デリリアム', '착란', 'Delirium', 'Delírios', 'Бред', '迷狂', '精神錯亂');
INSERT OR REPLACE INTO item_runes VALUES ('r33', 'Zod Rune', '[fs]Zod-Rune', 'Runa Zod', 'Runa Zod', '[fs]Rune Zod', '[fs]Runa Zod', 'ゾッド・ルーン', '조드 룬', 'Runa Zod', 'Runa Zod', 'Руна Зод', '佐德符文', '符文：薩德');
INSERT OR REPLACE INTO item_runes VALUES ('r32', 'Cham Rune', '[fs]Cham-Rune', 'Runa Cham', 'Runa Cham', '[fs]Rune Cham', '[fs]Runa Cham', 'チャム・ルーン', '참 룬', 'Runa Cham', 'Runa Cham', 'Руна Чам', '查姆符文', '符文：查姆');
INSERT OR REPLACE INTO item_runes VALUES ('r30', 'Ber Rune', '[fs]Ber-Rune', 'Runa Ber', 'Runa Ber', '[fs]Rune Ber', '[fs]Runa Ber', 'バー・ルーン', '베르 룬', 'Runa Ber', 'Runa Ber', 'Руна Бер', '贝符文', '符文：貝');
INSERT OR REPLACE INTO item_runes VALUES ('r29', 'Sur Rune', '[fs]Sur-Rune', 'Runa Sur', 'Runa Sur', '[fs]Rune Sur', '[fs]Runa Sur', 'サー・ルーン', '수르 룬', 'Runa Sur', 'Runa Sur', 'Руна Сур', '瑟符文', '符文：瑟');
INSERT OR REPLACE INTO item_runes VALUES ('r28', 'Lo Rune', '[fs]Lo-Rune', 'Runa Lo', 'Runa Lo', '[fs]Rune Lo', '[fs]Runa Lo', 'ロー・ルーン', '로 룬', 'Runa Lo', 'Runa Lo', 'Руна Ло', '罗符文', '符文：羅');
INSERT OR REPLACE INTO item_runes VALUES ('r27', 'Ohm Rune', '[fs]Ohm-Rune', 'Runa Ohm', 'Runa Ohm', '[fs]Rune Ohm', '[fs]Runa Ohm', 'オーム・ルーン', '오움 룬', 'Runa Ohm', 'Runa Ohm', 'Руна Ом', '欧姆符文', '符文：歐姆');
INSERT OR REPLACE INTO item_runes VALUES ('r26', 'Vex Rune', '[fs]Vex-Rune', 'Runa Vex', 'Runa Vex', '[fs]Rune Vex', '[fs]Runa Vex', 'ヴェックス・ルーン', '벡스 룬', 'Runa Vex', 'Runa Vex', 'Руна Векс', '伐克斯符文', '符文：伐克斯');
INSERT OR REPLACE INTO item_runes VALUES ('r25', 'Gul Rune', '[fs]Gul-Rune', 'Runa Gul', 'Runa Gul', '[fs]Rune Gul', '[fs]Runa Gul', 'ガル・ルーン', '굴 룬', 'Runa Gul', 'Runa Gul', 'Руна Гул', '古尔符文', '符文：古爾');
INSERT OR REPLACE INTO item_runes VALUES ('r24', 'Ist Rune', '[fs]Ist-Rune', 'Runa Ist', 'Runa Ist', '[fs]Rune Ist', '[fs]Runa Ist', 'イスト・ルーン', '이스트 룬', 'Runa Ist', 'Runa Ist', 'Руна Ист', '伊司特符文', '符文：伊司特');
INSERT OR REPLACE INTO item_runes VALUES ('r23', 'Mal Rune', '[fs]Mal-Rune', 'Runa Mal', 'Runa Mal', '[fs]Rune Mal', '[fs]Runa Mal', 'マル・ルーン', '말 룬', 'Runa Mal', 'Runa Mal', 'Руна Мал', '玛尔符文', '符文：馬爾');
INSERT OR REPLACE INTO item_runes VALUES ('r22', 'Um Rune', '[fs]Um-Rune', 'Runa Um', 'Runa Um', '[fs]Rune Um', '[fs]Runa Um', 'アム・ルーン', '우움 룬', 'Runa Um', 'Runa Um', 'Руна Ум', '乌姆符文', '符文：烏姆');
INSERT OR REPLACE INTO item_runes VALUES ('r21', 'Pul Rune', '[fs]Pul-Rune', 'Runa Pul', 'Runa Pul', '[fs]Rune Pul', '[fs]Runa Pul', 'プル・ルーン', '풀 룬', 'Runa Pul', 'Runa Pul', 'Руна Пул', '普尔符文', '符文：普爾');
INSERT OR REPLACE INTO item_runes VALUES ('r20', 'Lem Rune', '[fs]Lem-Rune', 'Runa Lem', 'Runa Lem', '[fs]Rune Lem', '[fs]Runa Lem', 'レム・ルーン', '렘 룬', 'Runa Lem', 'Runa Lem', 'Руна Лем', '兰姆符文', '符文：藍姆');
INSERT OR REPLACE INTO item_runes VALUES ('r19', 'Fal Rune', '[fs]Fal-Rune', 'Runa Fal', 'Runa Fal', '[fs]Rune Fal', '[fs]Runa Fal', 'ファル・ルーン', '팔 룬', 'Runa Fal', 'Runa Fal', 'Руна Фал', '法尔符文', '符文：法爾');
INSERT OR REPLACE INTO item_runes VALUES ('r18', 'Ko Rune', '[fs]Ko-Rune', 'Runa Ko', 'Runa Ko', '[fs]Rune Ko', '[fs]Runa Ko', 'コー・ルーン', '코 룬', 'Runa Ko', 'Runa Ko', 'Руна Ко', '科符文', '符文：科');
INSERT OR REPLACE INTO item_runes VALUES ('r17', 'Lum Rune', '[fs]Lum-Rune', 'Runa Lum', 'Runa Lum', '[fs]Rune Lum', '[fs]Runa Lum', 'ラム・ルーン', '룸 룬', 'Runa Lum', 'Runa Lum', 'Руна Лум', '卢姆符文', '符文：盧姆');
INSERT OR REPLACE INTO item_runes VALUES ('r15', 'Hel Rune', '[fs]Hel-Rune', 'Runa Hel', 'Runa Hel', '[fs]Rune Hel', '[fs]Runa Hel', 'ヘル・ルーン', '헬 룬', 'Runa Hel', 'Runa Hel', 'Руна Хел', '海尔符文', '符文：海爾');
INSERT OR REPLACE INTO item_runes VALUES ('r14', 'Dol Rune', '[fs]Dol-Rune', 'Runa Dol', 'Runa Dol', '[fs]Rune Dol', '[fs]Runa Dol', 'ドル・ルーン', '돌 룬', 'Runa Dol', 'Runa Dol', 'Руна Дол', '多尔符文', '符文：多爾');
INSERT OR REPLACE INTO item_runes VALUES ('r12', 'Sol Rune', '[fs]Sol-Rune', 'Runa Sol', 'Runa Sol', '[fs]Rune Sol', '[fs]Runa Sol', 'ソル・ルーン', '솔 룬', 'Runa Sol', 'Runa Sol', 'Руна Сол', '索尔符文', '符文：索爾');
INSERT OR REPLACE INTO item_runes VALUES ('r11', 'Amn Rune', '[fs]Amn-Rune', 'Runa Amn', 'Runa Amn', '[fs]Rune Amn', '[fs]Runa Amn', 'アムン・ルーン', '앰 룬', 'Runa Amn', 'Runa Amn', 'Руна Амн', '安姆符文', '符文：安姆');
INSERT OR REPLACE INTO item_runes VALUES ('r10', 'Thul Rune', '[fs]Thul-Rune', 'Runa Thul', 'Runa Thul', '[fs]Rune Thul', '[fs]Runa Thul', 'スル・ルーン', '주울 룬', 'Runa Thul', 'Runa Thul', 'Руна Тул', '图尔符文', '符文：書爾');
INSERT OR REPLACE INTO item_runes VALUES ('r09', 'Ort Rune', '[fs]Ort-Rune', 'Runa Ort', 'Runa Ort', '[fs]Rune Ort', '[fs]Runa Ort', 'オルト・ルーン', '오르트 룬', 'Runa Ort', 'Runa Ort', 'Руна Орт', '欧特符文', '符文：歐特');
INSERT OR REPLACE INTO item_runes VALUES ('r08', 'Ral Rune', '[fs]Ral-Rune', 'Runa Ral', 'Runa Ral', '[fs]Rune Ral', '[fs]Runa Ral', 'ラル・ルーン', '랄 룬', 'Runa Ral', 'Runa Ral', 'Руна Рал', '拉尔符文', '符文：拉爾');
INSERT OR REPLACE INTO item_runes VALUES ('r07', 'Tal Rune', '[fs]Tal-Rune', 'Runa Tal', 'Runa Tal', '[fs]Rune Tal', '[fs]Runa Tal', 'タル・ルーン', '탈 룬', 'Runa Tal', 'Runa Tal', 'Руна Тал', '塔尔符文', '符文：塔爾');
INSERT OR REPLACE INTO item_runes VALUES ('r06', 'Ith Rune', '[fs]Ith-Rune', 'Runa Ith', 'Runa Ith', '[fs]Rune Ith', '[fs]Runa Ith', 'イス・ルーン', '아이드 룬', 'Runa Ith', 'Runa Ith', 'Руна Ит', '伊司符文', '符文：伊司');
INSERT OR REPLACE INTO item_runes VALUES ('r05', 'Eth Rune', '[fs]Eth-Rune', 'Runa Eth', 'Runa Eth', '[fs]Rune Eth', '[fs]Runa Eth', 'エス・ルーン', '에드 룬', 'Runa Eth', 'Runa Eth', 'Руна Эт', '艾斯符文', '符文：愛斯');
INSERT OR REPLACE INTO item_runes VALUES ('r04', 'Nef Rune', '[fs]Nef-Rune', 'Runa Nef', 'Runa Nef', '[fs]Rune Nef', '[fs]Runa Nef', 'ネフ・ルーン', '네프 룬', 'Runa Nef', 'Runa Nef', 'Руна Неф', '奈夫符文', '符文：那夫');
INSERT OR REPLACE INTO item_runes VALUES ('r03', 'Tir Rune', '[fs]Tir-Rune', 'Runa Tir', 'Runa Tir', '[fs]Rune Tir', '[fs]Runa Tir', 'ティア・ルーン', '티르 룬', 'Runa Tir', 'Runa Tir', 'Руна Тир', '提尔符文', '符文：特爾');
INSERT OR REPLACE INTO item_runes VALUES ('r02', 'Eld Rune', '[fs]Eld-Rune', 'Runa Eld', 'Runa Eld', '[fs]Rune Eld', '[fs]Runa Eld', 'エルド・ルーン', '엘드 룬', 'Runa Eld', 'Runa Eld', 'Руна Элд', '艾德符文', '符文：艾德');
INSERT OR REPLACE INTO item_runes VALUES ('r01', 'El Rune', '[fs]El-Rune', 'Runa El', 'Runa El', '[fs]Rune El', '[fs]Runa El', 'エル・ルーン', '엘 룬', 'Runa El', 'Runa El', 'Руна Эль', '艾尔符文', '符文：艾爾');
INSERT OR REPLACE INTO item_runes VALUES ('r33L', 'Zod', 'Zod', 'Zod', 'Zod', 'Zod', 'Zod', 'ゾッド', '조드', 'Zod', 'Zod', 'Зод', '佐德', '薩德');
INSERT OR REPLACE INTO item_runes VALUES ('r32L', 'Cham', 'Cham', 'Cham', 'Cham', 'Cham', 'Cham', 'チャム', '참', 'Cham', 'Cham', 'Чам', '查姆', '查姆');
INSERT OR REPLACE INTO item_runes VALUES ('r30L', 'Ber', 'Ber', 'Ber', 'Ber', 'Ber', 'Ber', 'バー', '베르', 'Ber', 'Ber', 'Бер', '贝', '貝');
INSERT OR REPLACE INTO item_runes VALUES ('r29L', 'Sur', 'Sur', 'Sur', 'Sur', 'Sur', 'Sur', 'サー', '수르', 'Sur', 'Sur', 'Сур', '瑟', '瑟');
INSERT OR REPLACE INTO item_runes VALUES ('r28L', 'Lo', 'Lo', 'Lo', 'Lo', 'Lo', 'Lo', 'ロー', '로', 'Lo', 'Lo', 'Ло', '罗', '羅');
INSERT OR REPLACE INTO item_runes VALUES ('r27L', 'Ohm', 'Ohm', 'Ohm', 'Ohm', 'Ohm', 'Ohm', 'オーム', '오움', 'Ohm', 'Ohm', 'Ом', '欧姆', '歐姆');
INSERT OR REPLACE INTO item_runes VALUES ('r26L', 'Vex', 'Vex', 'Vex', 'Vex', 'Vex', 'Vex', 'ヴェックス', '벡스', 'Vex', 'Vex', 'Векс', '伐克斯', '伐克斯');
INSERT OR REPLACE INTO item_runes VALUES ('r25L', 'Gul', 'Gul', 'Gul', 'Gul', 'Gul', 'Gul', 'ガル', '굴', 'Gul', 'Gul', 'Гул', '古尔', '古爾');
INSERT OR REPLACE INTO item_runes VALUES ('r24L', 'Ist', 'Ist', 'Ist', 'Ist', 'Ist', 'Ist', 'イスト', '이스트', 'Ist', 'Ist', 'Ист', '伊司特', '伊司特');
INSERT OR REPLACE INTO item_runes VALUES ('r23L', 'Mal', 'Mal', 'Mal', 'Mal', 'Mal', 'Mal', 'マル', '말', 'Mal', 'Mal', 'Мал', '玛尔', '馬爾');
INSERT OR REPLACE INTO item_runes VALUES ('r22L', 'Um', 'Um', 'Um', 'Um', 'Um', 'Um', 'アム', '우움', 'Um', 'Um', 'Ум', '乌姆', '烏姆');
INSERT OR REPLACE INTO item_runes VALUES ('r21L', 'Pul', 'Pul', 'Pul', 'Pul', 'Pul', 'Pul', 'プル', '풀', 'Pul', 'Pul', 'Пул', '普尔', '普爾');
INSERT OR REPLACE INTO item_runes VALUES ('r20L', 'Lem', 'Lem', 'Lem', 'Lem', 'Lem', 'Lem', 'レム', '렘', 'Lem', 'Lem', 'Лем', '兰姆', '藍姆');
INSERT OR REPLACE INTO item_runes VALUES ('r19L', 'Fal', 'Fal', 'Fal', 'Fal', 'Fal', 'Fal', 'ファル', '팔', 'Fal', 'Fal', 'Фал', '法尔', '法爾');
INSERT OR REPLACE INTO item_runes VALUES ('r18L', 'Ko', 'Ko', 'Ko', 'Ko', 'Ko', 'Ko', 'コー', '코', 'Ko', 'Ko', 'Ко', '科', '科');
INSERT OR REPLACE INTO item_runes VALUES ('r17L', 'Lum', 'Lum', 'Lum', 'Lum', 'Lum', 'Lum', 'ラム', '룸', 'Lum', 'Lum', 'Лум', '卢姆', '盧姆');
INSERT OR REPLACE INTO item_runes VALUES ('r15L', 'Hel', 'Hel', 'Hel', 'Hel', 'Hel', 'Hel', 'ヘル', '헬', 'Hel', 'Hel', 'Хел', '海尔', '海爾');
INSERT OR REPLACE INTO item_runes VALUES ('r14L', 'Dol', 'Dol', 'Dol', 'Dol', 'Dol', 'Dol', 'ドル', '돌', 'Dol', 'Dol', 'Дол', '多尔', '多爾');
INSERT OR REPLACE INTO item_runes VALUES ('r12L', 'Sol', 'Sol', 'Sol', 'Sol', 'Sol', 'Sol', 'ソル', '솔', 'Sol', 'Sol', 'Сол', '索尔', '索爾');
INSERT OR REPLACE INTO item_runes VALUES ('r11L', 'Amn', 'Amn', 'Amn', 'Amn', 'Amn', 'Amn', 'アムン', '앰', 'Amn', 'Amn', 'Амн', '安姆', '安姆');
INSERT OR REPLACE INTO item_runes VALUES ('r10L', 'Thul', 'Thul', 'Thul', 'Thul', 'Thul', 'Thul', 'スル', '주울', 'Thul', 'Thul', 'Тул', '图尔', '書爾');
INSERT OR REPLACE INTO item_runes VALUES ('r09L', 'Ort', 'Ort', 'Ort', 'Ort', 'Ort', 'Ort', 'オルト', '오르트', 'Ort', 'Ort', 'Орт', '欧特', '歐特');
INSERT OR REPLACE INTO item_runes VALUES ('r08L', 'Ral', 'Ral', 'Ral', 'Ral', 'Ral', 'Ral', 'ラル', '랄', 'Ral', 'Ral', 'Рал', '拉尔', '拉爾');
INSERT OR REPLACE INTO item_runes VALUES ('r07L', 'Tal', 'Tal', 'Tal', 'Tal', 'Tal', 'Tal', 'タル', '탈', 'Tal', 'Tal', 'Тал', '塔尔', '塔爾');
INSERT OR REPLACE INTO item_runes VALUES ('r06L', 'Ith', 'Ith', 'Ith', 'Ith', 'Ith', 'Ith', 'イス', '아이드', 'Ith', 'Ith', 'Ит', '伊司', '伊司');
INSERT OR REPLACE INTO item_runes VALUES ('r05L', 'Eth', 'Eth', 'Eth', 'Eth', 'Eth', 'Eth', 'エス', '에드', 'Eth', 'Eth', 'Эт', '艾斯', '愛斯');
INSERT OR REPLACE INTO item_runes VALUES ('r04L', 'Nef', 'Nef', 'Nef', 'Nef', 'Nef', 'Nef', 'ネフ', '네프', 'Nef', 'Nef', 'Неф', '奈夫', '那夫');
INSERT OR REPLACE INTO item_runes VALUES ('r03L', 'Tir', 'Tir', 'Tir', 'Tir', 'Tir', 'Tir', 'ティア', '티르', 'Tir', 'Tir', 'Тир', '提尔', '特爾');
INSERT OR REPLACE INTO item_runes VALUES ('r02L', 'Eld', 'Eld', 'Eld', 'Eld', 'Eld', 'Eld', 'エルド', '엘드', 'Eld', 'Eld', 'Элд', '艾德', '艾德');
INSERT OR REPLACE INTO item_runes VALUES ('r01L', 'El', 'El', 'El', 'El', 'El', 'El', 'エル', '엘', 'El', 'El', 'Эль', '艾尔', '艾爾');
INSERT OR REPLACE INTO item_runes VALUES ('RuneQuote', '''', '''', '''', '''', NULL, '''', '''', '''', '''', '"', '''', '''', '''');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword1', 'Ancients'' Pledge', 'Schwur der Urahnen', 'Promesa de los Antiguos', 'Juramento de los ancestros', 'Anathème des Anciens', 'Pegno degli Antichi', '古の民の誓約', '고대인의 서약', 'Przysięga Starożytnych', 'Promessa dos Ancestrais', 'Клятва Древних', '先祖之誓', '先祖之契');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword2', 'Armageddon', 'Armageddon', 'Armagedón', 'Armagedón', 'Armageddon', 'Armageddon', '天変地異', '대전쟁', 'Armagedon', 'Armagedom', 'Армагеддон', '末日', '毀天滅地');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword3', 'Authority', 'Macht', 'Autoridad', 'Autoridad', 'Autorité', 'Autorità', '権威', '권위', 'Władza', 'Autoridade', 'Власть', '权威', '權威');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword4', 'Beast', 'Bestie', 'Bestia', 'Bestia', 'Bête', 'Bestia', '獣', '야수', 'Bestia', 'Fera', 'Зверь', '野兽', '野獸');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword5', 'Beauty', 'Schönheit', 'Belleza', 'Belleza', 'Beauté', 'Bellezza', '美', '아름다움', 'Piękno', 'Beleza', 'Красота', '美丽', '美貌');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword6', 'Black', 'Schwärze', 'Negrura', 'Opaco', 'Noir', 'Nero', '黒', '어둠', 'Czerń', 'Ébano', 'Мрак', '黑色', '黑錘');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword7', 'Blood', 'Blut', 'Sangre', 'Sangre', 'Sang', 'Sangue', '血', '피', 'Krew', 'Sangue', 'Кровь', '精华', '鮮血');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword8', 'Bone', 'Knochen', 'Hueso', 'Hueso', 'Os', 'Ossa', '骨', '뼈', 'Kość', 'Osso', 'Кость', '白骨', '骸骨');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword9', 'Bramble', 'Dornen', 'Zarza', 'Zarza', 'Barbelé', 'Roveto', '茨', '찔레', 'Kolec', 'Espinho', 'Терновник', '荆棘', '刺藤');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword10', 'Brand', 'Brand', 'Tizón', 'Marca', 'Balafre', 'Marchio', '烙印', '낙인', 'Piętno', 'Marca', 'Тавро', '烙印', '烙印');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword11', 'Breath of the Dying', 'Odem der Sterbenden', 'Aliento de los muertos', 'Aliento de los moribundos', 'Souffle des mourants', 'Ultimo Respiro', '死の吐息', '죽어가는 자의 숨결', 'Ostatni Oddech', 'Sopro da Morbidez', 'Вздох умирающего', '濒死之息', '死亡呼吸');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword12', 'Broken Promise', 'Gebrochenes Versprechen', 'Promesa rota', 'Promesa rota', 'Promesse rompue', 'Promessa Spezzata', '背約', '깨어진 약속', 'Złamana Obietnica', 'Promessa Quebrada', 'Нарушенный обет', '破碎誓言', '破碎誓言');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword13', 'Call to Arms', 'Ruf zu den Waffen', 'Llamada a las armas', 'Llamado a las armas', 'Cri de ralliement', 'Chiamata alle Armi', '召集', '소집', 'Wezwanie do Broni', 'Chamado às Armas', 'Призыв к оружию', '战争召唤', '戰爭召喚');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword14', 'Chains of Honor', 'Ketten der Ehre', 'Cadenas de honor', 'Cadenas de honor', 'Chaînes de l’honneur', 'Catene dell''Onore', '名誉の連鎖', '명예의 굴레', 'Okowy Honoru', 'Correntes da Honra', 'Цепи чести', '荣耀之链', '榮耀之鍊');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword15', 'Chance', 'Zufall', 'Casualidad', 'Azar', 'Chance', 'Probabilità', '好機', '기회', 'Szansa', 'Chance', 'Шанс', '机遇', '機會');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword16', 'Chaos', 'Chaos', 'Caos', 'Caos', 'Chaos', 'Caos', '混沌', '혼돈', 'Chaos', 'Caos', 'Хаос', '混沌', '混沌');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword17', 'Crescent Moon', 'Mondsichel', 'Luna creciente', 'Luna creciente', 'Croissant de lune', 'Falce di Luna', '三日月', '초승달', 'Sierp Księżyca', 'Lua Crescente', 'Полумесяц', '新月', '新月');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword18', 'Darkness', 'Dunkelheit', 'Oscuridad', 'Oscuridad', 'Ténèbres', 'Oscurità', '暗闇', '암흑', 'Ciemność', 'Escuridão', 'Тьма', '黑暗', '黑暗');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword19', 'Daylight', 'Tageslicht', 'Luz', 'Luz del día', 'Divine lumière', 'Luce del Giorno', '夜明け', '햇빛', 'Światło Dnia', 'Luz do Dia', 'Ясный день', '白昼', '白晝');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword20', 'Death', 'Tod', 'Muerte', 'Muerte', 'Décès', 'Morte', '死', '죽음', 'Śmierć', 'Morte', 'Смерть', '殒灭', '死神');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword21', 'Deception', 'Täuschung', 'Engaño', 'Decepción', 'Duperie', 'Inganno', '欺瞞', '기만', 'Podstęp', 'Engano', 'Обман', '欺骗', '欺暪');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword23', 'Desire', 'Begierde', 'Deseo', 'Deseo', 'Désir', 'Desiderio', '欲望', '욕망', 'Pożądanie', 'Desejo', 'Желание', '欲望', '渴望');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword24', 'Despair', 'Verzweiflung', 'Desesperación', 'Desesperación', 'Désespoir', 'Disperazione', '絶望', '절망', 'Rozpacz', 'Desespero', 'Отчаяние', '绝望', '絕望');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword25', 'Destruction', 'Zerstörung', 'Destrucción', 'Destrucción', 'Destruction', 'Distruzione', '破壊', '파괴', 'Zniszczenie', 'Destruição', 'Разрушение', '毁灭', '毀滅');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword26', 'Doom', 'Verdammnis', 'Fatalidad', 'Calamidad', 'Dévastation', 'Rovina', '破滅', '파멸', 'Zagłada', 'Perdição', 'Гибель', '厄运', '末日');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword27', 'Dragon', 'Drachen', 'Dragón', 'Dragón', 'Dragon', 'Drago', 'ドラゴン', '용', 'Smok', 'Dragão', 'Дракон', '巨龙', '飛龍');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword28', 'Dread', 'Schrecken', 'Pavor', 'Temor', 'Défiance', 'Terrore', '戦慄', '두려움', 'Strach', 'Pavor', 'Ужас', '惊惧', '恐懼');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword29', 'Dream', 'Traum', 'Sueño', 'Sueño', 'Rêve', 'Sogno', '夢', '꿈', 'Sen', 'Sonho', 'Видение', '梦境', '夢境');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword30', 'Duress', 'Nötigung', 'Coacción', 'Coerción', 'Domination', 'Coercizione', '威圧', '협박', 'Przymus', 'Coerção', 'Принуждение', '强压', '強制');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword31', 'Edge', 'Schneide', 'Filo', 'Filo', 'Arête', 'Lama', '刃', '모서리', 'Ostrze', 'Gume', 'Острие', '锐锋', '邊緣');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword32', 'Elation', 'Erregung', 'Júbilo', 'Euforia', 'Effusion', 'Esultanza', '高揚', '득의', 'Radość', 'Euforia', 'Воодушевление', '欢欣', '興高彩烈');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword33', 'Enigma', 'Rätsel', 'Enigma', 'Enigma', 'Énigme', 'Enigma', '不知', '수수께끼', 'Enigma', 'Enigma', 'Тайна', '谜团', '謎團');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword34', 'Enlightenment', 'Erleuchtung', 'Ilustración', 'Iluminación', 'Éclaircissement', 'Illuminazione', '悟り', '깨우침', 'Oświecenie', 'Iluminação', 'Просветление', '启迪', '教化');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword35', 'Envy', 'Neid', 'Envidia', 'Envidia', 'Envie', 'Invidia', '羨望', '시기', 'Zawiść', 'Inveja', 'Зависть', '羡慕', '羨慕');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword36', 'Eternity', 'Ewigkeit', 'Eternidad', 'Eternidad', 'Éternité', 'Eternità', '永遠', '영원', 'Wieczność', 'Eternidade', 'Вечность', '永恒', '永恆');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword37', 'Exile', 'Exil', 'Exilio', 'Exilio', 'Exil', 'Esilio', '追放', '추방', 'Wygnanie', 'Exílio', 'Изгнание', '流放', '流亡');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword38', 'Faith', 'Glaube', 'Fe', 'Fe', 'Foi', 'Fede', '信念', '신념', 'Wiara', 'Fé', 'Вера', '信念', '信心');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword39', 'Famine', 'Hungersnot', 'Hambruna', 'Hambruna', 'Famine', 'Carestia', '飢饉', '기근', 'Wygłodzenie', 'Penúria', 'Голод', '饥荒', '饑荒');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword40', 'Flickering Flame', 'Flamme', 'Llama trémula', 'Llama parpadeante', 'Flamme vacillante', 'Fiamma', '揺らめく炎', '꺼져가는 불길', 'Płomień', 'Chama Tremeluzente', 'Мерцающее пламя', '闪烁火焰', '閃爍火焰');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword41', 'Fortitude', 'Stärke', 'Fortaleza', 'Fortaleza', 'Fortitude', 'Fermezza', '不屈', '인내', 'Hart', 'Fortitude', 'Сила духа', '刚毅', '剛毅');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword42', 'Fortune', 'Glück', 'Fortuna', 'Fortuna', 'Fortune', 'Fortuna', '幸運', '행운', 'Fortuna', 'Fortuna', 'Удача', '鸿运', '機運');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword43', 'Friendship', 'Freundschaft', 'Amistad', 'Amistad', 'Fraternité', 'Amicizia', '友情', '우정', 'Przyjaźń', 'Amizade', 'Дружба', '友谊', '友情');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword44', 'Fury', 'Wut', 'Furia', 'Furia', 'Fureur', 'Furia', '激情', '분노', 'Furia', 'Fúria', 'Ярость', '愤怒', '狂怒');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword45', 'Gloom', 'Düsternis', 'Melancolía', 'Penumbra', 'Funérailles', 'Tenebre', '陰鬱', '어스름', 'Mrok', 'Melancolia', 'Сумрак', '阴霾', '幽暗');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword46', 'Glory', 'Ruhm', 'Gloria', 'Gloria', 'Gloire', 'Gloria', '栄光', '영예', 'Chwała', 'Glória', 'Слава', '光华', '榮譽');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword47', 'Grief', 'Trauer', 'Dolor', 'Pena', 'Grand chagrin', 'Cordoglio', '嘆き', '슬픔', 'Żal', 'Luto', 'Печаль', '悔恨', '悔恨');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword48', 'Hand of Justice', 'Hand der Gerechtigkeit', 'Mano justiciera', 'Mano de la justicia', 'Glaive de la justice', 'Mano della Giustizia', '正義の手', '정의의 손길', 'Ręka Sprawiedliwości', 'Mão da Justiça', 'Длань правосудия', '正义之手', '正義之手');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword49', 'Harmony', 'Harmonie', 'Armonía', 'Armonía', 'Harmonie', 'Armonia', '調和', '조화', 'Harmonia', 'Harmonia', 'Гармония', '和谐', '和諧');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword50', 'Hatred', 'Hass', 'Odio', 'Odio', 'Haine', 'Odio', '憎悪', '증오', 'Nienawiść', 'Ódio', 'Ненависть', '憎恨', '憎恨');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword51', 'Heart of the Oak', 'Herz der Eiche', 'Corazón de roble', 'Corazón de roble', 'Haut chêne', 'Cuore della Quercia', '神木の心', '참나무의 심장', 'Serce Dębu', 'Coração do Carvalho', 'Сердце дуба', '橡树之心', '橡樹之心');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword52', 'Heaven''s Will', 'Himmlischer Wille', 'Voluntad del cielo', 'Voluntad del cielo', 'Volonté des Cieux', 'Volontà Celeste', '天命', '천상의 의지', 'Wola Niebios', 'Vontade do Paraíso', 'Воля небес', '天堂意志', '天堂意志');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword53', 'Holy Tears', 'Heilige Tränen', 'Lágrimas sagradas', 'Lágrimas sagradas', 'Larmes sacrées', 'Sacre Lacrime', '聖なる涙', '신성한 눈물', 'Święta Łza', 'Lágrimas Sagradas', 'Слезы святого', '神圣之泪', '神聖之淚');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword54', 'Holy Thunder', 'Heiliger Donner', 'Trueno sagrado', 'Trueno sagrado', 'Tonnerre sacré', 'Sacro Tuono', '神の雷', '신성한 천둥', 'Święty Grom', 'Trovão Sagrado', 'Божественный гром', '圣雷', '神聖雷擊');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword55', 'Honor', 'Ehre', 'Honor', 'Honor', 'Honneur', 'Onore', '名誉', '명예', 'Honor', 'Honra', 'Честь', '荣耀', '榮耀');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword56', 'Revenge', 'Rache', 'Revancha', 'Represalia', 'Revanche', 'Rivincita', '復讐', '보복', 'Mściwość', 'Revanche', 'Возмездие', '报复', '報復');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword57', 'Humility', 'Demut', 'Humildad', 'Humildad', 'Humilité', 'Umiltà', '謙虚', '겸손', 'Pokora', 'Humildade', 'Смирение', '谦逊', '謙恭');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword58', 'Hunger', 'Hunger', 'Hambre', 'Hambre', 'Faim', 'Fame', '飢え', '굶주림', 'Głód', 'Fome', 'Голод', '饥饿', '飢餓');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword59', 'Ice', 'Eis', 'Hielo', 'Hielo', 'Glace', 'Ghiaccio', '氷', '얼음', 'Lód', 'Gelo', 'Лед', '寒冰', '寒冰');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword60', 'Infinity', 'Unendlichkeit', 'Infinito', 'Infinito', 'Infinité', 'Infinito', '無限', '무한', 'Nieskończoność', 'Infinito', 'Бесконечность', '无限', '無限');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword61', 'Innocence', 'Unschuld', 'Inocencia', 'Inocencia', 'Innocence', 'Innocenza', '無垢', '결백', 'Niewinność', 'Inocência', 'Невинность', '无辜', '純真');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword62', 'Insight', 'Einsicht', 'Perspicacia', 'Percepción', 'Spiritualité', 'Intuito', '洞察', '통찰', 'Olśnienie', 'Intuição', 'Прозрение', '眼光', '靈光');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword63', 'Jealousy', 'Eifersucht', 'Celos', 'Celos', 'Jalousie', 'Gelosia', '嫉妬', '질투', 'Zazdrość', 'Ciúme', 'Ревность', '嫉妒', '妒忌');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword64', 'Judgement', 'Urteil', 'Sentencia', 'Juicio', 'Jugement', 'Giudizio', '審判', '심판', 'Osąd', 'Julgamento', 'Правосудие', '审判', '審判');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword65', 'King''s Grace', 'Königliche Gnade', 'Gracia del rey', 'Gracia del rey', 'Grâce du roi', 'Grazia del Re', '王の赦し', '왕의 은총', 'Królewska Łaska', 'Graça Régia', 'Милость короля', '王恩', '王者的慈悲');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword66', 'Kingslayer', 'Königsmord', 'Regicida', 'Matarreyes', 'Régicide', 'Regicida', '王殺し', '왕 시해자', 'Zabójca Królów', 'Regicida', 'Цареубийца', '弑君者', '弒王者');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword67', 'Knight''s Vigil', 'Ritters Wacht', 'Vela del caballero', 'Vigilia del caballero', 'Veille du chevalier', 'Veglia del Cavaliere', '騎士の見張り', '기사의 경계', 'Rycerska Warta', 'Vigília do Cavaleiro', 'Бдение рыцаря', '骑士之印', '騎士守望');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword68', 'Knowledge', 'Wissen', 'Saber', 'Conocimiento', 'Connaissance', 'Conoscenza', '知識', '지식', 'Poznanie', 'Conhecimento', 'Знание', '知识', '學識');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword69', 'Last Wish', 'Letzter Wunsch', 'Último deseo', 'Último deseo', 'Dernière volonté', 'Ultimo Desiderio', '最後の願い', '마지막 소원', 'Ostatnie Życzenie', 'Último Desejo', 'Последняя воля', '临终之愿', '最後遺願');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword70', 'Law', 'Gesetz', 'Ley', 'Ley', 'Loi', 'Legge', '法', '법률', 'Prawo', 'Lei', 'Закон', '律法', '律法');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword71', 'Lawbringer', 'Gesetzesbringer', 'Jurista', 'Justiciero', 'Législateur', 'Legiferatore', '法の番人', '집행자', 'Prawodawca', 'Justiceiro', 'Судия', '执法者', '執法者');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword72', 'Leaf', 'Blatt', 'Hoja', 'Hoja', 'Feuille', 'Foglia', '葉', '잎새', 'Liść', 'Folha', 'Лист', '叶子', '葉子');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword73', 'Lightning', 'Blitzschlag', 'Rayo', 'Rayo', 'Foudre', 'Fulmine', '稲妻', '번개', 'Błyskawica', 'Eletricidade', 'Молния', '闪电', '閃電');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword74', 'Lionheart', 'Löwenherz', 'Corazón de león', 'Corazón de león', 'Cœur-de-lion', 'Cuor di Leone', '勇猛', '용맹', 'Lwie Serce', 'Coração de Leão', 'Львиное сердце', '狮心', '獅子心');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword75', 'Lore', 'Überlieferung', 'Acervo', 'Historia', 'Savoir', 'Dottrina', '伝承', '전승', 'Wiedza', 'Legado', 'История', '学识', '知識');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword76', 'Love', 'Liebe', 'Amor', 'Amor', 'Lien amoureux', 'Amore', '愛', '사랑', 'Miłość', 'Amor', 'Любовь', '爱情', '愛情');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword77', 'Loyalty', 'Treue', 'Lealtad', 'Lealtad', 'Loyauté', 'Fedeltà', '忠義', '충심', 'Lojalność', 'Lealdade', 'Преданность', '忠诚', '忠誠');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword78', 'Lust', 'Lust', 'Lujuria', 'Lujuria', 'Luxure', 'Lussuria', '熱望', '욕정', 'Żądza', 'Luxúria', 'Похоть', '欲望', '慾望');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword79', 'Madness', 'Wahnsinn', 'Locura', 'Locura', 'Aliénation', 'Follia', '狂気', '광기', 'Szaleństwo', 'Loucura', 'Безумие', '疯狂', '瘋狂');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword81', 'Malice', 'Boshaftigkeit', 'Maldad', 'Malicia', 'Méchanceté', 'Malizia', '恨み', '악의', 'Złośliwość', 'Malícia', 'Злоба', '怨恨', '怨恨');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword82', 'Melody', 'Melodie', 'Melodía', 'Melodía', 'Mélodie', 'Melodia', '旋律', '선율', 'Melodia', 'Melodia', 'Мелодия', '旋律', '旋律');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword83', 'Memory', 'Erinnerung', 'Memoria', 'Memoria', 'Mémoire', 'Memoria', '記憶', '기억', 'Pamięć', 'Memória', 'Память', '回忆', '記憶');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword84', 'Mist', 'Nebel', 'Bruma', 'Bruma', 'Brume', 'Bruma', '霧', '안개', 'Mgła', 'Névoa', 'Туман', '迷雾', '迷霧');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword85', 'Morning', 'Morgen', 'Mañana', 'Mañana', 'Matin', 'Mattino', '朝', '아침', 'Poranek', 'Manhã', 'Утро', '清晨', '清晨');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword86', 'Mystery', 'Geheimnis', 'Misterio', 'Misterio', 'Mystère', 'Mistero', '神秘', '신비', 'Tajemnica', 'Mistério', 'Загадка', '谜题', '神秘');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword87', 'Myth', 'Mythos', 'Mito', 'Mito', 'Mythe', 'Mito', '神話', '신화', 'Mit', 'Mito', 'Миф', '神话', '神話');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword88', 'Nadir', 'Nadir', 'Nadir', 'Nadir', 'Nadir', 'Nadir', '奈落', '구렁텅이', 'Nadir', 'Nadir', 'Упадок', '天底', '天底');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword89', 'Nature''s Kingdom', 'Königreich der Natur', 'Reino de la naturaleza', 'Reino de la naturaleza', 'Nature', 'Regno della Natura', '自然の王国', '자연의 왕국', 'Królestwo Natury', 'Reino da Natureza', 'Царство природы', '自然王国', '自然王國');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword90', 'Night', 'Nacht', 'Noche', 'Noche', 'Nuit', 'Notte', '夜', '밤', 'Noc', 'Noite', 'Ночь', '黑夜', '夜晚');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword91', 'Oath', 'Eid', 'Juramento', 'Juramento', 'Serment', 'Giuramento', '誓い', '서약', 'Przysięga', 'Juramento', 'Клятва', '誓言', '誓約');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword92', 'Obedience', 'Gehorsam', 'Obediencia', 'Obediencia', 'Obéissance ', 'Obbedienza', '服従', '순종', 'Posłuszeństwo', 'Obediência', 'Послушание', '顺从', '遵從');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword93', 'Oblivion', 'Nichts', 'Olvido', 'Olvido', 'Oubli', 'Oblio', '忘却', '망각', 'Zapomnienie', 'Esquecimento', 'Забвение', '湮灭', '湮沒');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword94', 'Obsession', 'Besessenheit', 'Obsesión', 'Obsesión', 'Obsession', 'Ossessione', '執着', '집착', 'Obsesja', 'Obsessão', 'Помешательство', '执着', '執念');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword95', 'Passion', 'Leidenschaft', 'Pasión', 'Pasión', 'Passion', 'Passione', '情熱', '열정', 'Pasja', 'Paixão', 'Страсть', '热情', '熱情');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword96', 'Patience', 'Geduld', 'Paciencia', 'Paciencia', 'Patience', 'Pazienza', '忍耐', '끈기', 'Cierpliwość', 'Paciência', 'Терпение', '耐心', '耐心');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword97', 'Pattern', 'Prasseln', 'Patrón', 'Patrón', 'Patron', 'Trama', '模範', '귀감', 'Wzorzec', 'Padrão', 'Узор', '典范', '圖紋');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword98', 'Peace', 'Frieden', 'Paz', 'Paz', 'Paix', 'Pace', '平和', '평화', 'Pokój', 'Paz', 'Мир', '平和', '和平');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword99', 'Voice of Reason', 'Stimme der Vernunft', 'Voz de la razón', 'Voz de la razón', 'Voix de la raison', 'Voce della Ragione', '理知の声', '이성의 목소리', 'Głos Rozsądku', 'Voz da Razão', 'Голос разума', '理智之声', '理性之聲');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword100', 'Penitence', 'Reue', 'Penitencia', 'Penitencia', 'Pénitence', 'Penitenza', '懺悔', '참회', 'Skrucha', 'Penitência', 'Покаяние', '忏悔', '懺悔');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword101', 'Peril', 'Gefahr', 'Peligro', 'Riesgo', 'Péril', 'Pericolo', '危機', '위기', 'Niebezpieczeństwo', 'Perigo', 'Опасность', '灾劫', '危害');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword102', 'Pestilence', 'Pestilenz', 'Pestilencia', 'Pestilencia', 'Pestilence', 'Pestilenza', '悪疫', '질병', 'Zaraza', 'Pestilência', 'Мор', '疾病', '疫病');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword103', 'Phoenix', 'Phönix', 'Fénix', 'Fénix', 'Phénix', 'Fenice', '不死鳥', '불사조', 'Feniks', 'Fênix', 'Феникс', '凤凰', '鳳凰');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword104', 'Piety', 'Frömmigkeit', 'Piedad', 'Piedad', 'Piété', 'Pietà', '敬神', '경건', 'Pobożność', 'Piedade', 'Благочестие', '虔敬', '虔誠');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword105', 'Pillar of Faith', 'Säule des Glaubens', 'Pilar de fe', 'Pilar de fe', 'Pilier de la foi', 'Pilastro della Fede', '信仰の柱', '신념의 기둥', 'Filar Wiary', 'Pilar da Fé', 'Столп веры', '信念之柱', '信念之柱');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword106', 'Plague', 'Pest', 'Peste', 'Peste', 'Peste', 'Contagio', '災厄', '역병', 'Plaga', 'Peste', 'Чума', '瘟疫', '瘟疫');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword107', 'Praise', 'Lobpreisung', 'Alabanza', 'Loa', 'Louange', 'Lode', '賛美', '칭송', 'Pochwała', 'Louvor', 'Похвала', '赞美', '頌揚');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword108', 'Prayer', 'Gebet', 'Oración', 'Plegaria', 'Prière', 'Preghiera', '祈願', '기도', 'Modlitwa', 'Prece', 'Молитва', '祈祷', '祈禱');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword109', 'Pride', 'Stolz', 'Orgullo', 'Orgullo', 'Fierté', 'Superbia', '誇り', '긍지', 'Duma', 'Orgulho', 'Гордость', '骄傲', '驕傲');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword110', 'Principle', 'Prinzip', 'Principio', 'Principio', 'Principe', 'Principio', '原理', '원칙', 'Zasada', 'Princípio', 'Убеждение', '信条', '原則');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword111', 'Prowess in Battle', 'Tapferkeit im Kampf', 'Habilidad en combate', 'Destreza en combate', 'Prouesse', 'Prodezza', '武勇', '전장의 기량', 'Sprawność Bojowa', 'Perícia em Batalha', 'Доблесть', '战力', '勇戰之力');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword112', 'Prudence', 'Besonnenheit', 'Prudencia', 'Prudencia', 'Prudence', 'Prudenza', '分別', '신중', 'Rozwaga', 'Prudência', 'Благоразумие', '谨慎', '謹慎');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword113', 'Punishment', 'Strafe', 'Castigo', 'Castigo', 'Punition', 'Punizione', '罰', '징벌', 'Kara', 'Punição', 'Наказание', '惩罚', '懲罰');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword114', 'Purity', 'Reinheit', 'Pureza', 'Pureza', 'Pureté', 'Purezza', '純粋', '순수', 'Czystość', 'Pureza', 'Чистота', '纯洁', '純潔');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword115', 'Question', 'Frage', 'Duda', 'Pregunta', 'Question', 'Quesito', '疑問', '의문', 'Pytanie', 'Questão', 'Вопрос', '质问', '疑問');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword116', 'Radiance', 'Glanz', 'Resplandor', 'Resplandor', 'Rayon', 'Fulgore', '輝き', '광휘', 'Blask', 'Luminosidade', 'Сияние', '光辉', '光輝');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword117', 'Rain', 'Regen', 'Lluvia', 'Lluvia', 'Pluie', 'Pioggia', '雨', '비', 'Deszcz', 'Chuva', 'Ливень', '暴雨', '降雨');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword118', 'Reason', 'Vernunft', 'Razón', 'Razón', 'Raison', 'Ragione', '理性', '이성', 'Rozsądek', 'Razão', 'Разум', '理性', '理由');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword119', 'Red', 'Röte', 'Rojez', 'Rojo', 'Rouge', 'Rosso', '赤', '핏빛', 'Czerwień', 'Vermelho', 'Багрянец', '绯红', '鮮紅');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword120', 'Rhyme', 'Reim', 'Rima', 'Rima', 'Rime', 'Rima', '韻', '각운', 'Rym', 'Rima', 'Рифма', '韵律', '聲韻');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword121', 'Rift', 'Zerrissenheit', 'Grieta', 'Grieta', 'Rupture', 'Varco', '不和', '균열', 'Szczelina', 'Fenda', 'Разлом', '裂隙', '裂隙');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword122', 'Sanctuary', 'Zuflucht', 'Santuario', 'Santuario', 'Sanctuaire', 'Santuario', '聖域', '성역', 'Sanktuarium', 'Santuário', 'Убежище', '庇护', '聖堂');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword123', 'Serendipity', 'Glückseligkeit', 'Serendipia', 'Serendipia', 'Veine', 'Serendipità', '僥倖', '우연한 행운', 'Łut Szczęścia', 'Acaso', 'Прозорливость', '奇遇', '機緣');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword124', 'Shadow', 'Schatten', 'Sombra', 'Sombra', 'Ombre', 'Ombra', '闇', '그림자', 'Cień', 'Sombra', 'Тень', '阴影', '暗影');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword125', 'Shadow of Doubt', 'Schatten des Zweifels', 'Sombra de duda', 'Sombra de la duda', 'Ombre du doute', 'Ombra del Dubbio', '疑惑の影', '의심의 그림자', 'Cień Wątpliwości', 'Sombra da Dúvida', 'Тень сомнения', '疑虑之影', '懷疑之影');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword126', 'Silence', 'Stille', 'Silencio', 'Silencio', 'Silence', 'Silenzio', '静寂', '침묵', 'Cisza', 'Silêncio', 'Безмолвие', '沉默', '寂靜');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword127', 'Siren''s Song', 'Sirenengesang', 'Canto de sirena', 'Canto de sirena', 'Sirène', 'Canto della Sirena', 'セイレーンの歌', '세이렌의 노래', 'Pieśń Syreny', 'Canto da Sirena', 'Песнь сирены', '海妖之歌', '賽蓮之歌');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword128', 'Smoke', 'Rauch', 'Humo', 'Humo', 'Fumée', 'Fumo', '煙', '연기', 'Dym', 'Fumaça', 'Дым', '烟雾', '煙霧');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword129', 'Sorrow', 'Traurigkeit', 'Tristeza', 'Melancolía', 'Tristesse', 'Rimorso', '悲哀', '비탄', 'Smutek', 'Mágoa', 'Скорбь', '悲伤', '哀傷');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword130', 'Spirit', 'Geist', 'Espíritu', 'Espíritu', 'Esprit', 'Spirito', '精霊', '영혼', 'Duch', 'Espírito', 'Дух', '精神', '精神');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword131', 'Splendor', 'Pracht', 'Esplendor', 'Esplendor', 'Splendeur', 'Splendore', '壮麗', '광채', 'Splendor', 'Esplendor', 'Великолепие', '壮美', '燦爛');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword132', 'Starlight', 'Sternenlicht', 'Luz de las estrellas', 'Luz estelar', 'Lumière stellaire', 'Luce Stellare', '星の光', '별빛', 'Światło Gwiazd', 'Luz Estelar', 'Звездный свет', '星光', '星光');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword133', 'Stealth', 'Verstohlenheit', 'Sigilo', 'Sigilo', 'Discrétion', 'Furtività', '隠密', '잠행', 'Skradanie', 'Furtividade', 'Незаметность', '隐秘', '隱密');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword134', 'Steel', 'Stahl', 'Acero', 'Acero', 'Acier', 'Acciaio', '鋼', '강철', 'Stal', 'Aço', 'Сталь', '钢铁', '鋼鐵');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword135', 'Still Water', 'Stilles Wasser', 'Aguas tranquilas', 'Agua estancada', 'Eaux calmes', 'Acqua Ferma', '淀み', '고요한 물', 'Spokojna Woda', 'Água Parada', 'Тихие воды', '止水', '寂靜之水');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword136', 'Sting', 'Stachel', 'Aguijón', 'Aguijón', 'Piqûre', 'Pungiglione', '痛烈', '쐐기', 'Żądło', 'Picada', 'Жало', '钉刺', '螫刺');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword137', 'Stone', 'Stein', 'Roca', 'Piedra', 'Pierre', 'Pietra', '石', '돌', 'Kamień', 'Pedra', 'Камень', '磐石', '石塊');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword138', 'Storm', 'Sturm', 'Tormenta', 'Tormenta', 'Orage', 'Ciclone', '嵐', '폭풍', 'Nawałnica', 'Tempestade', 'Шторм', '风暴', '風暴');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword139', 'Strength', 'Stärke', 'Fuerza', 'Fuerza', 'Force', 'Forza', '力', '강함', 'Siła', 'Força', 'Сила', '力量', '力量');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword140', 'Tempest', 'Orkan', 'Tempestad', 'Tempestad', 'Tempête', 'Tempesta', '波乱', '비바람', 'Burza', 'Tormenta', 'Буря', '暴风雨', '暴風雨');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword141', 'Temptation', 'Versuchung', 'Tentación', 'Tentación', 'Tentation', 'Tentazione', '誘惑', '유혹', 'Pokusa', 'Tentação', 'Искушение', '诱惑', '誘惑');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword142', 'Terror', 'Terror', 'Terror', 'Terror', 'Terreur', 'Orrore', '恐怖', '공포', 'Groza', 'Terror', 'Кошмар', '恐惧', '恐佈');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword143', 'Thirst', 'Durst', 'Sed', 'Sed', 'Soif', 'Sete', '渇望', '갈증', 'Pragnienie', 'Sede', 'Жажда', '渴求', '飢渴');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword144', 'Thought', 'Gedanke', 'Pensamiento', 'Pensamiento', 'Stoïque', 'Pensiero', '思考', '사고', 'Myśl', 'Pensamento', 'Мысль', '思绪', '思維');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword145', 'Thunder', 'Donner', 'Trueno', 'Trueno', 'Tonnerre', 'Tuono', '稲妻', '천둥', 'Grom', 'Trovão', 'Гром', '雷霆', '雷霆');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword146', 'Time', 'Zeit', 'Tiempo', 'Tiempo', 'Temps', 'Tempo', '時間', '시간', 'Czas', 'Tempo', 'Время', '时光', '時間');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword147', 'Tradition', 'Tradition', 'Tradición', 'Tradición', 'Tradition', 'Tradizione', '伝統', '전통', 'Tradycja', 'Tradição', 'Традиция', '传统', '傳統');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword148', 'Treachery', 'Verrat', 'Traición', 'Traición', 'Traîtrise', 'Tradimento', '不信', '배신', 'Zdrada', 'Deslealdade', 'Вероломство', '背叛', '背信');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword149', 'Trust', 'Vertrauen', 'Confianza', 'Confianza', 'Confiance', 'Fiducia', '信頼', '신임', 'Zaufanie', 'Confiança', 'Доверие', '信任', '信任');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword150', 'Truth', 'Wahrheit', 'Verdad', 'Verdad', 'Vérité', 'Verità', '真実', '진실', 'Prawda', 'Verdade', 'Правда', '真理', '真理');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword151', 'Unbending Will', 'Unbeugsamer Wille', 'Voluntad de hierro', 'Voluntad inflexible', 'Volonté inébranlable', 'Volontà di Ferro', '恒心', '불굴의 의지', 'Nieugięta Wola', 'Determinação', 'Непреклонность', '不屈之志', '不屈意志');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword152', 'Valor', 'Heldenmut', 'Valor', 'Valor', 'Valeur', 'Valore', '勇敢', '용기', 'Odwaga', 'Bravura', 'Отвага', '勇气', '英勇');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword153', 'Vengeance', 'Rache', 'Venganza', 'Venganza', 'Vengeance', 'Vendetta', '報復', '복수', 'Zemsta', 'Vingança', 'Отмщение', '复仇', '復仇');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword154', 'Venom', 'Geifer', 'Ponzoña', 'Veneno', 'Venin', 'Veleno', '毒', '맹독', 'Jad', 'Veneno', 'Яд', '毒液', '劇毒');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword155', 'Victory', 'Sieg', 'Victoria', 'Victoria', 'Victoire', 'Vittoria', '勝利', '승리', 'Zwycięstwo', 'Vitória', 'Победа', '胜利', '勝利');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword156', 'Voice', 'Stimme', 'Voz', 'Voz', 'Voix', 'Voce', '声', '목소리', 'Głos', 'Voz', 'Голос', '声音', '聲音');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword157', 'Void', '[ms]leerer[fs]leere[ns]leeres[pl]leere', 'Vacío', 'Vacío', 'Vide', 'Vuoto', '虚無', '공허', 'Pustka', 'Vazio', 'Бездна', '虚空', '虛無');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword158', 'War', 'Krieg', 'Guerra', 'Guerra', 'Guerre', 'Guerra', '戦争', '전쟁', 'Wojna', 'Guerra', 'Война', '战争', '戰爭');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword159', 'Water', 'Wasser', 'Agua', 'Agua', 'Eau', 'Acqua', '水', '물', 'Woda', 'Água', 'Вода', '源水', '清水');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword160', 'Wealth', 'Wohlstand', 'Riqueza', 'Riqueza', 'Richesse', 'Ricchezza', '富', '부', 'Bogactwo', 'Riqueza', 'Изобилие', '财富', '財富');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword161', 'Whisper', 'Flüstern', 'Susurro', 'Susurro', 'Murmure', 'Sussurro', '囁き', '속삭임', 'Szept', 'Sussurro', 'Шепот', '低语', '低語');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword162', 'White', 'Weiß', 'Blancura', 'Blanco', 'Blancheur', 'Bianco', '白', '순백', 'Biel', 'Branco', 'Белизна', '白色', '蒼白');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword163', 'Wind', 'Wind', 'Viento', 'Viento', 'Vent', 'Vento', '風', '바람', 'Wiatr', 'Vento', 'Ветер', '疾风', '輕風');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword164', 'Wings of Hope', 'Schwingen der Hoffnung', 'Alas de esperanza', 'Alas de esperanza', 'Ailes de l’espoir', 'Ali della Speranza', '希望の翼', '희망의 날개', 'Wiatr Nadziei', 'Asas da Esperança', 'Крылья надежды', '希望之翼', '希望之翼');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword165', 'Wisdom', 'Weisheit', 'Sabiduría', 'Sabiduría', 'Sagesse', 'Saggezza', '英知', '지혜', 'Mądrość', 'Sabedoria', 'Мудрость', '智慧', '智慧');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword166', 'Woe', 'Unglück', 'Aflicción', 'Aflicción', 'Malheur', 'Pena', '苦悩', '재난', 'Nieszczęście', 'Angústia', 'Горе', '灾祸', '悲痛');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword167', 'Wonder', 'Wunder', 'Asombro', 'Asombro', 'Merveille', 'Meraviglia', '驚異', '경이', 'Cud', 'Fascínio', 'Чудо', '奇迹', '驚奇');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword168', 'Wrath', 'Wut', 'Ira', 'Ira', 'Courroux', 'Ira', '激怒', '진노', 'Gniew', 'Ira', 'Гнев', '怒火', '憤怒');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword169', 'Youth', 'Jugend', 'Juventud', 'Juventud', 'Jeunesse', 'Gioventù', '若さ', '젊음', 'Młodość', 'Juventude', 'Юность', '青春', '年輕');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword170', 'Zephyr', 'Zephyr', 'Céfiro', 'Céfiro', 'Zéphyr', 'Zefiro', '軟風', '서풍', 'Zefir', 'Zefir', 'Бриз', '和风', '和風');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword171', 'Hysteria', 'Hysterie', 'Histeria', 'Histeria', 'Hystérie', 'Isteria', '発奮', '발작', 'Histeria', 'Histeria', 'Истерия', '狂乱', '歇斯底里');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword172', 'Mania', 'Manie', 'Manía', 'Manía', 'Mania', 'Mania', 'マニア', '광기', 'Mania', 'Obsessão', 'Мания', '癫狂', '狂躁');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword173', 'Mosaic', 'Mosaik', 'Mosaico', 'Mosaico', 'Mosaïque', 'Mosaico', '坩堝', '모자이크', 'Mozaika', 'Mosaico', 'Мозаика', '模糊', '嵌飾');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword174', 'Metamorphosis', 'Metamorphose', 'Metamorfosis', 'Metamorfosis', 'Métamorphose', 'Metamorfosi', '変容', '탈태', 'Metamorfoza', 'Metamorfose', 'Метаморфоза', '变形', '變化');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword175', 'Ground', 'Boden', 'Tierra', 'Suelo', 'Terre', 'Suolo', '大地', '접지', 'Grunt', 'Solo', 'Земля', '接地', '接地');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword176', 'Temper', 'Temperament', 'Temperamento', 'Ira', 'Tempérament', 'Tempra', '沈着', '담금질', 'Charakter', 'Índole', 'Закалка', '淬火', '和緩');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword177', 'Hearth', 'Heim', 'Hogar', 'Fogón', 'Âtre', 'Focolare', '炉辺', '화로', 'Palenisko', 'Lar', 'Очаг', '壁炉', '火爐');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword178', 'Cure', 'Heilung', 'Cura', 'Cura', 'Remède', 'Cura', '治癒', '치료', 'Remedium', 'Cura', 'Лекарство', '解药', '治癒');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword180', 'Coven', 'Zirkel', 'Aquelarre', 'Aquelarre', 'Cabale', 'Congrega', '集会', '마녀단', 'Sabat', 'Pacto', 'Ковен', '女巫团', '巫師會');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword181', 'Vigilance', 'Wachsamkeit', 'Vigilancia', 'Vigilancia', 'Vigilance', 'Vigilanza', '警戒', '경계', 'Czujność', 'Vigilância', 'Бдительность', '警戒', '戒慎');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword182', 'Ritual', 'Ritual', 'Ritual', 'Ritual', 'Rituel', 'Rituale', '儀式', '의식', 'Rytuał', 'Ritual', 'Ритуал', '仪式', '儀式');
INSERT OR REPLACE INTO item_runes VALUES ('Runeword179', 'Bulwark', 'Bollwerk', 'Baluarte', 'Baluarte', 'Rempart', 'Baluardo', '防塁', '방벽', 'Szaniec', 'Baluarte', 'Оплот', '壁垒', '壁壘');
COMMIT;
