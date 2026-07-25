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

// --- set-difficulty helpers ------------------------------------------------

namespace {

// Build a minimally-sized buffer whose only meaningful bytes are the three
// at kDifficultyOffset. Everything else stays 0, which is fine because the
// difficulty accessors read/write nothing else.
std::vector<std::byte> difficultyBuf(std::uint8_t normal, std::uint8_t nm, std::uint8_t hell) {
    std::vector<std::byte> b(d2r::kDifficultyOffset + d2r::kDifficultySize, std::byte{0});
    b[d2r::kDifficultyOffset + 0] = std::byte{normal};
    b[d2r::kDifficultyOffset + 1] = std::byte{nm};
    b[d2r::kDifficultyOffset + 2] = std::byte{hell};
    return b;
}

std::uint8_t diffByte(std::span<const std::byte> b, std::size_t i) {
    return static_cast<std::uint8_t>(b[d2r::kDifficultyOffset + i]);
}

} // namespace

TEST_CASE("readActiveDifficulty returns nullopt for undersized buffer", "[difficulty]") {
    std::vector<std::byte> b(d2r::kDifficultyOffset + d2r::kDifficultySize - 1, std::byte{0xFF});
    REQUIRE_FALSE(d2r::readActiveDifficulty({b.data(), b.size()}).has_value());
}

TEST_CASE("readActiveDifficulty returns nullopt when no active flag is set", "[difficulty]") {
    // Low bits set (act progress) but no 0x80 anywhere: still nullopt.
    const auto b = difficultyBuf(0x07, 0x07, 0x07);
    REQUIRE_FALSE(d2r::readActiveDifficulty({b.data(), b.size()}).has_value());
}

TEST_CASE("readActiveDifficulty finds Normal/Nightmare/Hell active bit", "[difficulty]") {
    const auto n = difficultyBuf(0x80, 0x00, 0x00);
    REQUIRE(d2r::readActiveDifficulty({n.data(), n.size()}) == std::uint8_t{0});

    const auto m = difficultyBuf(0x00, 0x82, 0x00);
    REQUIRE(d2r::readActiveDifficulty({m.data(), m.size()}) == std::uint8_t{1});

    const auto h = difficultyBuf(0x00, 0x00, 0x84);
    REQUIRE(d2r::readActiveDifficulty({h.data(), h.size()}) == std::uint8_t{2});
}

TEST_CASE("writeActiveDifficulty is a no-op for out-of-range value", "[difficulty]") {
    auto b = difficultyBuf(0x00, 0x00, 0x84);
    d2r::writeActiveDifficulty({b.data(), b.size()}, 3);
    REQUIRE(diffByte(b, 0) == 0x00);
    REQUIRE(diffByte(b, 1) == 0x00);
    REQUIRE(diffByte(b, 2) == 0x84);
    d2r::writeActiveDifficulty({b.data(), b.size()}, 255);
    REQUIRE(diffByte(b, 2) == 0x84);
}

TEST_CASE("writeActiveDifficulty is a no-op for undersized buffer", "[difficulty]") {
    std::vector<std::byte> b(d2r::kDifficultyOffset + d2r::kDifficultySize - 1, std::byte{0x11});
    d2r::writeActiveDifficulty({b.data(), b.size()}, 0);
    // No bytes past the offset were touched (and no crash).
    for (auto v : b) REQUIRE(static_cast<std::uint8_t>(v) == 0x11);
}

TEST_CASE("writeActiveDifficulty preserves act bits on non-target bytes", "[difficulty]") {
    // Hell has act-5 progress (0x04) + active bit (0x80). Move active to Normal;
    // Hell should keep 0x04, active bit cleared. Nightmare stays untouched.
    auto b = difficultyBuf(0x00, 0x02, 0x84);
    d2r::writeActiveDifficulty({b.data(), b.size()}, 0);
    REQUIRE(diffByte(b, 0) == 0x80);
    REQUIRE(diffByte(b, 1) == 0x02);   // act bits preserved, no 0x80 flip
    REQUIRE(diffByte(b, 2) == 0x04);   // 0x80 cleared, act bits kept
    REQUIRE(d2r::readActiveDifficulty({b.data(), b.size()}) == std::uint8_t{0});
}

TEST_CASE("writeActiveDifficulty preserves target byte's own act bits", "[difficulty]") {
    // Hell byte already has act-5 progress (0x04) but was inactive.
    // Activating Hell should set 0x80 without wiping the 0x04.
    auto b = difficultyBuf(0x80, 0x00, 0x04);
    d2r::writeActiveDifficulty({b.data(), b.size()}, 2);
    REQUIRE(diffByte(b, 0) == 0x00);
    REQUIRE(diffByte(b, 1) == 0x00);
    REQUIRE(diffByte(b, 2) == 0x84);   // 0x80 | 0x04
}

TEST_CASE("writeActiveDifficulty defaults blank target to act 1", "[difficulty]") {
    auto b = difficultyBuf(0x00, 0x00, 0x00);
    d2r::writeActiveDifficulty({b.data(), b.size()}, 1);
    REQUIRE(diffByte(b, 0) == 0x00);
    REQUIRE(diffByte(b, 1) == 0x80);   // active bit only; low bits 0 == act 1
    REQUIRE(diffByte(b, 2) == 0x00);
}

TEST_CASE("writeActiveDifficulty ignores stray bits (0x08..0x40) on non-target byte", "[difficulty]") {
    // Bit 3 through bit 6 are not documented as meaningful and are stripped
    // by the current writer alongside the active bit. Pin the behavior so
    // regressions get caught if that changes.
    auto b = difficultyBuf(0x00, 0x78, 0x00);   // bits 3..6 all set, no active
    d2r::writeActiveDifficulty({b.data(), b.size()}, 0);
    REQUIRE(diffByte(b, 0) == 0x80);
    REQUIRE(diffByte(b, 1) == 0x00);   // stray bits stripped
    REQUIRE(diffByte(b, 2) == 0x00);
}

TEST_CASE("writeActiveDifficulty round-trip through the same target is idempotent", "[difficulty]") {
    auto b = difficultyBuf(0x00, 0x00, 0x84);
    d2r::writeActiveDifficulty({b.data(), b.size()}, 2);
    REQUIRE(diffByte(b, 0) == 0x00);
    REQUIRE(diffByte(b, 1) == 0x00);
    REQUIRE(diffByte(b, 2) == 0x84);
    d2r::writeActiveDifficulty({b.data(), b.size()}, 2);
    REQUIRE(diffByte(b, 2) == 0x84);
}

TEST_CASE("set-difficulty on a fixture: switch and round-trip preserves everything else",
          "[fixtures][difficulty]") {
    // Paladin has 0x00,0x00,0x84 at kDifficultyOffset -- Hell active with act-5
    // progress. Ideal for verifying we don't wipe the low bits on either the
    // outgoing or the returning target byte.
    const auto p = fixture("Paladin.d2s");
    REQUIRE(fs::exists(p));
    auto original = d2r::readFile(p);
    REQUIRE(d2r::hasValidMagic(original));
    REQUIRE(d2r::readStoredChecksum(original) == d2r::computeChecksum(original));
    REQUIRE(d2r::readActiveDifficulty(original) == std::uint8_t{2});
    REQUIRE(diffByte(original, 2) == 0x84);

    const auto originalName    = d2r::readCharacterName(original);
    const auto originalMapSeed = d2r::readMapSeed(original);

    // Move to Normal.
    auto bytes = original;
    d2r::writeActiveDifficulty({bytes.data(), bytes.size()}, 0);
    const auto newChecksum = d2r::recomputeAndWriteChecksum(bytes);
    REQUIRE(d2r::readActiveDifficulty(bytes) == std::uint8_t{0});
    REQUIRE(diffByte(bytes, 0) == 0x80);
    REQUIRE(diffByte(bytes, 1) == 0x00);
    REQUIRE(diffByte(bytes, 2) == 0x04);   // act bits preserved on outgoing Hell
    REQUIRE(d2r::readStoredChecksum(bytes) == newChecksum);
    REQUIRE(d2r::computeChecksum(bytes) == newChecksum);
    // Name and map seed are unaffected side-effects.
    REQUIRE(d2r::readCharacterName(bytes) == originalName);
    REQUIRE(d2r::readMapSeed(bytes) == originalMapSeed);

    // Move back to Hell -- the 0x04 progress bits on Hell come back untouched,
    // and the checksum settles back to the fixture's original value.
    d2r::writeActiveDifficulty({bytes.data(), bytes.size()}, 2);
    const auto restoredChecksum = d2r::recomputeAndWriteChecksum(bytes);
    REQUIRE(d2r::readActiveDifficulty(bytes) == std::uint8_t{2});
    REQUIRE(diffByte(bytes, 0) == 0x00);
    REQUIRE(diffByte(bytes, 1) == 0x00);
    REQUIRE(diffByte(bytes, 2) == 0x84);
    REQUIRE(restoredChecksum == d2r::readStoredChecksum(original));
    REQUIRE(bytes == original);
}
