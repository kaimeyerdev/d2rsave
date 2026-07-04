// Catch2 tests for save-file utilities (checksum, name/seed field access).

#include "d2r/Save.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path fixture(const char* name) {
    if (const char* dir = std::getenv("D2R_FIXTURE_DIR")) {
        return fs::path(dir) / name;
    }
#ifdef D2R_TEST_FIXTURE_DIR
    return fs::path(D2R_TEST_FIXTURE_DIR) / name;
#else
    return fs::path("../../D2R_Saves") / name;
#endif
}

} // namespace

TEST_CASE("rol32_1 rotates left by one", "[checksum]") {
    STATIC_REQUIRE(d2r::rol32_1(0x00000001u) == 0x00000002u);
    STATIC_REQUIRE(d2r::rol32_1(0x80000000u) == 0x00000001u);
    STATIC_REQUIRE(d2r::rol32_1(0xC0000001u) == 0x80000003u);
}

TEST_CASE("checksum on constructed buffer", "[checksum]") {
    // Manually reproduce the algorithm on a small buffer.
    std::vector<std::byte> buf(32);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        buf[i] = std::byte{static_cast<unsigned char>(i)};
    }
    std::uint32_t expected = 0;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        const std::uint8_t b = (i >= 12 && i < 16) ? 0u : static_cast<std::uint8_t>(i);
        expected = d2r::rol32_1(expected) + b;
    }
    REQUIRE(d2r::computeChecksum({buf.data(), buf.size()}) == expected);
}

TEST_CASE("stored checksum matches computed for all D2R_Saves fixtures", "[fixtures]") {
    for (const char* name : {"Kai.d2s", "Warlock.d2s", "Amazon.d2s", "Sorceress.d2s",
                             "Necromancer.d2s", "Paladin.d2s", "Barbarian.d2s",
                             "Druid.d2s", "Assassin.d2s", "DeadKai.d2s"}) {
        const auto p = fixture(name);
        INFO(p.string());
        REQUIRE(fs::exists(p));
        auto bytes = d2r::readFile(p);
        REQUIRE(d2r::hasValidMagic(bytes));
        REQUIRE(d2r::readStoredChecksum(bytes) == d2r::computeChecksum(bytes));
    }
}

TEST_CASE("character names match filenames for class fixtures", "[fixtures]") {
    struct Entry { const char* file; const char* name; };
    for (const auto& e : std::array{
        Entry{"Amazon.d2s",      "Amazon"},
        Entry{"Sorceress.d2s",   "Sorceress"},
        Entry{"Necromancer.d2s", "Necromancer"},
        Entry{"Paladin.d2s",     "Paladin"},
        Entry{"Barbarian.d2s",   "Barbarian"},
        Entry{"Druid.d2s",       "Druid"},
        Entry{"Assassin.d2s",    "Assassin"},
        Entry{"Warlock.d2s",     "Warlock"},
        Entry{"Kai.d2s",         "Kai"},
    }) {
        const auto p = fixture(e.file);
        INFO(p.string());
        auto bytes = d2r::readFile(p);
        REQUIRE(d2r::readCharacterName(bytes) == e.name);
    }
}
