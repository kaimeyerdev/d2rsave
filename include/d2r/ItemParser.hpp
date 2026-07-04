// ItemParser: bit-level decoder for D2R item bytes.
//
// Faithful port of d2rsavegameparser/src/main/java/.../parser/ItemParser.java.
// Uses BitReader for LSB-first bit reads and RefDb for txt-table lookups
// (armor/weapons/misc/itemstatcost/uniqueitems/setitems). Requires the item
// tables to be loaded via RefDb::loadItemTables() before parsing.

#pragma once

#include "d2r/Item.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace d2r {

class BitReader;
class RefDb;

class ItemParser {
public:
    // Owns no state beyond references. RefDb must outlive the parser and
    // must already have called loadItemTables().
    explicit ItemParser(const RefDb& refDb) noexcept : refDb_(refDb) {}

    // Parse an item list of the form: "JM" + u16 count + bit-packed items.
    // `bytes` should span the entire save file; `sectionOffset` points at
    // the "JM" byte. Reads through the end of the buffer. On a parse failure
    // returns the items decoded so far and writes the position of the failing
    // item (0-indexed) plus the exception message via the out-params.
    [[nodiscard]] std::vector<Item> parseItems(
        std::span<const std::byte> bytes,
        std::size_t sectionOffset,
        std::size_t* failedItemIndex = nullptr,
        std::string* failureMessage  = nullptr);

    // Parse a single bit-packed item that lives at a byte offset without a
    // "JM" wrapper (iron-golem item after the "kf" marker).
    [[nodiscard]] Item parseSingleItem(
        std::span<const std::byte> bytes, std::size_t byteOffset);

    // Parse a single item at the current bit position. Recurses for
    // socketed sub-items.
    [[nodiscard]] Item parseItem(BitReader& br);

private:
    const RefDb& refDb_;
};

} // namespace d2r
