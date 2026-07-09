// Tests for src/terror_zones.cpp.
//
// Test taxonomy:
//
//   [terrorzones]              - passes today; guard rails for the API
//                                (determinism, slot alignment, edge
//                                cases). DO NOT let these regress.
//
//   [terrorzones][!mayfail]    - Work-in-progress. Compares the
//                                forecaster's output to the 672-slot
//                                ground-truth oracle in
//                                tests/tz_oracle_data.cpp (2 weeks of
//                                d2tz.info/offline observations). Will
//                                fail until the RNG in
//                                src/terror_zones.cpp is swapped out for
//                                the reverse-engineered one; Catch2
//                                reports them without failing the run.
//
// When the algorithm is reversed, drop the `!mayfail` tag off each
// [terrorzones] test as it starts passing so regressions surface
// immediately. See TODO.md > "Terror Zones -- reverse-engineer the
// offline schedule" for the plan.

#include <catch2/catch_test_macros.hpp>

#include "d2r/RefDb.hpp"
#include "d2r/TerrorZones.hpp"
#include "tz_oracle_data.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <set>
#include <string>
#include <vector>

using namespace std::chrono;
using d2r::testdata::kD2tzInfoOracle;

namespace {

// Small synthetic zone list so the API-shape tests are independent of
// the shipped reference DB.
std::vector<d2r::TerrorZone> syntheticZones() {
    return {
        {0, "Act1-Fake0", "Zone 0", 1},
        {1, "Act1-Fake1", "Zone 1", 1},
        {2, "Act2-Fake2", "Zone 2", 2},
        {3, "Act3-Fake3", "Zone 3", 3},
        {4, "Act4-Fake4", "Zone 4", 4},
        {5, "Act5-Fake5", "Zone 5", 5},
        {6, "Act1-Fake6", "Zone 6", 1},
        {7, "Act2-Fake7", "Zone 7", 2},
        {8, "Act3-Fake8", "Zone 8", 3},
        {9, "Act4-Fake9", "Zone 9", 4},
    };
}

sys_seconds parseISO(const char* iso) {
    int y, mo, d, h, mi, se;
    if (std::sscanf(iso, "%d-%d-%dT%d:%d:%d",
                    &y, &mo, &d, &h, &mi, &se) != 6) {
        return sys_seconds{};
    }
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
    tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = se;
    return sys_seconds{seconds{timegm(&tm)}};
}

} // namespace

// ---------------------------------------------------------------------------
// [terrorzones] -- API-shape guard rails, expected to always pass.
// ---------------------------------------------------------------------------

TEST_CASE("Forecast is deterministic for identical inputs", "[terrorzones]") {
    const auto zones  = syntheticZones();
    const auto anchor = d2r::kDefaultTerrorZoneAnchor;
    const auto from   = parseISO("2026-07-06T00:00:00Z");
    const auto a = d2r::forecastTerrorZones(zones, anchor, from, 48, 3);
    const auto b = d2r::forecastTerrorZones(zones, anchor, from, 48, 3);
    REQUIRE(a == b);
}

TEST_CASE("Each slot has zonesPerSlot distinct zones", "[terrorzones]") {
    const auto zones  = syntheticZones();
    const auto anchor = d2r::kDefaultTerrorZoneAnchor;
    const auto from   = parseISO("2026-07-06T00:00:00Z");
    const auto forecast =
        d2r::forecastTerrorZones(zones, anchor, from, 48, 3);
    REQUIRE(forecast.size() == 48);
    for (const auto& f : forecast) {
        REQUIRE(f.zoneIds.size() == 3);
        std::set<int> uniq(f.zoneIds.begin(), f.zoneIds.end());
        REQUIRE(uniq.size() == 3);
        for (const int id : f.zoneIds) {
            REQUIRE(id >= 0);
            REQUIRE(id <= 9);
        }
    }
}

TEST_CASE("Slots align to :00 / :30 marks", "[terrorzones]") {
    const auto zones  = syntheticZones();
    const auto anchor = d2r::kDefaultTerrorZoneAnchor;
    // 00:37:15 -> should round down to 00:30:00, then step by 30 minutes.
    const auto from = parseISO("2026-07-06T00:37:15Z");
    const auto forecast =
        d2r::forecastTerrorZones(zones, anchor, from, 4, 1);
    REQUIRE(forecast.size() == 4);
    REQUIRE(forecast[0].slot == parseISO("2026-07-06T00:30:00Z"));
    REQUIRE(forecast[1].slot == parseISO("2026-07-06T01:00:00Z"));
    REQUIRE(forecast[2].slot == parseISO("2026-07-06T01:30:00Z"));
    REQUIRE(forecast[3].slot == parseISO("2026-07-06T02:00:00Z"));
}

TEST_CASE("Slots exactly on a boundary don't shift", "[terrorzones]") {
    const auto zones  = syntheticZones();
    const auto anchor = d2r::kDefaultTerrorZoneAnchor;
    const auto from   = parseISO("2026-07-06T00:30:00Z");
    const auto forecast =
        d2r::forecastTerrorZones(zones, anchor, from, 2, 1);
    REQUIRE(forecast.size() == 2);
    REQUIRE(forecast[0].slot == parseISO("2026-07-06T00:30:00Z"));
    REQUIRE(forecast[1].slot == parseISO("2026-07-06T01:00:00Z"));
}

TEST_CASE("Edge cases: empty inputs return empty forecasts", "[terrorzones]") {
    const auto anchor = d2r::kDefaultTerrorZoneAnchor;
    const auto from   = parseISO("2026-07-06T00:00:00Z");
    REQUIRE(d2r::forecastTerrorZones({}, anchor, from, 6, 3).empty());
    REQUIRE(d2r::forecastTerrorZones(
        syntheticZones(), anchor, from, 0, 3).empty());
    REQUIRE(d2r::forecastTerrorZones(
        syntheticZones(), anchor, from, 6, 0).empty());
}

TEST_CASE("Oracle vector is well-formed and every pool_idx is in range",
          "[terrorzones]") {
    REQUIRE(kD2tzInfoOracle.size() == 14 * 48);
    REQUIRE(kD2tzInfoOracle.front().slot ==
            parseISO("2026-07-03T06:00:00Z"));
    // 7/16/2026 23:30 MDT (UTC-6) == 7/17/2026 05:30 UTC.
    REQUIRE(kD2tzInfoOracle.back().slot ==
            parseISO("2026-07-17T05:30:00Z"));
    for (const auto& row : kD2tzInfoOracle) {
        REQUIRE(row.zoneIds.size() == 1);
        REQUIRE(row.zoneIds.front() >= 0);
        REQUIRE(row.zoneIds.front() < 34);
    }
    // Slots must be strictly increasing by exactly one slot length.
    for (std::size_t i = 1; i < kD2tzInfoOracle.size(); ++i) {
        const auto gap = kD2tzInfoOracle[i].slot
                       - kD2tzInfoOracle[i - 1].slot;
        REQUIRE(gap == d2r::kTerrorZoneSlotLength);
    }
}

// ---------------------------------------------------------------------------
// [terrorzones][!mayfail] -- properties of the REAL D2R schedule that the
// current placeholder RNG fails to reproduce. Kept as work-in-progress
// signals for the algorithm reversal in TODO.md.
// ---------------------------------------------------------------------------

TEST_CASE("Forecast matches the 672-slot d2tz.info oracle",
          "[terrorzones][!mayfail]") {
#ifndef D2R_TEST_REFERENCE_DB
    SKIP("D2R_TEST_REFERENCE_DB not defined");
#else
    d2r::RefDb db(D2R_TEST_REFERENCE_DB);
    const auto zones = d2r::loadTerrorZones(db);
    REQUIRE_FALSE(zones.empty());

    const auto n     = kD2tzInfoOracle.size();
    const auto from  = kD2tzInfoOracle.front().slot;
    const auto fcst  = d2r::forecastTerrorZones(
        zones, d2r::kDefaultTerrorZoneAnchor, from,
        static_cast<int>(n), 1);
    REQUIRE(fcst == kD2tzInfoOracle);
#endif
}

TEST_CASE("Forecast matches the first 48 slots of the oracle",
          "[terrorzones][!mayfail]") {
    // Smaller-slice variant of the big-lookup test. Useful while
    // debugging a candidate picker -- if this passes but the big one
    // still fails, the picker drifts partway through the window.
#ifndef D2R_TEST_REFERENCE_DB
    SKIP("D2R_TEST_REFERENCE_DB not defined");
#else
    constexpr int kCount = 48;
    d2r::RefDb db(D2R_TEST_REFERENCE_DB);
    const auto zones = d2r::loadTerrorZones(db);
    const auto from  = kD2tzInfoOracle.front().slot;
    const auto fcst  = d2r::forecastTerrorZones(
        zones, d2r::kDefaultTerrorZoneAnchor, from, kCount, 1);
    const std::vector<d2r::TerrorZoneSlot> expected(
        kD2tzInfoOracle.begin(), kD2tzInfoOracle.begin() + kCount);
    REQUIRE(fcst == expected);
#endif
}

TEST_CASE("Forecast respects the K=1 no-repeat rule "
          "(no zone twice in a row)",
          "[terrorzones][!mayfail]") {
    // Confirmed on the oracle: 0 self-transitions in 671 pairs.
    // P(0 | uniform iid over 34 zones) = (33/34)^671 ~= 2e-9.
#ifndef D2R_TEST_REFERENCE_DB
    SKIP("D2R_TEST_REFERENCE_DB not defined");
#else
    d2r::RefDb db(D2R_TEST_REFERENCE_DB);
    const auto zones = d2r::loadTerrorZones(db);
    const auto fcst  = d2r::forecastTerrorZones(
        zones, d2r::kDefaultTerrorZoneAnchor,
        kD2tzInfoOracle.front().slot,
        static_cast<int>(kD2tzInfoOracle.size()), 1);
    int repeats = 0;
    for (std::size_t i = 1; i < fcst.size(); ++i) {
        if (fcst[i].zoneIds == fcst[i - 1].zoneIds) ++repeats;
    }
    INFO("consecutive-zone repeats: " << repeats << " (expected 0)");
    REQUIRE(repeats == 0);
#endif
}

TEST_CASE("Forecast respects the K=2 no-repeat rule "
          "(no A-B-A patterns)",
          "[terrorzones][!mayfail]") {
    // Confirmed on the oracle: 0 A-B-A occurrences in 670 triples.
#ifndef D2R_TEST_REFERENCE_DB
    SKIP("D2R_TEST_REFERENCE_DB not defined");
#else
    d2r::RefDb db(D2R_TEST_REFERENCE_DB);
    const auto zones = d2r::loadTerrorZones(db);
    const auto fcst  = d2r::forecastTerrorZones(
        zones, d2r::kDefaultTerrorZoneAnchor,
        kD2tzInfoOracle.front().slot,
        static_cast<int>(kD2tzInfoOracle.size()), 1);
    int aba = 0;
    for (std::size_t i = 2; i < fcst.size(); ++i) {
        if (fcst[i].zoneIds == fcst[i - 2].zoneIds) ++aba;
    }
    INFO("A-B-A occurrences: " << aba << " (expected 0)");
    REQUIRE(aba == 0);
#endif
}

TEST_CASE("Forecast has a tightly bounded zone-frequency spread "
          "(anti-clustering picker)",
          "[terrorzones][!mayfail]") {
    // Over 672 slots of oracle data every one of 34 zones appears
    // 16..25 times (spread=9, mean=19.76, variance=5.89). A uniform
    // iid sampler would give variance ~= mean = 19.76 (Poisson). The
    // real picker enforces tighter spread; we allow max-min <= 12
    // (some head-room over the observed spread of 9).
#ifndef D2R_TEST_REFERENCE_DB
    SKIP("D2R_TEST_REFERENCE_DB not defined");
#else
    d2r::RefDb db(D2R_TEST_REFERENCE_DB);
    const auto zones = d2r::loadTerrorZones(db);
    const auto fcst  = d2r::forecastTerrorZones(
        zones, d2r::kDefaultTerrorZoneAnchor,
        kD2tzInfoOracle.front().slot,
        static_cast<int>(kD2tzInfoOracle.size()), 1);
    std::vector<int> count(zones.size(), 0);
    for (const auto& row : fcst) {
        REQUIRE(row.zoneIds.size() == 1);
        const int id = row.zoneIds.front();
        REQUIRE(id >= 0);
        REQUIRE(id < static_cast<int>(count.size()));
        ++count[id];
    }
    const int lo = *std::min_element(count.begin(), count.end());
    const int hi = *std::max_element(count.begin(), count.end());
    INFO("zone-frequency spread: min=" << lo << " max=" << hi
         << " (oracle observed 16..25 over 672 slots)");
    REQUIRE(hi - lo <= 12);
#endif
}
