#include "d2r/CharacterParser.hpp"
#include "d2r/BitReader.hpp"
#include "d2r/Save.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace d2r {

namespace {

// Offsets from the Java parser and docs/format/characterFile.md.
constexpr std::size_t kStatusOffset          = 0x14;   // 20
constexpr std::size_t kProgressionOffset     = 0x15;   // 21
constexpr std::size_t kClassOffset           = 0x18;   // 24
constexpr std::size_t kLevelOffset           = 0x1B;   // 27
constexpr std::size_t kTimestampOffset       = 0x20;   // 32
constexpr std::size_t kDifficultyOffset      = 0x98;   // 152 (3 bytes)
constexpr std::size_t kMercAliveOffset       = 0xA1;   // 161 u16
constexpr std::size_t kMercIdOffset          = 0xA3;   // 163 u32
constexpr std::size_t kMercNameIdOffset      = 0xA7;   // 167 u16
constexpr std::size_t kMercTypeIdOffset      = 0xA9;   // 169 u16
constexpr std::size_t kMercExpOffset         = 0xAB;   // 171 u32
constexpr std::size_t kExpansionByteOffset   = 0xF8;   // 248
constexpr std::size_t kQuestsStart           = 0x193;  // 403; "Woo!" at this offset if present.

constexpr std::uint8_t kStatusHardcore  = 0x04;
constexpr std::uint8_t kStatusDied      = 0x08;
constexpr std::uint8_t kStatusLoD       = 0x20;

std::uint8_t u8At(std::span<const std::byte> b, std::size_t off) noexcept {
    return off < b.size() ? static_cast<std::uint8_t>(b[off]) : 0u;
}
std::uint16_t u16LE(std::span<const std::byte> b, std::size_t off) noexcept {
    if (off + 2 > b.size()) return 0;
    const auto* p = reinterpret_cast<const std::uint8_t*>(b.data()) + off;
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

// The Java AttributeParser reads a variable-length bitstream: 9-bit id, then
// N-bit value (width from the table below). Terminator is id == 0x1FF. Bit
// ordering matches BitReader's LSB-first semantics.
struct AttrDef {
    std::uint8_t bits;
    std::uint8_t coefficient;
};
constexpr std::array<AttrDef, 16> kAttrDefs{{
    {10, 1},   // 0  strength
    {10, 1},   // 1  energy
    {10, 1},   // 2  dexterity
    {10, 1},   // 3  vitality
    {10, 1},   // 4  stat points left
    { 8, 1},   // 5  skill points left
    {21, 255}, // 6  hp
    {21, 255}, // 7  max hp
    {21, 255}, // 8  mana
    {21, 255}, // 9  max mana
    {21, 255}, // 10 stamina
    {21, 255}, // 11 max stamina
    { 7, 1},   // 12 level
    {32, 1},   // 13 experience
    {25, 1},   // 14 gold in inventory
    {25, 1},   // 15 gold in stash
}};

// Skill section: 30-byte block after the 'if' marker.
constexpr std::size_t kSkillsBlockLen = 30;

void parseAttributes(std::span<const std::byte> region, CharacterAttributes& out) {
    BitReader br(region);
    for (;;) {
        const auto id = br.readInt(9);
        if (id == 0x1FF) return;
        if (id >= kAttrDefs.size()) {
            throw ParseException("attribute id out of range");
        }
        const auto def = kAttrDefs[id];
        const std::uint64_t raw = br.readBits(def.bits);
        const std::uint64_t value = raw / def.coefficient;
        switch (id) {
            case  0: out.strength        = static_cast<std::uint32_t>(value); break;
            case  1: out.energy          = static_cast<std::uint32_t>(value); break;
            case  2: out.dexterity       = static_cast<std::uint32_t>(value); break;
            case  3: out.vitality        = static_cast<std::uint32_t>(value); break;
            case  4: out.statPointsLeft  = static_cast<std::uint32_t>(value); break;
            case  5: out.skillPointsLeft = static_cast<std::uint32_t>(value); break;
            case  6: out.hp              = value; break;
            case  7: out.maxHP           = value; break;
            case  8: out.mana            = value; break;
            case  9: out.maxMana         = value; break;
            case 10: out.stamina         = value; break;
            case 11: out.maxStamina      = value; break;
            case 12: out.level           = static_cast<std::uint32_t>(value); break;
            case 13: out.experience      = static_cast<std::uint32_t>(value); break;
            case 14: out.gold            = static_cast<std::uint32_t>(value); break;
            case 15: out.goldInStash     = static_cast<std::uint32_t>(value); break;
        }
    }
}

} // namespace

std::string_view toString(CharacterClass c) noexcept {
    switch (c) {
        case CharacterClass::Amazon:      return "Amazon";
        case CharacterClass::Sorceress:   return "Sorceress";
        case CharacterClass::Necromancer: return "Necromancer";
        case CharacterClass::Paladin:     return "Paladin";
        case CharacterClass::Barbarian:   return "Barbarian";
        case CharacterClass::Druid:       return "Druid";
        case CharacterClass::Assassin:    return "Assassin";
        case CharacterClass::Warlock:     return "Warlock";
        default:                          return "Unknown";
    }
}

std::optional<std::size_t> findMarker(std::span<const std::byte> bytes,
                                      std::string_view marker,
                                      std::size_t from) noexcept {
    if (marker.empty() || bytes.size() < marker.size()) return std::nullopt;
    const auto* raw = reinterpret_cast<const char*>(bytes.data());
    const std::size_t end = bytes.size() - marker.size();
    for (std::size_t i = from; i <= end; ++i) {
        if (std::memcmp(raw + i, marker.data(), marker.size()) == 0) {
            return i;
        }
    }
    return std::nullopt;
}

Character parseCharacter(std::span<const std::byte> bytes) {
    if (!hasValidMagic(bytes)) throw ParseException("invalid magic");
    if (bytes.size() < kNameOffset + kNameSize) throw ParseException("file too short");

    Character c;
    c.version          = readU32LE(bytes, 4);
    c.fileSize         = readU32LE(bytes, 8);
    c.storedChecksum   = readStoredChecksum(bytes);
    c.computedChecksum = computeChecksum(bytes);

    const auto status = u8At(bytes, kStatusOffset);
    c.hardcore          = (status & kStatusHardcore) != 0;
    c.died              = (status & kStatusDied)     != 0;
    c.lordOfDestruction = (status & kStatusLoD)      != 0;

    c.actProgression = u8At(bytes, kProgressionOffset);
    const auto classByte = u8At(bytes, kClassOffset);
    c.characterClass = (classByte <= 7)
        ? static_cast<CharacterClass>(classByte)
        : CharacterClass::Unknown;
    c.level     = u8At(bytes, kLevelOffset);
    c.timestamp = readU32LE(bytes, kTimestampOffset);
    c.name      = readCharacterName(bytes);
    c.mapId     = readMapSeed(bytes);

    for (std::size_t i = 0; i < 3; ++i) {
        const auto b = u8At(bytes, kDifficultyOffset + i);
        c.locations[i].active = (b & 0x80) != 0;
        c.locations[i].act    = static_cast<std::uint8_t>((b & 0x07) + 1);
    }

    c.mercenary.alive       = u16LE(bytes, kMercAliveOffset) == 0;
    c.mercenary.controlSeed = readU32LE(bytes, kMercIdOffset);
    c.mercenary.nameId      = u16LE(bytes, kMercNameIdOffset);
    c.mercenary.typeId      = u16LE(bytes, kMercTypeIdOffset);
    c.mercenary.experience  = readU32LE(bytes, kMercExpOffset);

    const auto expansionByte = u8At(bytes, kExpansionByteOffset);
    c.reignOfTheWarlock = (expansionByte == 3);

    // Freshly rolled characters stop at 403 bytes (no quests/attributes/items).
    if (bytes.size() <= kQuestsStart) return c;

    // Section markers ("Woo!", "WS", "w4", "gf", "if", "JM"). We locate each
    // in order rather than trusting hard-coded offsets, matching Java.
    if (auto off = findMarker(bytes, "Woo!", kQuestsStart))            c.questsOffset    = *off;
    const std::size_t afterQuests = c.questsOffset ? c.questsOffset : kQuestsStart;
    if (auto off = findMarker(bytes, "WS", afterQuests))               c.waypointsOffset = *off;
    if (auto off = findMarker(bytes, "w4", c.waypointsOffset))         c.npcOffset       = *off;
    const std::size_t afterNpc = c.npcOffset ? c.npcOffset : c.waypointsOffset;
    if (auto off = findMarker(bytes, "gf", afterNpc))                  c.statsOffset     = *off;
    if (auto off = findMarker(bytes, "if", c.statsOffset))             c.skillsOffset    = *off;
    if (auto off = findMarker(bytes, "JM", c.skillsOffset))            c.itemsOffset     = *off;

    // Attributes: bit-packed region between "gf" (+2 header bytes) and "if".
    if (c.statsOffset && c.skillsOffset && c.skillsOffset > c.statsOffset + 2) {
        const auto begin = c.statsOffset + 2;
        const auto end   = c.skillsOffset;
        parseAttributes(bytes.subspan(begin, end - begin), c.attributes);
    }

    // Skills: 30-byte block starting right after the "if" marker.
    if (c.skillsOffset && c.skillsOffset + 2 + kSkillsBlockLen <= bytes.size()) {
        c.skills.reserve(kSkillsBlockLen);
        for (std::size_t i = 0; i < kSkillsBlockLen; ++i) {
            c.skills.push_back(SkillPoint{
                .slot  = static_cast<std::uint8_t>(i),
                .level = u8At(bytes, c.skillsOffset + 2 + i),
            });
        }
    }

    // Items: "JM" + u16 count. Actual per-item bit parsing is Phase 5.
    if (c.itemsOffset && c.itemsOffset + 4 <= bytes.size()) {
        c.itemCount = u16LE(bytes, c.itemsOffset + 2);
    }

    // Post-items sections follow the Java CharacterParser layout:
    //   1) Immediately after the character-items JM we find the corpse JM.
    //      Its u16 count is 0 when alive, 1 when dead (with the corpse item
    //      block starting 16 bytes later).
    //   2) For LoD/RotW files an iron-golem "kf" marker sits at the end of
    //      the file. Search backwards from EOF for it.
    //   3) Mercenary items live between the corpse JM and "kf" behind a "jf"
    //      marker. `jf` is followed 2 bytes later by the nested "JM" header.
    //
    // The merc/golem sections only exist for Expansion (LoD/RotW) saves; the
    // Java parser gates on that flag. `jf`/`kf` byte pairs can also occur
    // legitimately inside item bit-streams, so we further require the marker
    // to sit right beside its expected companion (`jf` must be followed by
    // `JM`; `kf` is nailed down by an in-range 0/1 hasGolem flag).
    if (c.itemsOffset && (c.lordOfDestruction || c.reignOfTheWarlock)) {
        if (auto off = findMarker(bytes, "JM", c.itemsOffset + 2)) {
            c.corpseJMOffset    = *off;
            c.corpseItemCount   = u16LE(bytes, *off + 2);
        }
        // Search backwards for "kf" (iron golem marker).
        for (std::size_t i = bytes.size() >= 3 ? bytes.size() - 3 : 0;
             i > c.corpseJMOffset; --i) {
            if (bytes[i] != std::byte{'k'} || bytes[i + 1] != std::byte{'f'}) continue;
            const auto flag = u8At(bytes, i + 2);
            if (flag != 0 && flag != 1) continue; // false positive
            c.hasIronGolem = flag != 0;
            if (c.hasIronGolem) c.ironGolemItemOffset = i + 3;
            // Search backwards from here for "jf"+"JM".
            for (std::size_t j = i; j > c.corpseJMOffset; --j) {
                if (bytes[j]     != std::byte{'j'} ||
                    bytes[j + 1] != std::byte{'f'} ||
                    bytes[j + 2] != std::byte{'J'} ||
                    bytes[j + 3] != std::byte{'M'}) continue;
                c.mercItemsJMOffset = j + 2;
                c.mercItemCount     = u16LE(bytes, c.mercItemsJMOffset + 2);
                break;
            }
            break;
        }
    }

    return c;
}

} // namespace d2r
