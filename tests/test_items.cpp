// Catch2 tests for ItemParser against the D2R_Saves fixtures.
//
// The truth values (expected item counts, expected code+quality for the first
// few items) were captured from the reference Java parser (see
// d2rsavegameparser/src/test/java/.../WarlockDump.java).

#include "d2r/CharacterParser.hpp"
#include "d2r/ItemParser.hpp"
#include "d2r/RefDb.hpp"
#include "d2r/Save.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path fixture(const char* name) {
    if (const char* dir = std::getenv("D2R_FIXTURE_DIR")) return fs::path(dir) / name;
#ifdef D2R_TEST_FIXTURE_DIR
    return fs::path(D2R_TEST_FIXTURE_DIR) / name;
#else
    return fs::path("../../D2R_Saves") / name;
#endif
}

fs::path refDbPath() {
    if (const char* env = std::getenv("D2R_REFERENCE_DB")) return env;
#ifdef D2R_TEST_REFERENCE_DB
    return D2R_TEST_REFERENCE_DB;
#else
    return "reference.sqlite";
#endif
}

d2r::RefDb& sharedDb() {
    static d2r::RefDb db(refDbPath());
    static bool loaded = false;
    if (!loaded) { db.loadItemTables(); loaded = true; }
    return db;
}

std::vector<d2r::Item> parseItems(const char* file) {
    const auto bytes = d2r::readFile(fixture(file));
    const auto ch    = d2r::parseCharacter(bytes);
    REQUIRE(ch.itemsOffset != 0);
    d2r::ItemParser parser(sharedDb());
    return parser.parseItems(bytes, ch.itemsOffset);
}

} // namespace

TEST_CASE("item counts across every save match Java reference", "[items][fixtures]") {
    // Numbers captured from the Java parser via WarlockDump.
    struct Case { const char* file; std::size_t count; };
    for (const auto& c : std::array{
        Case{"Amazon.d2s",       39},
        Case{"Assassin.d2s",     28},
        Case{"Barbarian.d2s",    50},
        Case{"Druid.d2s",        31},
        Case{"Necromancer.d2s",  60},
        Case{"Paladin.d2s",      55},
        Case{"Sorceress.d2s",    55},
        Case{"Warlock.d2s",      20},
        Case{"Kai.d2s",          62},
        Case{"DeadKai.d2s",      19},
    }) {
        INFO(c.file);
        const auto items = parseItems(c.file);
        REQUIRE(items.size() == c.count);
    }
}

TEST_CASE("Kai's first ten items match the Java parser", "[items][fixtures]") {
    struct Expected {
        const char* code;
        d2r::ItemQuality quality;
        std::uint8_t ilvl;
    };
    const auto items = parseItems("Kai.d2s");
    REQUIRE(items.size() >= 10);

    constexpr Expected expected[] = {
        {"ci2", d2r::ItemQuality::Magic,    97},
        {"ci1", d2r::ItemQuality::Magic,    92},
        {"amu", d2r::ItemQuality::Magic,    79},
        {"amu", d2r::ItemQuality::Craft,    93},
        {"amu", d2r::ItemQuality::Magic,    96},
        {"amu", d2r::ItemQuality::Magic,    89},
        {"wad", d2r::ItemQuality::Set,      87},
        {"cm3", d2r::ItemQuality::Magic,    81},
        {"ci1", d2r::ItemQuality::Magic,    91},
        {"cm3", d2r::ItemQuality::Magic,    96},
    };
    for (std::size_t i = 0; i < 10; ++i) {
        INFO("item #" << i);
        REQUIRE(items[i].code       == expected[i].code);
        REQUIRE(items[i].quality    == expected[i].quality);
        REQUIRE(items[i].itemLevel  == expected[i].ilvl);
    }
}

TEST_CASE("Warlock fresh character starter items", "[items][fixtures]") {
    const auto items = parseItems("Warlock.d2s");
    REQUIRE(items.size() == 20);
    // First four items are Minor Healing Potions.
    for (std::size_t i = 0; i < 4; ++i) {
        REQUIRE(items[i].code   == "hp1");
        REQUIRE(items[i].simple == true);
    }
    // Item #4 is the class-specific tome "Forgotten Volume" (armor code "wab").
    REQUIRE(items[4].code == "wab");
    REQUIRE(items[4].quality == d2r::ItemQuality::Normal);
    REQUIRE(items[4].socketed);
    REQUIRE(items[4].itemName == "Forgotten Volume");
    // Item #5 is a unique "Measured Wrath".
    REQUIRE(items[5].quality  == d2r::ItemQuality::Unique);
    REQUIRE(items[5].uniqueId != 0);
    REQUIRE(items[5].itemName == "Measured Wrath");
}

TEST_CASE("no exceptions thrown for any D2R_Saves .d2s fixture", "[items][fixtures]") {
    // Enumerate every .d2s in the fixture directory and require full parse.
    const auto dir = fixture("");
    REQUIRE(fs::exists(dir));
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".d2s") continue;
        INFO(entry.path().string());
        const auto bytes = d2r::readFile(entry.path());
        const auto ch    = d2r::parseCharacter(bytes);
        if (ch.itemsOffset == 0) continue; // freshly-rolled character
        d2r::ItemParser parser(sharedDb());
        std::size_t failIdx = 0;
        std::string failMsg;
        const auto items = parser.parseItems(bytes, ch.itemsOffset, &failIdx, &failMsg);
        REQUIRE(failMsg.empty());
        REQUIRE(items.size() == ch.itemCount);
    }
}
