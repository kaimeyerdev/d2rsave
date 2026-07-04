#include "d2r/BitReader.hpp"

#include <array>
#include <string_view>

namespace d2r {

namespace {

// D2R Huffman dictionary: bit pattern (first char = first bit read, LSB-first
// bit index 0) → symbol. Copied verbatim from the reference Java BitReader.
// 40 entries: a-z, 0-9, space. Space terminates a decoded item code.
struct HuffEntry { std::string_view bits; char sym; };
constexpr std::array<HuffEntry, 40> kHuff{{
    {"11110",     'a'}, {"0101",      'b'}, {"01000",     'c'},
    {"110001",    'd'}, {"110000",    'e'}, {"010011",    'f'},
    {"11010",     'g'}, {"00011",     'h'}, {"1111110",   'i'},
    {"000101110", 'j'}, {"010010",    'k'}, {"11101",     'l'},
    {"01101",     'm'}, {"001101",    'n'}, {"1111111",   'o'},
    {"11011001",  'q'}, {"11001",     'p'}, {"11100",     'r'},
    {"0010",      's'}, {"01100",     't'}, {"00001",     'u'},
    {"1101110",   'v'}, {"00000",     'w'}, {"00111",     'x'},
    {"0001010",   'y'}, {"11011000",  'z'}, {"10",        ' '},
    {"11111011",  '0'}, {"1111100",   '1'}, {"001100",    '2'},
    {"1101101",   '3'}, {"11111010",  '4'}, {"00010110",  '5'},
    {"1101111",   '6'}, {"01111",     '7'}, {"000100",    '8'},
    {"01110",     '9'},
    // Only 37 letters + digits + space here; padding for stability:
    {"",          '\0'}, {"",         '\0'}, {"",         '\0'},
}};

// Binary trie built at load time. -1 marks a missing edge; leafSym is 0 for
// internal nodes. Node 0 is root.
struct Trie {
    struct Node { int left = -1; int right = -1; char sym = 0; };
    std::array<Node, 256> nodes{};
    int size = 1;

    constexpr Trie() {
        for (const auto& e : kHuff) {
            if (e.bits.empty()) continue;
            int cur = 0;
            for (char c : e.bits) {
                int& next = (c == '0') ? nodes[cur].left : nodes[cur].right;
                if (next == -1) {
                    next = size++;
                }
                cur = next;
            }
            nodes[cur].sym = e.sym;
        }
    }
};

const Trie kTrie{};

} // namespace

std::uint64_t BitReader::readBits(unsigned bits) noexcept {
    // Sanity-cap at 64. Beyond 64 makes no sense for a return in uint64_t.
    if (bits > 64) bits = 64;

    std::uint64_t result = 0;
    const std::size_t total = data_.size() * 8;
    for (unsigned i = 0; i < bits; ++i) {
        if (positionInBits_ < total) {
            const std::size_t bytePos = positionInBits_ >> 3;
            const unsigned    bitPos  = static_cast<unsigned>(positionInBits_ & 7u);
            const auto        b       = static_cast<std::uint8_t>(data_[bytePos]);
            result |= (static_cast<std::uint64_t>((b >> bitPos) & 1u)) << i;
        }
        ++positionInBits_;
    }
    return result;
}

std::uint8_t BitReader::peekNextByte() const noexcept {
    // Java semantics: peek at the byte AFTER aligning up to the next boundary,
    // unless we're already aligned in which case peek at the byte after the
    // current one. Returns 0xFF on OOB (matches Java's -1 cast to byte).
    const std::size_t idx = (positionInBits_ / 8)
                          + (bitsToNextBoundary() == 0 ? 0 : 1);
    if (idx + 1 >= data_.size()) return 0xFF;
    return static_cast<std::uint8_t>(data_[idx]);
}

std::uint8_t BitReader::currentByte() const noexcept {
    const std::size_t idx = positionInBits_ / 8;
    if (idx >= data_.size()) return 0;
    return static_cast<std::uint8_t>(data_[idx]);
}

std::uint64_t BitReader::peekNextBits(unsigned bits) const noexcept {
    BitReader copy = *this;
    return copy.readBits(bits);
}

std::string BitReader::readHuffmanEncodedString() {
    std::string result;
    result.reserve(4);
    int node = 0;
    for (;;) {
        const auto bit = readBits(1);
        const auto& n  = kTrie.nodes[node];
        const int next = (bit == 0) ? n.left : n.right;
        if (next == -1) {
            throw ParseException("Huffman decode failed: unknown bit pattern");
        }
        const auto& nn = kTrie.nodes[next];
        if (nn.sym != 0) {
            if (nn.sym == ' ') return result; // space terminates the code
            result.push_back(nn.sym);
            if (result.size() > 32) {
                throw ParseException("Huffman decode failed: string too long");
            }
            node = 0;
        } else {
            node = next;
        }
    }
}

} // namespace d2r
