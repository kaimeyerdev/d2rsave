// Unit tests for the SQL emitter: verifies byte-for-byte layout matches the
// existing data/sql/*.sql format.

#include "d2r/SqlEmitter.hpp"
#include "d2r/StringTable.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sstream>

TEST_CASE("writeExcelSql emits expected byte layout", "[sql-emitter]") {
    d2r::ExcelTable t;
    t.columns = {"name", "code", "reqstr"};
    t.rows.push_back({"Cap", "cap", "0"});
    t.rows.push_back({"Griswold's Valor", "urn", ""});

    std::ostringstream out;
    d2r::writeExcelSql(out, "armor", t, "-- header comment");

    const std::string expected =
        "-- header comment\n"
        "\n"
        "DROP TABLE IF EXISTS armor;\n"
        "CREATE TABLE armor (\n"
        "    \"name\" TEXT,\n"
        "    \"code\" TEXT,\n"
        "    \"reqstr\" TEXT\n"
        ");\n"
        "\n"
        "BEGIN;\n"
        "INSERT INTO armor VALUES ('Cap', 'cap', '0');\n"
        "INSERT INTO armor VALUES ('Griswold''s Valor', 'urn', NULL);\n"
        "COMMIT;\n";
    REQUIRE(out.str() == expected);
}

TEST_CASE("writeExcelSql without attribution omits the leading block", "[sql-emitter]") {
    d2r::ExcelTable t;
    t.columns = {"k"};
    t.rows.push_back({"v"});

    std::ostringstream out;
    d2r::writeExcelSql(out, "kv", t, "");
    const std::string s = out.str();
    REQUIRE(s.rfind("DROP TABLE", 0) == 0);
}

TEST_CASE("writeStringTableSql emits 13 locale columns", "[sql-emitter]") {
    d2r::StringTableRow r;
    r.key = "qf1";
    r.locales.assign(13, "");
    r.locales[0] = "Khalim's Flail"; // en_us
    r.locales[3] = "Mangual de Khalim"; // es_mx (index 3 in the fixed order)

    std::ostringstream out;
    d2r::writeStringTableSql(out, "item_names", { r }, "-- ok");

    const std::string s = out.str();
    // Header + create table
    REQUIRE(s.find("CREATE TABLE item_names") != std::string::npos);
    REQUIRE(s.find("\"key\" TEXT PRIMARY KEY") != std::string::npos);
    for (const auto& col : d2r::stringTableLocaleColumns()) {
        INFO("locale column: " << col);
        REQUIRE(s.find("\"" + col + "\" TEXT") != std::string::npos);
    }
    // The row: key + 13 values (with escaped quote in en_us and NULL for missing locales)
    REQUIRE(s.find("INSERT OR REPLACE INTO item_names VALUES ('qf1', 'Khalim''s Flail', NULL, NULL, 'Mangual de Khalim', NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);") != std::string::npos);
}

TEST_CASE("parseStringTable ingests the D2R JSON schema", "[string-table]") {
    std::string_view json = R"([
        {"id": 1, "Key": "qf1",
         "enUS": "Khalim's Flail",
         "deDE": "Khalims Kultflegel",
         "esES": "Rompecabezas de Khalim",
         "esMX": "Mangual de Khalim",
         "frFR": "Fleau",
         "itIT": "Flagello",
         "jaJP": "JP",
         "koKR": "KR",
         "plPL": "PL",
         "ptBR": "BR",
         "ruRU": "RU",
         "zhCN": "CN",
         "zhTW": "TW"}
    ])";
    const auto rows = d2r::parseStringTable(json);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].key == "qf1");
    REQUIRE(rows[0].locales.size() == 13);
    // Order: en_us, de_de, es_es, es_mx, fr_fr, it_it, ja_jp, ko_kr, pl_pl, pt_br, ru_ru, zh_cn, zh_tw
    REQUIRE(rows[0].locales[0]  == "Khalim's Flail");
    REQUIRE(rows[0].locales[1]  == "Khalims Kultflegel");
    REQUIRE(rows[0].locales[2]  == "Rompecabezas de Khalim");
    REQUIRE(rows[0].locales[3]  == "Mangual de Khalim");
    REQUIRE(rows[0].locales[4]  == "Fleau");
    REQUIRE(rows[0].locales[5]  == "Flagello");
    REQUIRE(rows[0].locales[6]  == "JP");
    REQUIRE(rows[0].locales[7]  == "KR");
    REQUIRE(rows[0].locales[8]  == "PL");
    REQUIRE(rows[0].locales[9]  == "BR");
    REQUIRE(rows[0].locales[10] == "RU");
    REQUIRE(rows[0].locales[11] == "CN");
    REQUIRE(rows[0].locales[12] == "TW");
}
