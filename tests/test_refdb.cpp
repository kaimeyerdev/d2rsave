// Catch2 tests for the RefDb wrapper + baked reference-DB row counts.

#include "d2r/RefDb.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path refDbPath() {
    if (const char* env = std::getenv("D2R_REFERENCE_DB")) {
        return env;
    }
#ifdef D2R_TEST_REFERENCE_DB
    return D2R_TEST_REFERENCE_DB;
#else
    return "reference.sqlite";
#endif
}

} // namespace

TEST_CASE("Reference DB opens and reports expected row counts", "[refdb]") {
    const auto db_path = refDbPath();
    INFO("db path: " << db_path.string());
    REQUIRE(fs::exists(db_path));

    d2r::RefDb db(db_path);

    // Expected counts pinned from the current data/sql/*.sql seeds.
    // If txt/ upstream changes, regenerate seeds and update these numbers.
    struct Expect { const char* table; std::int64_t count; };
    constexpr Expect kExpected[] = {
        {"armor",         217},
        {"weapons",       306},
        {"misc",          169},
        {"uniqueitems",   438},
        {"setitems",      140},
        {"sets",           35},
        {"itemstatcost",  368},
        {"properties",    284},
        {"magicprefix",   721},
        {"magicsuffix",   785},
        {"rareprefix",     46},
        {"raresuffix",    155},
        {"runes",         181},
        {"gems",           68},
        {"hireling",      135},
    };
    for (const auto& e : kExpected) {
        INFO("table: " << e.table);
        REQUIRE(db.countRows(e.table) == e.count);
    }
}

TEST_CASE("uniqueitems has plausible content", "[refdb][sanity]") {
    d2r::RefDb db(refDbPath());
    // NOTE: in uniqueitems.txt, `index` is the unique display name
    // (e.g. "Windforce"); `itemname` holds the base type ("Long War Bow").
    auto stmt = db.prepare(
        "SELECT \"index\", code FROM uniqueitems "
        "WHERE \"index\" IN ('Windforce', 'The Grandfather', 'Stormspire') "
        "ORDER BY \"index\"");
    int rows = 0;
    while (stmt.step()) {
        ++rows;
        const auto name = stmt.columnText(0);
        const auto code = stmt.columnText(1);
        INFO(name << " -> " << code);
        REQUIRE(!name.empty());
        REQUIRE(!code.empty());
    }
    REQUIRE(rows == 3);
}
