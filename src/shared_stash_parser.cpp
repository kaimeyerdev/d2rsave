#include "d2r/SharedStashParser.hpp"

#include "d2r/BitReader.hpp"
#include "d2r/ItemParser.hpp"
#include "d2r/RefDb.hpp"
#include "d2r/Save.hpp"

#include <algorithm>
#include <cstring>

namespace d2r {

namespace {

constexpr std::array<std::uint8_t, 4> kStashMagic{{0x55, 0xAA, 0x55, 0xAA}};

std::uint16_t u16LE(std::span<const std::byte> b, std::size_t off) noexcept {
    if (off + 2 > b.size()) return 0;
    const auto* p = reinterpret_cast<const std::uint8_t*>(b.data()) + off;
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

StashTab parseHeader(std::span<const std::byte> bytes, std::size_t index) {
    // Java's parseHeader reads 20 header bytes, skips 64 bits (i.e. the first
    // 8 bytes, comprising the 4-byte magic and 4 unknown bytes), then reads
    // three little-endian u32s: version, gold, lengthInBytes.
    StashTab tab;
    tab.fileOffset  = index;
    tab.version     = readU32LE(bytes, index + 8);
    tab.gold        = readU32LE(bytes, index + 12);
    tab.lengthBytes = readU32LE(bytes, index + 16);
    if (tab.version != 105) {
        throw ParseException("unsupported shared-stash version " +
                             std::to_string(tab.version));
    }
    return tab;
}

ChronicleTab parseChronicleAt(std::span<const std::byte> bytes, std::size_t index) {
    ChronicleTab out;

    // Counts live at index + 70, packed as three little-endian u16s.
    const auto cntSet   = u16LE(bytes, index + 70);
    const auto cntUniq  = u16LE(bytes, index + 72);
    const auto cntRune  = u16LE(bytes, index + 74);

    const std::size_t total      = cntSet + cntUniq + cntRune;
    const std::size_t bytesStart = index + 84;
    const std::size_t bytesLen   = total * 10; // 10 bytes per entry
    if (bytesStart + bytesLen > bytes.size()) {
        throw ParseException("shared stash: chronicle payload truncated");
    }

    BitReader br(bytes.subspan(bytesStart, bytesLen));
    auto readEntry = [&](ItemQuality q) {
        ChronicleEntry e;
        e.itemId           = br.readInt(32);
        e.monsterId        = br.readShort(16);
        e.timestampMinutes = br.readInt(32);
        e.quality          = q;
        return e;
    };

    out.setItems.reserve(cntSet);
    for (std::size_t i = 0; i < cntSet;  ++i) out.setItems.push_back(readEntry(ItemQuality::Set));
    out.uniques.reserve(cntUniq);
    for (std::size_t i = 0; i < cntUniq; ++i) out.uniques.push_back(readEntry(ItemQuality::Unique));
    out.runewords.reserve(cntRune);
    for (std::size_t i = 0; i < cntRune; ++i) out.runewords.push_back(readEntry(ItemQuality::Normal));
    return out;
}

} // namespace

std::vector<std::size_t> SharedStashParser::findTabOffsets(std::span<const std::byte> bytes) noexcept {
    std::vector<std::size_t> out;
    if (bytes.size() < kStashMagic.size()) return out;
    const std::size_t end = bytes.size() - kStashMagic.size();
    for (std::size_t i = 0; i <= end; ++i) {
        if (std::memcmp(bytes.data() + i, kStashMagic.data(), kStashMagic.size()) == 0) {
            out.push_back(i);
            i += kStashMagic.size() - 1;
        }
    }
    return out;
}

SharedStash SharedStashParser::parse(std::span<const std::byte> bytes) {
    const auto offsets = findTabOffsets(bytes);
    SharedStash out;

    ItemParser items(refDb_);
    auto parseStorageTab = [&](std::size_t index) {
        auto tab = parseHeader(bytes, index);
        // Items live between +64 and +lengthBytes for this tab.
        const std::size_t jmOffset = index + 64;
        if (jmOffset + 4 <= bytes.size() &&
            bytes[jmOffset]     == std::byte{'J'} &&
            bytes[jmOffset + 1] == std::byte{'M'}) {
            const std::size_t tabEnd = std::min(index + tab.lengthBytes, bytes.size());
            tab.items = items.parseItems(bytes.subspan(0, tabEnd), jmOffset);
        }
        return tab;
    };

    if (offsets.size() == 3) {
        for (std::size_t off : offsets) out.tabs.push_back(parseStorageTab(off));
    } else if (offsets.size() == 7) {
        for (std::size_t i = 0; i < 6; ++i) {
            out.tabs.push_back(parseStorageTab(offsets[i]));
        }
        (void) parseHeader(bytes, offsets.back()); // version check
        out.chronicle = parseChronicleAt(bytes, offsets.back());
    } else {
        throw ParseException("shared stash: expected 3 or 7 tabs, found " +
                             std::to_string(offsets.size()));
    }

    return out;
}

ChronicleTab SharedStashParser::parseChronicleOnly(std::span<const std::byte> bytes) {
    const auto offsets = findTabOffsets(bytes);
    if (offsets.size() != 7) {
        throw ParseException("shared stash: chronicle tab requires 7-tab RotW file, found " +
                             std::to_string(offsets.size()));
    }
    (void) parseHeader(bytes, offsets.back()); // version check
    return parseChronicleAt(bytes, offsets.back());
}

} // namespace d2r
