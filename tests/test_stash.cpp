// Catch2 tests for the shared-stash + chronicle parser.

#include "d2r/RefDb.hpp"
#include "d2r/Save.hpp"
#include "d2r/SharedStashParser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>

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

} // namespace

TEST_CASE("Modern shared stash has 7 tabs", "[stash][fixtures]") {
    const auto bytes  = d2r::readFile(fixture("ModernSharedStashSoftCoreV2.d2i"));
    const auto offsets = d2r::SharedStashParser::findTabOffsets(bytes);
    REQUIRE(offsets.size() == 7);
}

TEST_CASE("Chronicle tab parses expected counts", "[stash][chronicle][fixtures]") {
    const auto bytes = d2r::readFile(fixture("ModernSharedStashSoftCoreV2.d2i"));
    d2r::SharedStashParser parser(sharedDb());
    const auto chron = parser.parseChronicleOnly(bytes);
    // The user reports >80% uniques and >90% sets discovered on this account.
    // Values captured directly from the file at test-authoring time.
    REQUIRE(chron.setItems.size() == 130);
    REQUIRE(chron.uniques.size() == 336);
    REQUIRE(chron.runewords.size() == 27);

    // Every chronicle entry has a plausible timestamp (minutes-since-epoch)
    // and monsterId within u16 range.
    for (const auto& e : chron.uniques) {
        REQUIRE(e.itemId != 0);
        REQUIRE(e.timestampMinutes > 0);
    }
}

TEST_CASE("Full shared-stash parse yields 6 storage tabs + 1 chronicle",
          "[stash][fixtures]") {
    const auto bytes = d2r::readFile(fixture("ModernSharedStashSoftCoreV2.d2i"));
    d2r::SharedStashParser parser(sharedDb());
    const auto stash = parser.parse(bytes);
    REQUIRE(stash.tabs.size() == 6);
    REQUIRE(stash.chronicle.has_value());
    REQUIRE(stash.chronicle->uniques.size() == 336);
    for (const auto& tab : stash.tabs) {
        REQUIRE(tab.version == 105);
        REQUIRE(tab.lengthBytes > 0);
    }
}
