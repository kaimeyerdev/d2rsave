// Character-file parser: byte-oriented walker over a full .d2s buffer.
//
// Faithful port of the Java CharacterParser in
// d2rsavegameparser/parser/CharacterParser.java. Field offsets and section
// markers come from docs/format/characterFile.md and the Java implementation.

#pragma once

#include "d2r/Character.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace d2r {

// Parse an entire save-file buffer into a Character. Throws ParseException
// (declared in BitReader.hpp) on structural failure.
[[nodiscard]] Character parseCharacter(std::span<const std::byte> bytes);

// Locate a 2-byte marker in `bytes` starting at `from`. Returns the offset
// of the first byte of the marker, or std::nullopt if not found.
[[nodiscard]] std::optional<std::size_t> findMarker(
    std::span<const std::byte> bytes, std::string_view marker, std::size_t from = 0) noexcept;

} // namespace d2r
