// Offline forecasting for Diablo II: Resurrected terror zones.
//
// Only compiled when D2R_HAVE_SQLITE is defined (the zone list lives in
// the reference DB). The forecasting itself is a pure function of an
// anchor timestamp, a target timestamp, the zone list, and a swappable
// hash function -- no state, no external I/O.
//
// D2R rotates the active zone(s) every 30 minutes on the UTC clock
// (:00 and :30 minute marks). All forecast slots align to those
// boundaries. The anchor + hash pair is a placeholder we validate
// empirically. See TODO.md under "Dashboard / Terror Zones" for the
// plan.

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace d2r {

class RefDb;

// One row in the `terrorzones` reference table. `id` is the pool index
// (0..N-1) matching the JSON `zones[i]` order in
// Data\hd\global\excel\desecratedzones.json; `gameId` is Blizzard's
// stable internal identifier for the zone (e.g. "Act2-ArcaneSanctuary").
struct TerrorZone {
    int         id;
    std::string gameId;
    std::string name;
    int         act;
};

// One rotation slot's forecast: which zone ids are active during the
// 30-minute window that starts at `slot` (UTC, aligned to :00 / :30).
struct TerrorZoneSlot {
    std::chrono::sys_seconds slot;
    std::vector<int>         zoneIds;

    friend bool operator==(const TerrorZoneSlot&,
                           const TerrorZoneSlot&) = default;
};

// Length of one rotation slot. D2R changed from a 60-minute to a
// 30-minute cadence in a recent patch; all forecasting is now in terms
// of this constant.
inline constexpr std::chrono::minutes kTerrorZoneSlotLength{30};

// Load the zone catalog from the reference DB. Sorted by id.
[[nodiscard]] std::vector<TerrorZone> loadTerrorZones(RefDb& db);

// Forecast `slotsAhead` consecutive 30-minute slots starting at the slot
// that contains `from` (rounded down to the previous :00 / :30 mark),
// each carrying `zonesPerSlot` distinct zone ids. `anchor` is the epoch
// the RNG is measured against; two slots with the same
// `slotsSinceAnchor` value always resolve to the same set.
//
// If `zones` is empty or either counter is <= 0, returns an empty vector.
[[nodiscard]] std::vector<TerrorZoneSlot> forecastTerrorZones(
    const std::vector<TerrorZone>& zones,
    std::chrono::sys_seconds       anchor,
    std::chrono::sys_seconds       from,
    int                            slotsAhead,
    int                            zonesPerSlot);

// Community-published anchor (2023-02-16 19:00 UTC, from various reddit
// threads) turns out to be WRONG. The authoritative value lives in
// `Data\hd\global\excel\desecratedzones.json` inside the CASC storage:
//
//   "start_time_utc":         "2025-12-05 00:00:00"    (UTC)
//   "zone_duration_minutes":  30
//   "break_duration_minutes": 0
//   "seed":                   16665365343970128666      (0xE747457BC371F31A)
//   "zones":                  [ ... 34 entries ... ]
//
// So the anchor rolls forward whenever Blizzard reships the file (Season
// resets, patches). The current value is baked in below; keep it in sync
// with the CASC file by re-running d2r_casc_dump on a fresh install.
// The 64-bit `kDefaultTerrorZoneSeed` is the actual RNG seed the game
// consumes -- see src/terror_zones.cpp for the (still-unmatched) mixer.
inline constexpr std::chrono::sys_seconds kDefaultTerrorZoneAnchor{
    std::chrono::seconds{1764892800}   // 2025-12-05T00:00:00Z
};

// Extracted alongside the anchor from desecratedzones.json. Used as the
// input to the not-yet-reversed slot-picking RNG in src/terror_zones.cpp.
inline constexpr std::uint64_t kDefaultTerrorZoneSeed = 0xE747457BC371F31AULL;

} // namespace d2r
