#include "d2r/Save.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace d2r {

std::uint32_t computeChecksum(std::span<const std::byte> bytes) noexcept {
    std::uint32_t checksum = 0;
    const std::size_t n = bytes.size();
    for (std::size_t i = 0; i < n; ++i) {
        const bool inChecksumField = (i >= kChecksumOffset) && (i < kChecksumOffset + kChecksumSize);
        const std::uint8_t b = inChecksumField ? 0u : std::to_integer<std::uint8_t>(bytes[i]);
        checksum = rol32_1(checksum) + b;
    }
    return checksum;
}

std::uint32_t readU32LE(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    // Callers already know the file is large enough; this is best-effort safe.
    if (offset + 4 > bytes.size()) return 0;
    const auto* p = reinterpret_cast<const std::uint8_t*>(bytes.data()) + offset;
    return  static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) <<  8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

void writeU32LE(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) noexcept {
    if (offset + 4 > bytes.size()) return;
    auto* p = reinterpret_cast<std::uint8_t*>(bytes.data()) + offset;
    p[0] = static_cast<std::uint8_t>( value        & 0xFF);
    p[1] = static_cast<std::uint8_t>((value >>  8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

std::vector<std::byte> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::system_error(errno, std::generic_category(),
                                "open for read: " + path.string());
    }
    const std::streamsize size = in.tellg();
    if (size < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "tellg: " + path.string());
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::byte> buf(static_cast<std::size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(buf.data()), size)) {
        throw std::system_error(errno, std::generic_category(),
                                "read: " + path.string());
    }
    return buf;
}

void writeFileAtomic(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::system_error(errno, std::generic_category(),
                                    "open for write: " + tmp.string());
        }
        if (!bytes.empty() &&
            !out.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()))) {
            throw std::system_error(errno, std::generic_category(),
                                    "write: " + tmp.string());
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        throw std::system_error(ec, "rename " + tmp.string() + " -> " + path.string());
    }
}

bool hasValidMagic(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < 4) return false;
    return readU32LE(bytes, kMagicOffset) == kMagic;
}

std::string readCharacterName(std::span<const std::byte> bytes) {
    if (bytes.size() < kNameOffset + kNameSize) return {};
    const auto* p = reinterpret_cast<const char*>(bytes.data()) + kNameOffset;
    const std::size_t maxChars = kNameSize - 1; // reserve trailing null
    const std::size_t len = ::strnlen(p, maxChars);
    return std::string(p, len);
}

bool writeCharacterName(std::span<std::byte> bytes, std::string_view newName) {
    if (bytes.size() < kNameOffset + kNameSize) return false;
    if (newName.size() >= kNameSize) return false;              // needs room for NUL
    for (char c : newName) {
        if (static_cast<unsigned char>(c) < 0x20) return false; // no control chars
    }
    auto* p = reinterpret_cast<std::uint8_t*>(bytes.data()) + kNameOffset;
    std::fill(p, p + kNameSize, std::uint8_t{0});
    std::memcpy(p, newName.data(), newName.size());
    return true;
}

std::uint32_t readMapSeed(std::span<const std::byte> bytes) noexcept {
    return readU32LE(bytes, kMapSeedOffset);
}

void writeMapSeed(std::span<std::byte> bytes, std::uint32_t seed) noexcept {
    writeU32LE(bytes, kMapSeedOffset, seed);
}

std::uint32_t readStoredChecksum(std::span<const std::byte> bytes) noexcept {
    return readU32LE(bytes, kChecksumOffset);
}

std::uint32_t recomputeAndWriteChecksum(std::span<std::byte> bytes) noexcept {
    const auto sum = computeChecksum(bytes);
    writeU32LE(bytes, kChecksumOffset, sum);
    return sum;
}

} // namespace d2r
