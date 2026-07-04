// Shared-stash + chronicle model.
//
// The D2R shared stash (.d2i) file contains a fixed number of "tabs" back-to-back,
// each starting with the four-byte magic `55 AA 55 AA`. Older files have 3 tabs,
// RotW files have 7: 6 storage tabs plus a chronicle tab that tracks which
// unique/set items and runewords the account has ever seen.
//
// See the reference Java parser at
// d2rsavegameparser/parser/SharedStashParser.java.

#pragma once

#include "d2r/Item.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace d2r {

// One chronicle entry (10 bytes on disk): itemId(32) + monsterId(16) + timestamp(32).
// The timestamp is minutes-since-Unix-epoch; multiply by 60 for seconds.
struct ChronicleEntry {
    std::uint32_t itemId          = 0;
    std::uint16_t monsterId       = 0;
    std::uint32_t timestampMinutes = 0;
    ItemQuality   quality         = ItemQuality::None; // Set/Unique/Normal (runewords use Normal)
};

// Chronicle tab (the 7th tab in RotW shared-stash files).
struct ChronicleTab {
    std::vector<ChronicleEntry> setItems;
    std::vector<ChronicleEntry> uniques;
    std::vector<ChronicleEntry> runewords;
};

// One normal storage tab (parsed items + gold + length).
struct StashTab {
    std::uint32_t     version      = 0;
    std::uint32_t     gold         = 0;
    std::uint32_t     lengthBytes  = 0;
    std::size_t       fileOffset   = 0;
    std::vector<Item> items;
};

// Full parse result of a shared-stash file.
struct SharedStash {
    std::vector<StashTab>       tabs;      // normal tabs only (pre-RotW: 3; RotW: 6)
    std::optional<ChronicleTab> chronicle; // present only in RotW files
};

} // namespace d2r
