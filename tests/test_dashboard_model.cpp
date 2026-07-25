// Catch2 tests for DashboardModel helpers exposed to the FTXUI layer.
// Focus areas: the SessionAnchor's per-rune stack snapshot (populated
// by makeSessionAnchorFromSnapshot from a DashboardSnapshot's inventory)
// and the item-key set that drives the "new uniques / sets" diff.
//
// These tests build synthetic DashboardSnapshots directly rather than
// parsing real .d2s files, so they're independent of fixtures.

#include "d2r/DashboardModel.hpp"
#include "d2r/Item.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

d2r::InventoryItem makeRune(std::string_view code, std::string_view name,
                             std::string_view loc = "stash tab 1") {
    d2r::InventoryItem inv;
    inv.name    = std::string(name);
    inv.baseName = std::string(name);
    inv.code    = std::string(code);
    inv.location = std::string(loc);
    inv.quality = d2r::ItemQuality::Normal;
    return inv;
}

d2r::InventoryItem makeUnique(std::uint32_t fingerprint, std::string_view name,
                               std::string_view code = "uni") {
    d2r::InventoryItem inv;
    inv.name        = std::string(name);
    inv.baseName    = std::string(name);
    inv.code        = std::string(code);
    inv.location    = "Kai.d2s";
    inv.quality     = d2r::ItemQuality::Unique;
    inv.fingerprint = fingerprint;
    inv.identified  = true;
    return inv;
}

} // namespace

TEST_CASE("SessionAnchor.runeStacks: empty inventory -> empty map",
          "[dashboard_model][runes]") {
    d2r::DashboardSnapshot snap;
    const auto anchor = d2r::makeSessionAnchorFromSnapshot(snap, 0, 0, 0);
    REQUIRE(anchor.runeStacks.empty());
}

TEST_CASE("SessionAnchor.runeStacks: counts one entry per rune instance",
          "[dashboard_model][runes]") {
    d2r::DashboardSnapshot snap;
    // 3 x Hel, 1 x Ko, 2 x Um.
    snap.inventory.push_back(makeRune("r15", "Hel Rune"));
    snap.inventory.push_back(makeRune("r15", "Hel Rune"));
    snap.inventory.push_back(makeRune("r15", "Hel Rune"));
    snap.inventory.push_back(makeRune("r18", "Ko Rune"));
    snap.inventory.push_back(makeRune("r22", "Um Rune"));
    snap.inventory.push_back(makeRune("r22", "Um Rune"));

    const auto anchor = d2r::makeSessionAnchorFromSnapshot(snap, 0, 0, 0);
    REQUIRE(anchor.runeStacks.size() == 3);
    REQUIRE(anchor.runeStacks.at("r15") == 3);
    REQUIRE(anchor.runeStacks.at("r18") == 1);
    REQUIRE(anchor.runeStacks.at("r22") == 2);
}

TEST_CASE("SessionAnchor.runeStacks: ignores non-rune codes",
          "[dashboard_model][runes]") {
    d2r::DashboardSnapshot snap;
    // Rune codes are 3-char 'r' + 2 digits. Everything else must not
    // be counted -- gems, keys, quest items, or an accidental 4-char
    // r-something.
    d2r::InventoryItem key;
    key.name = "Key of Terror";
    key.code = "pk1";
    key.quality = d2r::ItemQuality::Normal;

    d2r::InventoryItem gem;
    gem.name = "Chipped Ruby";
    gem.code = "gcr";
    gem.quality = d2r::ItemQuality::Normal;

    d2r::InventoryItem fakeR;
    fakeR.name = "Not a rune";
    fakeR.code = "rXY";   // non-digit trailing chars
    fakeR.quality = d2r::ItemQuality::Normal;

    d2r::InventoryItem fakeR4;
    fakeR4.name = "Overlong";
    fakeR4.code = "r100";  // 4 chars
    fakeR4.quality = d2r::ItemQuality::Normal;

    snap.inventory = {key, gem, fakeR, fakeR4,
                      makeRune("r08", "Ral Rune")};

    const auto anchor = d2r::makeSessionAnchorFromSnapshot(snap, 0, 0, 0);
    REQUIRE(anchor.runeStacks.size() == 1);
    REQUIRE(anchor.runeStacks.at("r08") == 1);
}

TEST_CASE("SessionAnchor: unique/set fingerprints go into itemKeys",
          "[dashboard_model][session_loot]") {
    d2r::DashboardSnapshot snap;
    // Two identified uniques with distinct fingerprints, plus a
    // set item, plus an unidentified unique (should be skipped).
    snap.inventory.push_back(makeUnique(0xAAAA1111u, "Windforce"));
    snap.inventory.push_back(makeUnique(0xBBBB2222u, "Griffon's Eye"));

    d2r::InventoryItem setItem = makeUnique(0xCCCC3333u, "Tal Rasha's Adjudication");
    setItem.quality = d2r::ItemQuality::Set;
    snap.inventory.push_back(setItem);

    d2r::InventoryItem unident = makeUnique(0xDDDD4444u, "Mystery");
    unident.identified = false;
    snap.inventory.push_back(unident);

    // A unique with fingerprint=0 (shouldn't be indexed either).
    d2r::InventoryItem noPrint = makeUnique(0u, "Unfingerprinted");
    snap.inventory.push_back(noPrint);

    const auto anchor = d2r::makeSessionAnchorFromSnapshot(snap, 0, 0, 0);
    REQUIRE(anchor.itemKeys.size() == 3);
    REQUIRE(anchor.itemKeys.contains({0xAAAA1111u, d2r::ItemQuality::Unique}));
    REQUIRE(anchor.itemKeys.contains({0xBBBB2222u, d2r::ItemQuality::Unique}));
    REQUIRE(anchor.itemKeys.contains({0xCCCC3333u, d2r::ItemQuality::Set}));
    REQUIRE_FALSE(anchor.itemKeys.contains({0xDDDD4444u, d2r::ItemQuality::Unique}));
    REQUIRE_FALSE(anchor.itemKeys.contains({0u,         d2r::ItemQuality::Unique}));
}

TEST_CASE("SessionAnchor round-trip: session-loot diff finds only positive rune deltas",
          "[dashboard_model][session_loot]") {
    // Anchor snapshot: 2 Hel + 1 Ko.
    d2r::DashboardSnapshot anchorSnap;
    anchorSnap.inventory.push_back(makeRune("r15", "Hel Rune"));
    anchorSnap.inventory.push_back(makeRune("r15", "Hel Rune"));
    anchorSnap.inventory.push_back(makeRune("r18", "Ko Rune"));
    const auto anchor = d2r::makeSessionAnchorFromSnapshot(anchorSnap, 0, 0, 0);

    // "Now" snapshot: 3 Hel (+1), 1 Ko (=), 2 Um (+2, new code),
    // 0 Fal  ("consumed" -- runes at anchor but not now).
    // Diff should surface r15:+1 and r22:+2 only; r18 stays at zero;
    // consumed runes never appear because we only care about gains.
    d2r::DashboardSnapshot nowSnap;
    nowSnap.inventory.push_back(makeRune("r15", "Hel Rune"));
    nowSnap.inventory.push_back(makeRune("r15", "Hel Rune"));
    nowSnap.inventory.push_back(makeRune("r15", "Hel Rune"));
    nowSnap.inventory.push_back(makeRune("r18", "Ko Rune"));
    nowSnap.inventory.push_back(makeRune("r22", "Um Rune"));
    nowSnap.inventory.push_back(makeRune("r22", "Um Rune"));

    // Duplicate the pane's diff logic since the renderer isn't
    // reachable from a linkable unit test.
    std::unordered_map<std::string, std::uint32_t> nowStacks;
    for (const auto& it : nowSnap.inventory) {
        if (it.code.size() == 3 && it.code[0] == 'r'
            && it.code[1] >= '0' && it.code[1] <= '9'
            && it.code[2] >= '0' && it.code[2] <= '9') {
            ++nowStacks[it.code];
        }
    }
    std::vector<std::pair<std::string, std::int32_t>> deltas;
    for (const auto& [code, now] : nowStacks) {
        const auto it = anchor.runeStacks.find(code);
        const std::uint32_t before = it == anchor.runeStacks.end() ? 0u : it->second;
        if (now > before) {
            deltas.emplace_back(code, static_cast<std::int32_t>(now - before));
        }
    }
    std::sort(deltas.begin(), deltas.end());
    REQUIRE(deltas.size() == 2);
    REQUIRE(deltas[0].first  == "r15");
    REQUIRE(deltas[0].second == 1);
    REQUIRE(deltas[1].first  == "r22");
    REQUIRE(deltas[1].second == 2);
}
