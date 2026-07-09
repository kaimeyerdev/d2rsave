// Auto-generated from the d2tz.info/offline schedule, week of
// 2026-07-03 through 2026-07-16 (14 days x 48 half-hour slots = 672
// slots). See tests/test_terror_zones.cpp for what the oracle is used
// for and TODO.md > "Terror Zones" for the plan to make the forecaster
// match it.
//
// The layout matches std::vector<d2r::TerrorZoneSlot> exactly so tests
// can compare a forecast slice directly to a slice of this oracle:
//
//     auto fcst = d2r::forecastTerrorZones(zones, anchor,
//                                          kD2tzInfoOracle.front().slot,
//                                          n, 1);
//     REQUIRE(fcst == std::vector<d2r::TerrorZoneSlot>(
//                          kD2tzInfoOracle.begin(),
//                          kD2tzInfoOracle.begin() + n));
//
// Regeneration: rerun the (one-off) generator that lived in
// scripts/gen_tz_oracle.py against the CSV + desecratedzones.json.
// If pasting new CSV weeks, ensure the local timezone is MDT (UTC-6)
// or adjust the timestamp shift before regenerating.

#pragma once

#include "d2r/TerrorZones.hpp"

#include <vector>

namespace d2r::testdata {

// 672 slots covering 2026-07-03T06:00:00Z .. 2026-07-16T05:30:00Z,
// sorted by slot ascending, each with a 1-element zoneIds matching
// the pool index in data/sql/17_terrorzones.sql.
extern const std::vector<::d2r::TerrorZoneSlot> kD2tzInfoOracle;

} // namespace d2r::testdata
