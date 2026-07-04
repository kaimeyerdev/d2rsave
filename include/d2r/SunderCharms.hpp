// Sunder-charm crafting pair map.
//
// D2R's crafting recipe consumes a "Latent" sunder charm and produces a
// "Renewed" one. The chronicle records only the Latent (base item found)
// -- it does NOT emit a fresh entry for the Renewed result. The reconcile
// subcommand therefore needs to know that owning a Renewed charm satisfies
// the chronicle entry for its paired Latent (and vice versa).
//
// Display names are NOT hard-coded here. They come from the D2R string-table
// overlay (`item_names` SQL table, populated from item-names.json). If a live
// D2R patch renames these items and your extracted item-names.json is older,
// the fix is to re-run cascviewer against your current D2R install and drop
// the fresh JSON into the sibling examples project, then rerun `regen-sql`.

#pragma once

#include <array>
#include <cstdint>

namespace d2r {

struct SunderPair {
    std::uint16_t latentId;
    std::uint16_t renewedId;
};

// Numeric IDs come from the *ID column of uniqueitems.txt (currently 426..437).
inline constexpr std::array<SunderPair, 6> kSunderPairs{{
    {426, 427}, // Cold Rupture
    {428, 433}, // Flame Rift
    {429, 434}, // Crack of the Heavens
    {430, 435}, // Rotting Fissure
    {431, 436}, // Bone Break
    {432, 437}, // Black Cleft
}};

// Return the paired sunder-charm unique ID (Latent<->Renewed), or 0 if the
// id is not part of a pair.
[[nodiscard]] constexpr std::uint32_t sunderPairedId(std::uint32_t id) noexcept {
    for (const auto& p : kSunderPairs) {
        if (id == p.latentId)  return p.renewedId;
        if (id == p.renewedId) return p.latentId;
    }
    return 0;
}

} // namespace d2r
