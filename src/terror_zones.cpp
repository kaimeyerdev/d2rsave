// Terror-zone forecasting implementation. See include/d2r/TerrorZones.hpp
// for the public API and TODO.md for the validation plan.

#include "d2r/TerrorZones.hpp"

#include "d2r/RefDb.hpp"

#include <algorithm>
#include <cstdint>

namespace d2r {

std::vector<TerrorZone> loadTerrorZones(RefDb& db) {
    std::vector<TerrorZone> out;
    auto st = db.prepare(
        "SELECT CAST(id AS INTEGER), game_id, name, CAST(act AS INTEGER) "
        "FROM terrorzones ORDER BY CAST(id AS INTEGER)");
    while (st.step()) {
        TerrorZone z;
        z.id     = static_cast<int>(st.columnInt64(0));
        z.gameId = st.columnText(1);
        z.name   = st.columnText(2);
        z.act    = static_cast<int>(st.columnInt64(3));
        out.push_back(std::move(z));
    }
    return out;
}

namespace {

// SplitMix64 finalizer. Deterministic, no state, very good avalanche.
// This is a placeholder for whatever mixer D2R actually uses -- swap
// this out when oracle testing reveals the real function.
constexpr std::uint64_t mix64(std::uint64_t x) noexcept {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x;
}

// Golden-ratio constant used to avoid same-mixer collisions between
// consecutive `i` values when picking multiple zones per hour.
constexpr std::uint64_t kSalt = 0x9E3779B97F4A7C15ULL;

} // namespace

std::vector<TerrorZoneSlot> forecastTerrorZones(
    const std::vector<TerrorZone>& zones,
    std::chrono::sys_seconds       anchor,
    std::chrono::sys_seconds       from,
    int                            slotsAhead,
    int                            zonesPerSlot) {
    std::vector<TerrorZoneSlot> out;
    if (zones.empty() || slotsAhead <= 0 || zonesPerSlot <= 0) return out;

    // Zones rotate every kTerrorZoneSlotLength (30 minutes) on the UTC
    // clock; snap `from` and `anchor` down to the containing slot before
    // computing the diff. `slot_t` is one duration unit == one slot, so
    // `floor<slot_t>` snaps to the surrounding :00 / :30 boundary.
    using slot_t = std::chrono::duration<
        std::int64_t,
        std::ratio_multiply<std::ratio<kTerrorZoneSlotLength.count()>,
                            std::chrono::minutes::period>>;
    const auto fromSlot   = std::chrono::floor<slot_t>(from);
    const auto anchorSlot = std::chrono::floor<slot_t>(anchor);
    const int  capacity   = std::min(zonesPerSlot,
                                      static_cast<int>(zones.size()));

    out.reserve(static_cast<std::size_t>(slotsAhead));
    for (int step = 0; step < slotsAhead; ++step) {
        const auto currentSlot = fromSlot + slot_t{step};
        const auto delta       = (currentSlot - anchorSlot).count();
        const std::uint64_t seed = static_cast<std::uint64_t>(delta);

        // Pick `capacity` distinct zone indices by mixing (seed + i*salt).
        // Small-N rejection sampling; the loop guard caps at a small
        // multiple of |zones| so we never spin.
        TerrorZoneSlot rec;
        rec.slot = std::chrono::time_point_cast<std::chrono::seconds>(currentSlot);
        rec.zoneIds.reserve(static_cast<std::size_t>(capacity));

        for (std::size_t i = 0, tries = 0;
             static_cast<int>(rec.zoneIds.size()) < capacity
             && tries < zones.size() * 4;
             ++i, ++tries) {
            const auto h64 = mix64(seed + static_cast<std::uint64_t>(i) * kSalt);
            const auto idx = static_cast<std::size_t>(h64 % zones.size());
            const int zid  = zones[idx].id;
            if (std::find(rec.zoneIds.begin(), rec.zoneIds.end(), zid)
                == rec.zoneIds.end()) {
                rec.zoneIds.push_back(zid);
            }
        }
        out.push_back(std::move(rec));
    }
    return out;
}

} // namespace d2r
