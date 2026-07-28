// Catch2 tests for DashboardConfig JSON round-trip.
//
// Exercises the public serializePaneTree / deserializePaneTree helpers
// so we don't have to touch a SQLite DB just to prove the encoding
// stays lossless for every PaneType (including the four configurable
// top-row panes: Character, SessionLoot, Uber, TerrorZone).

#include "d2r/DashboardConfig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>

namespace {

d2r::PaneNode leaf(d2r::PaneType t) {
    d2r::PaneNode n;
    n.isSplit = false;
    n.config.type = t;
    return n;
}

d2r::PaneNode split(d2r::SplitDirection dir, d2r::PaneNode a, d2r::PaneNode b) {
    d2r::PaneNode n;
    n.isSplit = true;
    n.direction = dir;
    n.a = std::make_unique<d2r::PaneNode>(std::move(a));
    n.b = std::make_unique<d2r::PaneNode>(std::move(b));
    return n;
}

d2r::PaneNode roundTrip(const d2r::PaneNode& in) {
    return d2r::deserializePaneTree(d2r::serializePaneTree(in));
}

} // namespace

TEST_CASE("PaneType enum round-trips through toString/fromString", "[dashboard_config]") {
    for (auto t : {
        d2r::PaneType::Blank, d2r::PaneType::Chronicle, d2r::PaneType::Inventory,
        d2r::PaneType::Reconcile, d2r::PaneType::Backups, d2r::PaneType::BackupLog,
        d2r::PaneType::Session, d2r::PaneType::Character, d2r::PaneType::SessionLoot,
        d2r::PaneType::Uber, d2r::PaneType::TerrorZone,
    }) {
        INFO("PaneType index=" << static_cast<int>(t));
        REQUIRE(d2r::paneTypeFromString(d2r::toString(t)) == t);
    }
}

TEST_CASE("paneTitle produces non-empty labels for every PaneType", "[dashboard_config]") {
    for (auto t : {
        d2r::PaneType::Blank, d2r::PaneType::Chronicle, d2r::PaneType::Inventory,
        d2r::PaneType::Reconcile, d2r::PaneType::Backups, d2r::PaneType::BackupLog,
        d2r::PaneType::Session, d2r::PaneType::Character, d2r::PaneType::SessionLoot,
        d2r::PaneType::Uber, d2r::PaneType::TerrorZone,
    }) {
        d2r::PaneConfig c;
        c.type = t;
        INFO("PaneType index=" << static_cast<int>(t));
        REQUIRE_FALSE(d2r::paneTitle(c).empty());
    }
}

TEST_CASE("Character pane round-trips characterSelection + anchor pin", "[dashboard_config]") {
    auto n = leaf(d2r::PaneType::Character);
    n.config.characterSelection      = "Kai";
    n.config.sessionAnchorPinned     = true;
    n.config.sessionAnchorPinnedDate = 1'700'000'000;
    n.config.paneWeight              = 2;

    const auto back = roundTrip(n);
    REQUIRE_FALSE(back.isSplit);
    REQUIRE(back.config.type                    == d2r::PaneType::Character);
    REQUIRE(back.config.characterSelection      == "Kai");
    REQUIRE(back.config.sessionAnchorPinned     == true);
    REQUIRE(back.config.sessionAnchorPinnedDate == 1'700'000'000);
    REQUIRE(back.config.paneWeight              == 2);
}

TEST_CASE("Character pane defaults survive round-trip (empty = auto)", "[dashboard_config]") {
    // Defaults: empty selection, auto anchor. Verify the empty string
    // survives so it keeps meaning "auto" after reload.
    const auto back = roundTrip(leaf(d2r::PaneType::Character));
    REQUIRE(back.config.type                    == d2r::PaneType::Character);
    REQUIRE(back.config.characterSelection      == "");
    REQUIRE(back.config.sessionAnchorPinned     == false);
    REQUIRE(back.config.sessionAnchorPinnedDate == 0);
}

TEST_CASE("SessionLoot pane round-trips runes toggle + character selection", "[dashboard_config]") {
    auto n = leaf(d2r::PaneType::SessionLoot);
    n.config.characterSelection   = "Warlock";
    n.config.sessionLootShowRunes = false;

    const auto back = roundTrip(n);
    REQUIRE(back.config.type                  == d2r::PaneType::SessionLoot);
    REQUIRE(back.config.characterSelection    == "Warlock");
    REQUIRE(back.config.sessionLootShowRunes  == false);
}

TEST_CASE("SessionLoot pane defaults: runes on, selection empty", "[dashboard_config]") {
    const auto back = roundTrip(leaf(d2r::PaneType::SessionLoot));
    REQUIRE(back.config.sessionLootShowRunes       == true);
    REQUIRE(back.config.characterSelection         == "");
    REQUIRE(back.config.sessionAnchorPinned        == false);
    REQUIRE(back.config.sessionAnchorPinnedDate    == 0);
    REQUIRE(back.config.sessionAnchorPinnedEndDate == 0);
}

TEST_CASE("Session-anchor end pin round-trips on Session / SessionLoot / Character",
          "[dashboard_config]") {
    // Each pane type that consumes the anchor should independently
    // persist a pinned end date. Non-zero values survive; zero (default)
    // is elided by the encoder and reads back as zero.
    for (auto t : {d2r::PaneType::Session,
                   d2r::PaneType::SessionLoot,
                   d2r::PaneType::Character}) {
        auto n = leaf(t);
        n.config.sessionAnchorPinnedEndDate = 1'751'000'000;
        const auto back = roundTrip(n);
        INFO("pane type index " << static_cast<int>(t));
        REQUIRE(back.config.type                       == t);
        REQUIRE(back.config.sessionAnchorPinnedEndDate == 1'751'000'000);
    }
}

TEST_CASE("Session-anchor end pin defaults to 0 across all three pane types",
          "[dashboard_config]") {
    for (auto t : {d2r::PaneType::Session,
                   d2r::PaneType::SessionLoot,
                   d2r::PaneType::Character}) {
        const auto back = roundTrip(leaf(t));
        INFO("pane type index " << static_cast<int>(t));
        REQUIRE(back.config.sessionAnchorPinnedEndDate == 0);
    }
}

TEST_CASE("Uber pane round-trips both option toggles", "[dashboard_config]") {
    auto n = leaf(d2r::PaneType::Uber);
    n.config.uberShowUbers        = true;
    n.config.uberShowTorchByClass = true;

    const auto back = roundTrip(n);
    REQUIRE(back.config.type                 == d2r::PaneType::Uber);
    REQUIRE(back.config.uberShowUbers        == true);
    REQUIRE(back.config.uberShowTorchByClass == true);
}

TEST_CASE("Uber pane defaults: both toggles off", "[dashboard_config]") {
    const auto back = roundTrip(leaf(d2r::PaneType::Uber));
    REQUIRE(back.config.uberShowUbers        == false);
    REQUIRE(back.config.uberShowTorchByClass == false);
}

TEST_CASE("TerrorZone pane has no per-pane options to lose", "[dashboard_config]") {
    // TerrorZone carries no configurable state today; the round-trip
    // just needs to preserve the type. Pinning this keeps regressions
    // honest if we add fields later without minding the encoder.
    auto n = leaf(d2r::PaneType::TerrorZone);
    n.config.paneWeight = 3;

    const auto back = roundTrip(n);
    REQUIRE(back.config.type       == d2r::PaneType::TerrorZone);
    REQUIRE(back.config.paneWeight == 3);
}

TEST_CASE("Chronicle pane round-trips uniquesShowMisc (default true)", "[dashboard_config]") {
    // Default true: verify the flag comes back true even though the
    // encoder elides the default value to keep JSON tidy.
    auto n = leaf(d2r::PaneType::Chronicle);
    n.config.category = d2r::ChronicleCategory::UniquesAll;
    REQUIRE(n.config.uniquesShowMisc == true);
    auto back = roundTrip(n);
    REQUIRE(back.config.uniquesShowMisc == true);

    // Non-default: false must survive the round-trip.
    n.config.uniquesShowMisc = false;
    back = roundTrip(n);
    REQUIRE(back.config.uniquesShowMisc == false);
}

TEST_CASE("Splits carrying new pane types round-trip losslessly", "[dashboard_config]") {
    // Build a small tree: Character | Uber over TerrorZone.
    auto charLeaf = leaf(d2r::PaneType::Character);
    charLeaf.config.characterSelection = "Kai";

    auto uber = leaf(d2r::PaneType::Uber);
    uber.config.uberShowUbers = true;

    auto tz = leaf(d2r::PaneType::TerrorZone);

    auto right = split(d2r::SplitDirection::Horizontal, std::move(uber), std::move(tz));
    auto root  = split(d2r::SplitDirection::Vertical, std::move(charLeaf), std::move(right));

    const auto back = roundTrip(root);
    REQUIRE(back.isSplit);
    REQUIRE(back.direction == d2r::SplitDirection::Vertical);
    REQUIRE(back.a);
    REQUIRE(back.b);

    // Left leaf.
    REQUIRE_FALSE(back.a->isSplit);
    REQUIRE(back.a->config.type               == d2r::PaneType::Character);
    REQUIRE(back.a->config.characterSelection == "Kai");

    // Right subtree.
    REQUIRE(back.b->isSplit);
    REQUIRE(back.b->direction == d2r::SplitDirection::Horizontal);
    REQUIRE(back.b->a);   // Catch2 REQUIRE forbids && inside the macro,
    REQUIRE(back.b->b);   // so we check each child separately.
    REQUIRE(back.b->a->config.type          == d2r::PaneType::Uber);
    REQUIRE(back.b->a->config.uberShowUbers == true);
    REQUIRE(back.b->b->config.type          == d2r::PaneType::TerrorZone);
}

TEST_CASE("deserializePaneTree returns a Blank leaf for malformed JSON", "[dashboard_config]") {
    const auto back = d2r::deserializePaneTree("{ this is not json ]");
    REQUIRE_FALSE(back.isSplit);
    REQUIRE(back.config.type == d2r::PaneType::Blank);
}

TEST_CASE("Unknown pane-type strings fall back to Blank", "[dashboard_config]") {
    const auto back = d2r::deserializePaneTree(R"({"type":"unheard_of_pane"})");
    REQUIRE_FALSE(back.isSplit);
    REQUIRE(back.config.type == d2r::PaneType::Blank);
}
