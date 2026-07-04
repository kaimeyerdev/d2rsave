// LSB-first bit-level reader for D2R save-file item data.
//
// Semantics match the reference Java BitReader
// (d2rsavegameparser/src/main/java/.../internal/parser/BitReader.java):
//
//   • The stream is a sequence of bits, LSB of byte 0 first.
//   • readInt(N) returns an integer whose bit 0 is the first bit read.
//   • readHuffmanEncodedString() decodes the 40-symbol D2R item-code alphabet
//     (a-z, 0-9, space) and terminates on the first decoded space.
//
// We store the buffer as std::span<const std::byte>; the reader owns no memory.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace d2r {

class ParseException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class BitReader {
public:
    explicit BitReader(std::span<const std::byte> data) noexcept
        : data_(data), positionInBits_(0) {}

    // --- position management --------------------------------------------------
    [[nodiscard]] std::size_t getPositionInBits() const noexcept { return positionInBits_; }
    [[nodiscard]] std::size_t bitsToNextBoundary() const noexcept {
        return (8 - (positionInBits_ % 8)) % 8;
    }
    void moveToNextByteBoundary() noexcept {
        positionInBits_ = (positionInBits_ + 7) & ~std::size_t{7};
    }
    void skip(std::size_t bits) noexcept { positionInBits_ += bits; }
    void revert(std::size_t bits) noexcept { positionInBits_ -= bits; }

    // --- raw reads -----------------------------------------------------------
    // Read `bits` bits (1..64) LSB-first; bit 0 of the return value is the
    // first bit read. Bits past end-of-buffer read as 0.
    std::uint64_t readBits(unsigned bits) noexcept;

    std::uint32_t readInt(unsigned bits)   noexcept { return static_cast<std::uint32_t>(readBits(bits)); }
    std::uint16_t readShort(unsigned bits) noexcept { return static_cast<std::uint16_t>(readBits(bits)); }
    std::uint8_t  readByte(unsigned bits)  noexcept { return static_cast<std::uint8_t>(readBits(bits)); }
    std::uint64_t readLong(unsigned bits)  noexcept { return readBits(bits); }
    char          readChar(unsigned bits)  noexcept { return static_cast<char>(readBits(bits)); }
    bool          readBool()               noexcept { return readBits(1) != 0; }

    // Non-advancing peek at the whole next byte after aligning to the next
    // byte boundary. Returns 0xFF (matches Java's -1 sentinel) when out of range.
    [[nodiscard]] std::uint8_t peekNextByte() const noexcept;

    // Non-advancing read of the byte containing the current bit position.
    // Returns 0 if past end-of-buffer.
    [[nodiscard]] std::uint8_t currentByte() const noexcept;

    // Non-advancing read of up to `bits` bits from the current position.
    [[nodiscard]] std::uint64_t peekNextBits(unsigned bits) const noexcept;

    // Decode a 3-4 char item code terminated by the Huffman space symbol.
    // Throws ParseException if a decode fails or the string grows unreasonably long.
    [[nodiscard]] std::string readHuffmanEncodedString();

    // Introspection: the underlying byte buffer.
    [[nodiscard]] std::span<const std::byte> data() const noexcept { return data_; }

private:
    std::span<const std::byte> data_;
    std::size_t positionInBits_;
};

} // namespace d2r
