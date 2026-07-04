// Catch2 tests for CharacterParser against the D2R_Saves fixtures.

#include "d2r/CharacterParser.hpp"
#include "d2r/Save.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

fs::path fixture(const char* name) {
    if (const char* dir = std::getenv("D2R_FIXTURE_DIR")) return fs::path(dir) / name;
#ifdef D2R_TEST_FIXTURE_DIR
    return fs::path(D2R_TEST_FIXTURE_DIR) / name;
#else
    return fs::path("../../D2R_Saves") / name;
#endif
}

d2r::Character parseFixture(const char* name) {
    const auto bytes = d2r::readFile(fixture(name));
    return d2r::parseCharacter(bytes);
}

} // namespace

TEST_CASE("class byte maps to the expected CharacterClass for each fixture",
          "[character][fixtures]") {
    struct Case { const char* file; d2r::CharacterClass klass; };
    for (const auto& c : std::array{
        Case{"Amazon.d2s",      d2r::CharacterClass::Amazon},
        Case{"Sorceress.d2s",   d2r::CharacterClass::Sorceress},
        Case{"Necromancer.d2s", d2r::CharacterClass::Necromancer},
        Case{"Paladin.d2s",     d2r::CharacterClass::Paladin},
        Case{"Barbarian.d2s",   d2r::CharacterClass::Barbarian},
        Case{"Druid.d2s",       d2r::CharacterClass::Druid},
        Case{"Assassin.d2s",    d2r::CharacterClass::Assassin},
        Case{"Warlock.d2s",     d2r::CharacterClass::Warlock},
        Case{"Kai.d2s",         d2r::CharacterClass::Warlock},
    }) {
        INFO(c.file);
        const auto ch = parseFixture(c.file);
        REQUIRE(ch.characterClass == c.klass);
    }
}

TEST_CASE("Kai matches known ground truth", "[character][fixtures]") {
    const auto k = parseFixture("Kai.d2s");
    REQUIRE(k.name == "Kai");
    REQUIRE(k.characterClass == d2r::CharacterClass::Warlock);
    REQUIRE(k.level == 97);
    REQUIRE(k.attributes.level == 97);
    REQUIRE(k.reignOfTheWarlock);
    // Hell / Act 4 active per the fixture's game state.
    REQUIRE(k.locations[static_cast<int>(d2r::Difficulty::Hell)].active);
    REQUIRE(k.locations[static_cast<int>(d2r::Difficulty::Hell)].act == 4);
    // 60 character items observed from build/debug/d2rsave dump.
    REQUIRE(k.itemCount == 60);
    // All section markers were located.
    REQUIRE(k.questsOffset    != 0);
    REQUIRE(k.waypointsOffset != 0);
    REQUIRE(k.statsOffset     != 0);
    REQUIRE(k.skillsOffset    != 0);
    REQUIRE(k.itemsOffset     != 0);
}

TEST_CASE("attributes decode plausibly for every class fixture",
          "[character][attributes]") {
    for (const char* file : {"Amazon.d2s","Sorceress.d2s","Necromancer.d2s",
                             "Paladin.d2s","Barbarian.d2s","Druid.d2s",
                             "Assassin.d2s","Warlock.d2s","Kai.d2s"}) {
        INFO(file);
        const auto ch = parseFixture(file);
        REQUIRE(ch.attributes.level >= 1);
        REQUIRE(ch.attributes.level <= 99);
        REQUIRE(ch.attributes.strength  >= 1);
        REQUIRE(ch.attributes.dexterity >= 1);
        REQUIRE(ch.attributes.vitality  >= 1);
        REQUIRE(ch.attributes.energy    >= 1);
        // MaxHP/MaxMana should be non-zero once attributes have been rolled.
        REQUIRE(ch.attributes.maxHP   > 0);
        REQUIRE(ch.attributes.maxMana > 0);
    }
}

TEST_CASE("findMarker locates 2-byte markers correctly", "[character][markers]") {
    const std::array<std::byte, 12> data{{
        std::byte{0x00}, std::byte{0x00}, std::byte{'W'}, std::byte{'S'},
        std::byte{0x11}, std::byte{'i'},  std::byte{'f'}, std::byte{0x22},
        std::byte{'J'},  std::byte{'M'},  std::byte{0x33}, std::byte{0x44},
    }};
    std::span<const std::byte> s{data.data(), data.size()};
    REQUIRE(d2r::findMarker(s, "WS") == 2);
    REQUIRE(d2r::findMarker(s, "if") == 5);
    REQUIRE(d2r::findMarker(s, "JM") == 8);
    REQUIRE(!d2r::findMarker(s, "xx").has_value());
    // Skip past the first match.
    REQUIRE(d2r::findMarker(s, "WS", 3) == std::nullopt);
}
