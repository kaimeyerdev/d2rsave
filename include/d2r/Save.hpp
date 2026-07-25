// D2R .d2s save-file utilities: checksum, character-name field, and map-seed field.
//
// Byte offsets and the checksum algorithm are taken from the legacy
// `legacy/rename_d2r.c` tool and the on-disk layout documented in the sibling
// Java project (see docs/format/characterFile.md).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace d2r {

// On-disk layout constants (little-endian throughout the file).
inline constexpr std::size_t kMagicOffset      = 0x00;
inline constexpr std::uint32_t kMagic          = 0xAA55AA55u;  // stored LE at offset 0
inline constexpr std::size_t kChecksumOffset   = 0x0C;
inline constexpr std::size_t kChecksumSize     = 4;
inline constexpr std::size_t kMapSeedOffset    = 0x9B;
inline constexpr std::size_t kMapSeedSize      = 4;
inline constexpr std::size_t kDifficultyOffset = 0x98;  // 3 bytes: Normal, Nightmare, Hell
inline constexpr std::size_t kDifficultySize   = 3;
inline constexpr std::size_t kNameOffset       = 0x12B;
inline constexpr std::size_t kNameSize         = 16;   // null-padded; max 15 printable chars

// Rotate a 32-bit value left by one bit (matches D2's checksum step).
[[nodiscard]] constexpr std::uint32_t rol32_1(std::uint32_t value) noexcept {
    return (value << 1) | (value >> 31);
}

// Compute the D2R save-file checksum over the given byte range. Bytes 12..15
// (the checksum field itself) are treated as zero during the sum.
[[nodiscard]] std::uint32_t computeChecksum(std::span<const std::byte> bytes) noexcept;

// Read the little-endian u32 stored at the given offset.
[[nodiscard]] std::uint32_t readU32LE(std::span<const std::byte> bytes, std::size_t offset) noexcept;

// Write a u32 in little-endian at the given offset.
void writeU32LE(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) noexcept;

// Load an entire file into memory. Throws std::system_error on failure.
[[nodiscard]] std::vector<std::byte> readFile(const std::filesystem::path& path);

// Overwrite a file with the given bytes atomically-ish (write + rename).
// Throws std::system_error on failure.
void writeFileAtomic(const std::filesystem::path& path, std::span<const std::byte> bytes);

// Validate the magic bytes at the start of `bytes`. Returns false if too short
// or the magic doesn't match.
[[nodiscard]] bool hasValidMagic(std::span<const std::byte> bytes) noexcept;

// Extract the character name (stops at the first NUL, or the full 15 chars).
[[nodiscard]] std::string readCharacterName(std::span<const std::byte> bytes);

// Overwrite the 16-byte name field. `newName` must be at most 15 chars and
// contain only bytes the game accepts (ASCII, no control chars); returns
// false if validation fails.
[[nodiscard]] bool writeCharacterName(std::span<std::byte> bytes, std::string_view newName);

// Convenience: read/write map seed as u32 LE.
[[nodiscard]] std::uint32_t readMapSeed(std::span<const std::byte> bytes) noexcept;
void writeMapSeed(std::span<std::byte> bytes, std::uint32_t seed) noexcept;

// Read the index (0=Normal, 1=Nightmare, 2=Hell) of the currently active
// difficulty, or std::nullopt if none of the three location bytes has the
// 0x80 flag set (e.g. brand-new characters).
[[nodiscard]] std::optional<std::uint8_t> readActiveDifficulty(std::span<const std::byte> bytes) noexcept;

// Mark the given difficulty (0=Normal, 1=Nightmare, 2=Hell) as active. The
// two other difficulty bytes have their high (active) bit cleared but keep
// their low act bits; the target byte keeps its existing act (or defaults
// to act 1 when it was blank). No-op if `difficulty` >= 3.
void writeActiveDifficulty(std::span<std::byte> bytes, std::uint8_t difficulty) noexcept;

// Read the stored checksum as u32 LE.
[[nodiscard]] std::uint32_t readStoredChecksum(std::span<const std::byte> bytes) noexcept;

// Recompute the checksum and write it back at offset 0x0C. Returns the new value.
std::uint32_t recomputeAndWriteChecksum(std::span<std::byte> bytes) noexcept;

} // namespace d2r
