// Character-file model types. These mirror the shape of the Java records in
// d2rsavegameparser/model/, minus fields we can't populate yet (full item
// data, skill enum resolution, chronicle stash bindings — later phases).

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace d2r {

enum class CharacterClass : std::uint8_t {
    Amazon      = 0,
    Sorceress   = 1,
    Necromancer = 2,
    Paladin     = 3,
    Barbarian   = 4,
    Druid       = 5,
    Assassin    = 6,
    Warlock     = 7,
    Unknown     = 0xFF,
};

std::string_view toString(CharacterClass c) noexcept;

enum class Difficulty : std::uint8_t {
    Normal    = 0,
    Nightmare = 1,
    Hell      = 2,
};

struct DifficultyLocation {
    bool          active = false;
    std::uint8_t  act    = 0; // 1..5 when active
};

struct Mercenary {
    bool          alive        = false; // true when the u16 "dead" flag is 0
    std::uint32_t controlSeed  = 0;
    std::uint16_t nameId       = 0;
    std::uint16_t typeId       = 0;
    std::uint32_t experience   = 0;
};

struct CharacterAttributes {
    std::uint32_t strength        = 0;
    std::uint32_t energy          = 0;
    std::uint32_t dexterity       = 0;
    std::uint32_t vitality        = 0;
    std::uint32_t statPointsLeft  = 0;
    std::uint32_t skillPointsLeft = 0;
    std::uint64_t hp              = 0;
    std::uint64_t maxHP           = 0;
    std::uint64_t mana            = 0;
    std::uint64_t maxMana         = 0;
    std::uint64_t stamina         = 0;
    std::uint64_t maxStamina      = 0;
    std::uint32_t level           = 0;
    std::uint32_t experience      = 0;
    std::uint32_t gold            = 0;
    std::uint32_t goldInStash     = 0;
};

// Skill points allocated per class-skill slot (0..29). Value is a raw byte
// from the "if" section; interpretation as SkillType enum comes later.
struct SkillPoint {
    std::uint8_t  slot  = 0;
    std::uint8_t  level = 0;
};

// One raw item entry located but not yet decoded. Phase 5 fills in the fields.
struct ItemStub {
    std::size_t byteOffset = 0;
    std::size_t bitLength  = 0;
};

struct Character {
    // Header
    std::uint32_t version       = 0;
    std::uint32_t fileSize      = 0;
    std::uint32_t storedChecksum = 0;
    std::uint32_t computedChecksum = 0;

    // Status flags (packed byte at offset 0x14)
    bool hardcore             = false;
    bool died                 = false;
    bool lordOfDestruction    = false;
    bool reignOfTheWarlock    = false;

    std::uint8_t     actProgression = 0;
    CharacterClass   characterClass = CharacterClass::Unknown;
    std::uint8_t     level          = 0;
    std::uint32_t    timestamp      = 0; // unix seconds
    std::string      name;               // <=15 chars

    std::array<DifficultyLocation, 3> locations{};
    std::uint32_t                     mapId       = 0;

    Mercenary                mercenary{};
    CharacterAttributes      attributes{};
    std::vector<SkillPoint>  skills;              // 30 entries when present
    std::vector<ItemStub>    items;               // count from JM; details TBD
    std::uint16_t            itemCount    = 0;
    std::uint16_t            corpseItemCount = 0;
    std::uint16_t            mercItemCount   = 0;
    bool                     hasIronGolem    = false;

    // Section offsets discovered during parsing (0 if absent).
    std::size_t questsOffset       = 0;
    std::size_t waypointsOffset    = 0;
    std::size_t npcOffset          = 0;
    std::size_t statsOffset        = 0;
    std::size_t skillsOffset       = 0;
    std::size_t itemsOffset        = 0;

    // Locations of the optional item blocks that follow the character items.
    // 0 when the block is not present (or the file predates the section).
    std::size_t corpseJMOffset     = 0; // "JM" marker for corpse item list; nonzero when died
    std::size_t mercItemsJMOffset  = 0; // "JM" marker for mercenary item list (== jfOffset + 2)
    std::size_t ironGolemItemOffset = 0; // first byte of iron-golem item bitstream; 0 if no golem
};

} // namespace d2r
