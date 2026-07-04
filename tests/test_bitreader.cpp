// Catch2 tests for the BitReader.

#include "d2r/BitReader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using d2r::BitReader;
using d2r::ParseException;

namespace {
constexpr std::byte B(int v) noexcept { return std::byte{static_cast<unsigned char>(v)}; }

std::span<const std::byte> bytes(const std::vector<std::byte>& v) noexcept {
    return {v.data(), v.size()};
}
} // namespace

TEST_CASE("BitReader reads single bits LSB-first", "[bitreader]") {
    // Byte 0xAC = 0b1010_1100 → bits LSB→MSB: 0,0,1,1,0,1,0,1
    const std::vector<std::byte> buf{B(0xAC)};
    BitReader br(bytes(buf));

    REQUIRE(br.readBits(1) == 0);
    REQUIRE(br.readBits(1) == 0);
    REQUIRE(br.readBits(1) == 1);
    REQUIRE(br.readBits(1) == 1);
    REQUIRE(br.readBits(1) == 0);
    REQUIRE(br.readBits(1) == 1);
    REQUIRE(br.readBits(1) == 0);
    REQUIRE(br.readBits(1) == 1);
    REQUIRE(br.getPositionInBits() == 8);
}

TEST_CASE("BitReader assembles multi-bit reads LSB-first", "[bitreader]") {
    // Two bytes 0xAC 0x35. Reading 12 bits = bits 0..11 of the stream
    //   byte 0 (0xAC = 0b1010_1100), LSB→MSB: 0,0,1,1,0,1,0,1
    //   byte 1 (0x35 = 0b0011_0101), LSB→MSB: 1,0,1,0,1,1,0,0 — first 4: 1,0,1,0
    // Packed LSB-first: low byte = 0xAC, high nibble = 0b0101 = 0x5 → 0x5AC.
    const std::vector<std::byte> buf{B(0xAC), B(0x35)};
    BitReader br(bytes(buf));
    REQUIRE(br.readInt(12) == 0x5ACu);
    REQUIRE(br.getPositionInBits() == 12);
}

TEST_CASE("BitReader boundary and skip helpers", "[bitreader]") {
    const std::vector<std::byte> buf{B(0xFF), B(0x00), B(0xFF)};
    BitReader br(bytes(buf));

    REQUIRE(br.bitsToNextBoundary() == 0);
    br.skip(3);
    REQUIRE(br.getPositionInBits() == 3);
    REQUIRE(br.bitsToNextBoundary() == 5);
    br.moveToNextByteBoundary();
    REQUIRE(br.getPositionInBits() == 8);
    br.revert(2);
    REQUIRE(br.getPositionInBits() == 6);
}

TEST_CASE("BitReader peek does not advance", "[bitreader]") {
    const std::vector<std::byte> buf{B(0xAC), B(0x35), B(0x77)};
    BitReader br(bytes(buf));
    const auto before = br.getPositionInBits();
    const auto v = br.peekNextBits(16);
    REQUIRE(br.getPositionInBits() == before);
    // 16-bit LE view of {0xAC, 0x35} = 0x35AC
    REQUIRE(v == 0x35ACu);
}

TEST_CASE("BitReader Huffman decodes known item codes", "[bitreader][huffman]") {
    // Encode a code by concatenating the reference bit patterns.
    // Each pattern is written first-bit-first, then packed LSB-first into bytes.
    struct HuffPattern { std::string_view code; std::string_view bits; };

    auto encode = [](std::string_view bitStr) {
        std::vector<std::byte> out;
        std::size_t idx = 0;
        std::uint8_t cur = 0;
        for (char c : bitStr) {
            const std::uint8_t bit = (c == '1') ? 1u : 0u;
            cur |= static_cast<std::uint8_t>(bit << (idx % 8));
            ++idx;
            if ((idx % 8) == 0) {
                out.push_back(std::byte{cur});
                cur = 0;
            }
        }
        if ((idx % 8) != 0) out.push_back(std::byte{cur});
        // pad an extra byte so the reader always has something to peek into
        out.push_back(std::byte{0});
        return out;
    };

    // Build patterns matching Huffman table + trailing space terminator.
    // "cap" = c(01000) + a(11110) + p(11001) + space(10)
    {
        const auto buf = encode("01000""11110""11001""10");
        BitReader br(bytes(buf));
        REQUIRE(br.readHuffmanEncodedString() == "cap");
    }
    // "wnd" = w(00000) + n(001101) + d(110001) + space(10)
    {
        const auto buf = encode("00000""001101""110001""10");
        BitReader br(bytes(buf));
        REQUIRE(br.readHuffmanEncodedString() == "wnd");
    }
    // "hax" = h(00011) + a(11110) + x(00111) + space(10)
    {
        const auto buf = encode("00011""11110""00111""10");
        BitReader br(bytes(buf));
        REQUIRE(br.readHuffmanEncodedString() == "hax");
    }
    // "jav" — tests the longest code (j = 9 bits).
    {
        const auto buf = encode("000101110""11110""1101110""10");
        BitReader br(bytes(buf));
        REQUIRE(br.readHuffmanEncodedString() == "jav");
    }
}
