// ItemParser: direct port of the reference Java ItemParser.
//
// Overall flow per item (see also docs/format/items.md and the Java source):
//   1. Read the 32-bit flag word.
//   2. Skip 3 bits, then read location/position/y/x/container (16 bits).
//   3. If the "ear" flag is set: read class/level + up to 16 7-bit chars, align.
//   4. Otherwise Huffman-decode the 3-char item code.
//   5. If not simple: extended part 1 (fingerprint, ilvl, quality, quality-specific block, runeword + personalization).
//   6. Quest-difficulty branch (special "vip" / "ice") or GUID branch (1 bit).
//   7. If not simple: extended part 2 (durability/damage/qty, socket count, property list, sockets recurse).
//   8. Chronicle bytes for set/unique items with the chronicle flag.
//   9. Simple-item boundary fix-ups; "xyz" Potion of Life skip.
//  10. Byte-align.

#include "d2r/ItemParser.hpp"
#include "d2r/BitReader.hpp"
#include "d2r/RefDb.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace d2r {

namespace {

// Java calls isBitChecked(flags, N) where N is 1-indexed. Our BitReader packs
// the first bit read into bit 0 of the returned integer, so the 0-indexed bit
// we care about is N-1.
constexpr bool javaFlagBit(std::uint32_t flags, unsigned oneIndexed) noexcept {
    return ((flags >> (oneIndexed - 1)) & 1u) != 0;
}

// D2 property IDs used for auto-paired reads and skill encoding branches.
constexpr std::uint16_t kPropPhysMax    = 17;
constexpr std::uint16_t kPropFireMin    = 48;
constexpr std::uint16_t kPropLightMin   = 50;
constexpr std::uint16_t kPropMagicMin   = 52;
constexpr std::uint16_t kPropColdMin    = 54;
constexpr std::uint16_t kPropPoisonMin  = 57;
constexpr std::uint16_t kPropUndeadDmg  = 122;
constexpr std::uint16_t kPropSkillAttack   = 195;
constexpr std::uint16_t kPropSkillKill     = 196;
constexpr std::uint16_t kPropSkillDeath    = 197;
constexpr std::uint16_t kPropSkillHit      = 198;
constexpr std::uint16_t kPropSkillLevelUp  = 199;
constexpr std::uint16_t kPropSkillGetHit   = 201;
constexpr std::uint16_t kPropChargedSkill  = 204;
constexpr std::uint16_t kPropEnd           = 511;

bool isSkillEventProperty(std::uint16_t id) noexcept {
    switch (id) {
        case kPropSkillAttack:
        case kPropSkillKill:
        case kPropSkillDeath:
        case kPropSkillHit:
        case kPropSkillLevelUp:
        case kPropSkillGetHit:
            return true;
        default:
            return false;
    }
}

std::uint16_t readU16LE(std::span<const std::byte> b, std::size_t off) noexcept {
    if (off + 2 > b.size()) return 0;
    const auto* p = reinterpret_cast<const std::uint8_t*>(b.data()) + off;
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

ItemType determineItemType(const ArmorRow* a, const WeaponRow* w, const MiscRow* m) noexcept {
    if (a) return ItemType::Armor;
    if (w) return ItemType::Weapon;
    if (m) return ItemType::Misc;
    return ItemType::Unknown;
}

bool isJewelCode(std::string_view code) noexcept { return code == "jew"; }
bool isTome(std::string_view code)      noexcept { return code == "tbk" || code == "ibk"; }
bool isCharm(std::string_view code) noexcept {
    return code == "cm1" || code == "cm2" || code == "cm3";
}

// Parse a single item property, consuming the appropriate bit widths from `br`.
ItemProperty parseSingleProperty(BitReader& br, std::uint16_t id, std::uint8_t qflag,
                                 const RefDb& refDb) {
    const auto* isc = refDb.lookupStatCost(id);
    if (!isc) {
        throw ParseException("no ItemStatCost row for property id " + std::to_string(id));
    }
    ItemProperty p;
    p.id = id;
    p.stat = isc->stat;
    p.qualityFlag = qflag;

    const int len = isc->saveBits;
    const int add = isc->saveAdd;

    if (isSkillEventProperty(id)) {
        p.values.push_back(static_cast<std::int32_t>(br.readInt(6))  - add);
        p.values.push_back(static_cast<std::int32_t>(br.readInt(10)) - add);
        p.values.push_back(static_cast<std::int32_t>(br.readInt(len)) - add);
    } else if (id == kPropChargedSkill) {
        p.values.push_back(static_cast<std::int32_t>(br.readInt(6))  - add);
        p.values.push_back(static_cast<std::int32_t>(br.readInt(10)) - add);
        p.values.push_back(static_cast<std::int32_t>(br.readInt(8))  - add);
        p.values.push_back(static_cast<std::int32_t>(br.readInt(8))  - add);
    } else if (isc->saveParamBits >= 0) {
        p.values.push_back(static_cast<std::int32_t>(br.readInt(isc->saveParamBits)) - add);
        p.values.push_back(static_cast<std::int32_t>(br.readInt(len)) - add);
    } else {
        p.values.push_back(static_cast<std::int32_t>(br.readInt(len)) - add);
    }
    return p;
}

// Read a property list until id == 511 (or an out-of-range id sentinel).
// qflag is used to tag which set/socket bonus tier a property belongs to.
std::vector<ItemProperty> readProperties(BitReader& br, std::uint8_t qflag,
                                         const RefDb& refDb) {
    std::vector<ItemProperty> out;
    for (;;) {
        const std::uint16_t root = static_cast<std::uint16_t>(br.readInt(9));
        if (root == kPropEnd || root >= 368) break;
        out.push_back(parseSingleProperty(br, root, qflag, refDb));
        if (root == kPropPhysMax || root == kPropFireMin ||
            root == kPropLightMin || root == kPropMagicMin) {
            out.push_back(parseSingleProperty(br, static_cast<std::uint16_t>(root + 1), qflag, refDb));
        } else if (root == kPropColdMin || root == kPropPoisonMin) {
            out.push_back(parseSingleProperty(br, static_cast<std::uint16_t>(root + 1), qflag, refDb));
            out.push_back(parseSingleProperty(br, static_cast<std::uint16_t>(root + 2), qflag, refDb));
        }
    }
    return out;
}

void parseEar(BitReader& br) {
    // 3 bits class, 7 bits level, up to 16 x 7-bit ASCII chars terminated by 0.
    (void) br.readInt(3);
    (void) br.readInt(7);
    for (int i = 0; i < 16; ++i) {
        const auto c = br.readChar(7);
        if (c == 0) break;
    }
    br.moveToNextByteBoundary();
}

void parseInferior(BitReader& br) { (void) br.readInt(3); }
void parseSuperior(BitReader& br) { (void) br.readInt(3); }

void parseMagical(BitReader& br, Item& item) {
    const auto prefix = static_cast<std::uint16_t>(br.readInt(11));
    const auto suffix = static_cast<std::uint16_t>(br.readInt(11));
    if (prefix != 0) item.prefixIds.push_back(prefix);
    if (suffix != 0) item.suffixIds.push_back(suffix);
}

void parseSetItem(BitReader& br, Item& item, const RefDb& refDb) {
    item.setItemId = static_cast<std::uint16_t>(br.readInt(12));
    if (const auto* row = refDb.lookupSetItem(item.setItemId)) {
        item.itemName = row->index;
    }
}

void parseUnique(BitReader& br, Item& item, const RefDb& refDb) {
    item.uniqueId = static_cast<std::uint16_t>(br.readInt(12));
    if (const auto* row = refDb.lookupUnique(item.uniqueId)) {
        item.itemName = row->index;
    }
}

void parseRare(BitReader& br, Item& item) {
    // rare-name IDs (8 + 8 bits) then up to 3 pairs of prefix/suffix flags.
    item.rareNameId1 = static_cast<std::uint16_t>(br.readInt(8));
    item.rareNameId2 = static_cast<std::uint16_t>(br.readInt(8));
    for (int i = 0; i < 3; ++i) {
        if (br.readBool()) {
            item.prefixIds.push_back(static_cast<std::uint16_t>(br.readInt(11)));
        }
        if (br.readBool()) {
            item.suffixIds.push_back(static_cast<std::uint16_t>(br.readInt(11)));
        }
    }
}

void personalize(BitReader& br, Item& item) {
    std::string name;
    for (int i = 0; i < 16; ++i) {
        const auto c = br.readChar(8);
        if (c == 0) break;
        name.push_back(c);
    }
    item.personalizedName = std::move(name);
}

// Runes/gems/amulets/rings/charms include a 128-bit GUID after the extended
// header. See parseGUID in the Java parser.
void parseGuid(BitReader& br, Item& item, const std::string& code,
               const MiscRow* misc, const ArmorRow* armor, const WeaponRow* weapon) {
    const auto& type  = armor ? armor->type
                     : weapon ? weapon->type
                     : misc   ? misc->type   : std::string();
    const bool needsGuid = misc == nullptr
        || type == "rune" || type.starts_with("gem")
        || type.starts_with("amu") || type.starts_with("rin")
        || isCharm(code);
    if (needsGuid) {
        char buf[64];
        const auto a = br.readInt(32);
        const auto b = br.readInt(32);
        const auto c = br.readInt(32);
        const auto d = br.readInt(32);
        std::snprintf(buf, sizeof(buf), "0x%x 0x%x 0x%x 0x%x", a, b, c, d);
        item.guid = buf;
    } else if (code != "bks") {
        // Preserve alignment for edge cases such as Scroll of Inifuss.
        br.skip(3);
    }
}

void parseArmorStats(BitReader& br, Item& item) {
    item.baseDefense = static_cast<std::uint16_t>(br.readInt(11)) - 10;
    const auto maxDur = static_cast<std::uint16_t>(br.readInt(8));
    item.maxDurability = maxDur;
    if (maxDur != 0) {
        item.durability = static_cast<std::uint16_t>(br.readInt(9));
    }
}

void parseWeaponStats(BitReader& br, Item& item, const WeaponRow& w) {
    const auto maxDur = static_cast<std::uint16_t>(br.readInt(8));
    item.maxDurability = maxDur;
    if (maxDur != 0) {
        item.durability = static_cast<std::uint16_t>(br.readInt(9));
    }
    if (w.maxStack > 0) {
        br.skip(1);
        item.stacks = static_cast<std::uint16_t>(br.readInt(9));
        item.maxStacks = static_cast<std::uint16_t>(w.maxStack);
    }
}

void parseMiscStats(BitReader& br, Item& item, const MiscRow& m) {
    if (m.maxStack > 0) {
        br.skip(1);
        item.stacks = static_cast<std::uint16_t>(br.readInt(9));
        item.maxStacks = static_cast<std::uint16_t>(m.maxStack);
    }
}

void readChronicleBytes(BitReader& br, Item& item) {
    if (!item.hasChronicleData) return;
    if (item.quality != ItemQuality::Set && item.quality != ItemQuality::Unique) return;
    std::array<std::uint8_t, 7> bytes{};
    bytes[0] = br.readByte(8);
    bytes[1] = br.readByte(8);
    bytes[2] = br.readByte(8);
    bytes[3] = br.readByte(8);
    const auto byte45 = static_cast<std::uint16_t>(br.peekNextBits(16));
    bytes[4] = br.readByte(8);
    std::size_t used = 5;
    if (byte45 == 0) {
        const auto bits = br.bitsToNextBoundary() == 0 ? 8u : (8u - br.bitsToNextBoundary());
        bytes[5] = br.readByte(static_cast<unsigned>(bits));
        used = 6;
    } else {
        bytes[5] = br.readByte(8);
        const auto bits = br.bitsToNextBoundary() == 0 ? 8u : (8u - br.bitsToNextBoundary());
        bytes[6] = br.readByte(static_cast<unsigned>(bits));
        used = 7;
    }
    item.chronicleBytes.assign(bytes.begin(), bytes.begin() + used);
}

} // namespace

std::string_view toString(ItemQuality q) noexcept {
    switch (q) {
        case ItemQuality::Inferior: return "inferior";
        case ItemQuality::Normal:   return "normal";
        case ItemQuality::Superior: return "superior";
        case ItemQuality::Magic:    return "magic";
        case ItemQuality::Set:      return "set";
        case ItemQuality::Rare:     return "rare";
        case ItemQuality::Unique:   return "unique";
        case ItemQuality::Craft:    return "crafted";
        case ItemQuality::None:     return "none";
        default:                    return "unknown";
    }
}

std::vector<Item> ItemParser::parseItems(std::span<const std::byte> bytes,
                                          std::size_t sectionOffset,
                                          std::size_t* failedItemIndex,
                                          std::string* failureMessage) {    if (sectionOffset + 4 > bytes.size()) return {};
    if (bytes[sectionOffset]     != std::byte{'J'} ||
        bytes[sectionOffset + 1] != std::byte{'M'}) {
        throw ParseException("expected JM item marker");
    }
    const auto count = readU16LE(bytes, sectionOffset + 2);

    std::vector<Item> result;
    result.reserve(count);
    const auto payload = bytes.subspan(sectionOffset + 4);
    BitReader br(payload);
    for (std::uint16_t i = 0; i < count; ++i) {
        try {
            result.push_back(parseItem(br));
        } catch (const ParseException& ex) {
            if (failedItemIndex) *failedItemIndex = i;
            if (failureMessage)  *failureMessage  = ex.what();
            return result;
        }
    }
    return result;
}

Item ItemParser::parseSingleItem(std::span<const std::byte> bytes,
                                  std::size_t byteOffset) {
    if (byteOffset >= bytes.size()) throw ParseException("parseSingleItem: past EOF");
    BitReader br(bytes.subspan(byteOffset));
    return parseItem(br);
}

Item ItemParser::parseItem(BitReader& br) {
    Item item;
    item.startBitOffset = br.getPositionInBits();

    // 1. Flag word (32 bits).
    const auto flags = br.readInt(32);
    item.identified       = javaFlagBit(flags, 5);
    item.socketed         = javaFlagBit(flags, 12);
    item.ear              = javaFlagBit(flags, 17);
    item.simple           = javaFlagBit(flags, 22);
    item.ethereal         = javaFlagBit(flags, 23);
    item.personalized     = javaFlagBit(flags, 25);
    item.runeword         = javaFlagBit(flags, 27);
    item.hasChronicleData = javaFlagBit(flags, 29);

    // 2. Skip 3, then location block.
    br.skip(3);
    item.location  = static_cast<std::uint8_t>(br.readInt(3));
    item.position  = static_cast<std::uint8_t>(br.readInt(4));
    item.y         = static_cast<std::uint8_t>(br.readInt(4));
    item.x         = static_cast<std::uint8_t>(br.readInt(4));
    item.container = static_cast<std::uint8_t>(br.readInt(3));

    // 3. Ear items follow a completely different layout after this point.
    if (item.ear) {
        parseEar(br);
        item.endBitOffset = br.getPositionInBits();
        return item;
    }

    // 4. Item code (Huffman).
    item.code = br.readHuffmanEncodedString();

    const auto* armor  = refDb_.lookupArmor(item.code);
    const auto* weapon = refDb_.lookupWeapon(item.code);
    const auto* misc   = refDb_.lookupMisc(item.code);
    item.itemType = determineItemType(armor, weapon, misc);
    if (armor) {
        item.itemName = armor->name;  item.type = armor->type;  item.type2 = armor->type2;
    } else if (weapon) {
        item.itemName = weapon->name; item.type = weapon->type; item.type2 = weapon->type2;
    } else if (misc) {
        item.itemName = misc->name;   item.type = misc->type;   item.type2 = misc->type2;
    }

    // 5. Extended part 1.
    if (!item.simple) {
        item.cntFilledSockets = static_cast<std::uint8_t>(br.readInt(3));
        item.fingerprint      = br.readInt(32);
        item.itemLevel        = static_cast<std::uint8_t>(br.readInt(7));
        item.quality          = static_cast<ItemQuality>(br.readInt(4));

        if (br.readBool()) {
            item.pictureId = static_cast<std::uint8_t>(br.readInt(3));
        }
        if (br.readBool()) {
            item.classSpecificModInfo = static_cast<std::uint16_t>(br.readInt(11));
        }

        switch (item.quality) {
            case ItemQuality::Inferior: parseInferior(br); break;
            case ItemQuality::Superior: parseSuperior(br); break;
            case ItemQuality::Normal:
                if (isTome(item.code)) item.tomeId = static_cast<std::uint16_t>(br.readInt(5));
                break;
            case ItemQuality::Magic: parseMagical(br, item);           break;
            case ItemQuality::Set:   parseSetItem(br, item, refDb_);   break;
            case ItemQuality::Rare:
            case ItemQuality::Craft: parseRare(br, item);              break;
            case ItemQuality::Unique: parseUnique(br, item, refDb_);   break;
            default:
                throw ParseException("unknown item quality at bit " +
                                     std::to_string(br.getPositionInBits()));
        }

        if (item.runeword) {
            br.skip(12);
            br.skip(4);
        }
        if (item.personalized) {
            personalize(br, item);
        }
    }

    // 6. Quest-difficulty branch or GUID branch.
    const bool questDiff = (armor && armor->questDiffCheck) ||
                           (weapon && weapon->questDiffCheck) ||
                           (misc && misc->questDiffCheck);
    if (questDiff) {
        if (item.code == "vip" || item.code == "ice") {
            br.revert(2);
        }
        (void) br.readInt(3); // questDifficulty byte, unused here
    } else if (br.readBool()) {
        parseGuid(br, item, item.code, misc, armor, weapon);
    }

    // 7. Extended part 2 (non-simple only).
    if (!item.simple) {
        switch (item.itemType) {
            case ItemType::Armor:  parseArmorStats(br, item);              break;
            case ItemType::Weapon: if (weapon) parseWeaponStats(br, item, *weapon); break;
            case ItemType::Misc:   if (misc)   parseMiscStats(br, item, *misc);    break;
            default:
                throw ParseException("unknown item type for code '" + item.code + "'");
        }

        // RotW alignment quirk: for non-stackable items an extra padding bit
        // appears after the type block (unless the code is the special Potion
        // of Life).
        const int maxStacks = weapon ? weapon->maxStack
                            : misc   ? misc->maxStack : 0;
        if (maxStacks == 0 && item.code != "xyz") br.skip(1);

        if (item.socketed) {
            item.cntSockets = static_cast<std::uint8_t>(br.readInt(4));
        }

        int lSet[5] = {0, 0, 0, 0, 0};
        if (item.quality == ItemQuality::Set) {
            for (int i = 0; i < 5; ++i) lSet[i] = static_cast<int>(br.readInt(1));
        }

        const std::uint8_t primaryQflag = isJewelCode(item.code) ? 1 : 0;
        auto primary = readProperties(br, primaryQflag, refDb_);
        item.properties.insert(item.properties.end(),
                               std::make_move_iterator(primary.begin()),
                               std::make_move_iterator(primary.end()));

        if (item.quality == ItemQuality::Set) {
            for (int i = 0; i < 5; ++i) {
                if (lSet[i] == 1) {
                    auto extra = readProperties(br, static_cast<std::uint8_t>(i + 2), refDb_);
                    item.properties.insert(item.properties.end(),
                                           std::make_move_iterator(extra.begin()),
                                           std::make_move_iterator(extra.end()));
                }
            }
        }
        if (item.runeword) {
            auto rw = readProperties(br, 0, refDb_);
            item.properties.insert(item.properties.end(),
                                   std::make_move_iterator(rw.begin()),
                                   std::make_move_iterator(rw.end()));
        }

        // RotW material-stash stack count.
        if (br.readBool()) {
            item.stacks = br.readByte(8);
        }

        // Socketed sub-items recurse via parseItem().
        if (item.cntFilledSockets > 0) {
            br.moveToNextByteBoundary();
            for (int i = 0; i < item.cntFilledSockets; ++i) {
                item.socketedItems.push_back(parseItem(br));
            }
        }
    }

    // 8. Chronicle blob for RotW set/unique items.
    readChronicleBytes(br, item);

    // 9. Simple-item RotW alignment fix-ups + special cases.
    if (item.simple) {
        if (misc && misc->advancedStashStackable) {
            const bool hasData = br.readBool();
            if (hasData) item.stacks = br.readByte(8);
        }
        const auto peek = br.peekNextByte();
        if (br.bitsToNextBoundary() == 0 && peek != 16 &&
            (br.peekNextBits(16) != 0 || br.peekNextBits(24) == 0)) {
            br.skip(1);
        }
        if (peek == 0 && br.currentByte() != 0 &&
            br.peekNextBits(16) != 0) {
            br.skip(8);
        }
    } else if (item.code == "xyz") {
        br.skip(16);
    }

    // 10. Byte alignment before the next item.
    br.moveToNextByteBoundary();
    item.endBitOffset = br.getPositionInBits();
    return item;
}

} // namespace d2r
