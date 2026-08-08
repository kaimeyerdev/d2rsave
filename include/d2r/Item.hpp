// D2R item model. Mirrors the shape of the Java Item record while trimming
// fields we can't populate without deeper name-resolution work (treasureClass,
// runeword-name inference from socket contents, etc.).

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace d2r {

enum class ItemQuality : std::uint8_t {
    None     = 0,
    Inferior = 1,
    Normal   = 2,
    Superior = 3,
    Magic    = 4,
    Set      = 5,
    Rare     = 6,
    Unique   = 7,
    Craft    = 8,
    Unknown  = 0xFF,
};
std::string_view toString(ItemQuality q) noexcept;

// Item's high-level classification (armor / weapon / misc / unknown).
enum class ItemType : std::uint8_t {
    Unknown = 0,
    Armor   = 1,
    Weapon  = 2,
    Misc    = 3,
};

struct ItemProperty {
    std::uint16_t              id            = 0;
    std::string                stat;
    std::vector<std::int32_t>  values;
    std::uint8_t               qualityFlag   = 0;
};

struct Item {
    // Header flags (java bit indices: 5, 12, 17, 22, 23, 25, 27, 29 → 1-indexed
    // == LSB-first stream position - 1). See item_parser.cpp for the mapping.
    bool identified       = false;
    bool socketed         = false;
    bool ear              = false;
    bool simple           = false;
    bool ethereal         = false;
    bool personalized     = false;
    bool runeword         = false;
    bool hasChronicleData = false;

    // Location (16 bits after 3 skipped): location(3), position(4), y(4), x(4), container(3)
    std::uint8_t location  = 0;
    std::uint8_t position  = 0;
    std::uint8_t x         = 0;
    std::uint8_t y         = 0;
    std::uint8_t container = 0;

    // Item identity
    std::string code;                    // e.g. "amu", "wnd", "swd"
    std::string itemName;                // display name from txt (base name; set/unique overlaid)
    ItemType    itemType = ItemType::Unknown;
    std::string type;                    // e.g. "amul", "wand", "swor"
    std::string type2;

    // Extended header (non-simple items)
    std::uint32_t fingerprint = 0;
    std::uint8_t  itemLevel   = 0;
    ItemQuality   quality     = ItemQuality::None;
    std::optional<std::uint8_t> pictureId;
    std::optional<std::uint16_t> classSpecificModInfo;

    // Quality-specific IDs
    std::vector<std::uint16_t> prefixIds;   // magic/rare/craft magic-affix IDs
    std::vector<std::uint16_t> suffixIds;
    std::uint16_t rareNameId1 = 0;          // rare prefix base
    std::uint16_t rareNameId2 = 0;          // rare suffix base
    std::uint16_t setItemId   = 0;
    std::uint16_t uniqueId    = 0;

    // Weapon/armor/misc stats
    std::uint16_t baseDefense    = 0;
    std::uint16_t maxDurability  = 0;
    std::uint16_t durability     = 0;
    std::uint16_t stacks         = 0;
    std::uint16_t maxStacks      = 0;
    // True when the `stacks` field was read from a RotW/D2R
    // material-stash slot header (either the general
    // material-stash-stack bit or the simple-item
    // `advancedStashStackable` variant). Distinguishes a legitimately
    // empty slot (`stacks == 0` == "user owns 0 of this rune/gem")
    // from a loose non-stackable item whose `stacks` field was never
    // written and stayed at the default 0 (which callers treat as 1).
    // Only meaningful when the item lives in the shared-stash
    // material tab; false everywhere else.
    bool          hasStackSlot   = false;
    std::uint8_t  cntSockets     = 0;
    std::uint8_t  cntFilledSockets = 0;
    std::uint16_t tomeId         = 0;

    std::string personalizedName;
    std::string guid;                    // formatted hex GUID for tokens

    // Chronicle (RotW). Empty when the flag isn't set or quality isn't set/unique.
    std::vector<std::uint8_t> chronicleBytes;

    // Item property list (after saveAdd normalization).
    std::vector<ItemProperty> properties;

    // Items socketed into this item (fully parsed recursively).
    std::vector<Item> socketedItems;

    // Location within the file. Useful for debugging + diff against Java parser.
    std::size_t startBitOffset = 0;
    std::size_t endBitOffset   = 0;
};

} // namespace d2r
