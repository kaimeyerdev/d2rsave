// Catch2 tests for DashboardConfig JSON round-trip.
//
// Exercises the public serializePaneTree / deserializePaneTree helpers
// so we don't have to touch a SQLite DB just to prove the encoding
// stays lossless for every PaneType (including the four configurable
// top-row panes: Character, SessionLoot, Uber, TerrorZone).

#include "d2r/DashboardConfig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <ctime>
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

TEST_CASE("Character pane round-trips characterSelection", "[dashboard_config]") {
    auto n = leaf(d2r::PaneType::Character);
    n.config.characterSelection       = "Kai";
    n.config.paneWeight               = 2;

    const auto back = roundTrip(n);
    REQUIRE_FALSE(back.isSplit);
    REQUIRE(back.config.type                     == d2r::PaneType::Character);
    REQUIRE(back.config.characterSelection       == "Kai");
    REQUIRE(back.config.paneWeight               == 2);
}

TEST_CASE("Character pane defaults survive round-trip (empty = auto)", "[dashboard_config]") {
    // Defaults: empty selection, auto session. Verify the empty string
    // survives so it keeps meaning "auto" after reload.
    const auto back = roundTrip(leaf(d2r::PaneType::Character));
    REQUIRE(back.config.type                     == d2r::PaneType::Character);
    REQUIRE(back.config.characterSelection       == "");
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
    REQUIRE(back.config.sessionLootShowRunes      == true);
    REQUIRE(back.config.characterSelection        == "");
}

TEST_CASE("Legacy session-window keys on PaneConfig JSON are silently ignored",
          "[dashboard_config]") {
    // Session-window overrides used to live on PaneConfig. They now
    // belong to the AppSession singleton (in-memory only). Loading a
    // layout that still has the legacy keys must not crash or leak
    // state onto PaneConfig -- the fields aren't there anymore, so
    // the loader just drops them.
    const std::string legacy = R"({
        "type": "session",
        "sessionCustomStartEpoch": 1750000000,
        "sessionCustomEndEpoch":   1751000000,
        "sessionAnchorPinned":     true,
        "sessionAnchorPinnedDate": 1700000000,
        "sessionAnchorPinnedEndDate": 1701000000
    })";
    const auto node = d2r::deserializePaneTree(legacy);
    REQUIRE_FALSE(node.isSplit);
    REQUIRE(node.config.type == d2r::PaneType::Session);
    // Re-serialising must succeed and produce no session-window keys.
    const auto rt = d2r::serializePaneTree(node);
    REQUIRE(rt.find("sessionCustomStartEpoch") == std::string::npos);
    REQUIRE(rt.find("sessionCustomEndEpoch")   == std::string::npos);
    REQUIRE(rt.find("sessionAnchorPinned")     == std::string::npos);
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

TEST_CASE("Backups pane round-trips backupsRunCollapse", "[dashboard_config]") {
    // Default is `true`; verify it survives a round-trip AND that the
    // non-default (`false`) also survives so a user's opt-out sticks.
    {
        const auto back = roundTrip(leaf(d2r::PaneType::Backups));
        REQUIRE(back.config.type               == d2r::PaneType::Backups);
        REQUIRE(back.config.backupsRunCollapse == true);
    }
    {
        auto n = leaf(d2r::PaneType::Backups);
        n.config.backupsRunCollapse = false;
        const auto back = roundTrip(n);
        REQUIRE(back.config.backupsRunCollapse == false);
    }
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

// ------------------------- parseUserDateTime -------------------------------
//
// Anchor the tests to a fixed reference "now" so today-relative and
// offset-relative forms are deterministic. 2026-07-29 15:30:00 local
// == unix 1785763800 in UTC, but we don't rely on that value; instead
// we let mktime tell us what "today at HH:MM" resolves to under the
// same tm structure the parser uses. This keeps the tests hermetic
// against whichever timezone CI happens to be in.

namespace {

std::int64_t localEpoch(int y, int mo, int d, int h, int mi, int se) {
    std::tm tm{};
    tm.tm_year  = y - 1900;
    tm.tm_mon   = mo - 1;
    tm.tm_mday  = d;
    tm.tm_hour  = h;
    tm.tm_min   = mi;
    tm.tm_sec   = se;
    tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm));
}

// A reference "now" the parseUserDateTime tests use for offset + today
// forms. Fixed date (2026-07-29 15:30:00 local); mktime resolves the
// local epoch under the test host's timezone the same way the parser
// will at runtime.
const std::int64_t kNow = localEpoch(2026, 7, 29, 15, 30, 0);

} // namespace

TEST_CASE("parseUserDateTime: empty and 'now' resolve to reference epoch",
          "[dashboard_config][datetime]") {
    REQUIRE(d2r::parseUserDateTime("",     kNow) == kNow);
    REQUIRE(d2r::parseUserDateTime("  ",   kNow) == kNow);
    REQUIRE(d2r::parseUserDateTime("now",  kNow) == kNow);
    REQUIRE(d2r::parseUserDateTime("Now",  kNow) == kNow);
    REQUIRE(d2r::parseUserDateTime("NOW ", kNow) == kNow);
}

TEST_CASE("parseUserDateTime: relative offsets subtract from now",
          "[dashboard_config][datetime]") {
    REQUIRE(d2r::parseUserDateTime("-5m",     kNow) == kNow - 5 * 60);
    REQUIRE(d2r::parseUserDateTime("-1h",     kNow) == kNow - 3600);
    REQUIRE(d2r::parseUserDateTime("-30s",    kNow) == kNow - 30);
    REQUIRE(d2r::parseUserDateTime("-1h30m",  kNow) == kNow - 3600 - 30 * 60);
    REQUIRE(d2r::parseUserDateTime("-2H",     kNow) == kNow - 2 * 3600);
    REQUIRE(d2r::parseUserDateTime("-1h2m3s", kNow) == kNow - 3600 - 2 * 60 - 3);
}

TEST_CASE("parseUserDateTime: rejects malformed relative offsets",
          "[dashboard_config][datetime]") {
    REQUIRE_FALSE(d2r::parseUserDateTime("-",     kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("-abc",  kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("-5",    kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("-0",    kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("-h",    kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("-5x",   kNow).has_value());
}

TEST_CASE("parseUserDateTime: absolute ISO-ish datetimes",
          "[dashboard_config][datetime]") {
    REQUIRE(d2r::parseUserDateTime("2026-07-29 15:30:00", kNow)
            == localEpoch(2026, 7, 29, 15, 30, 0));
    REQUIRE(d2r::parseUserDateTime("2026-07-29 15:30",    kNow)
            == localEpoch(2026, 7, 29, 15, 30, 0));
    REQUIRE(d2r::parseUserDateTime("2026-07-29",          kNow)
            == localEpoch(2026, 7, 29, 0, 0, 0));
    // Trims outer whitespace but rejects trailing junk.
    REQUIRE(d2r::parseUserDateTime("  2026-07-29 ",       kNow)
            == localEpoch(2026, 7, 29, 0, 0, 0));
    REQUIRE_FALSE(d2r::parseUserDateTime("2026-07-29 extra", kNow).has_value());
}

TEST_CASE("parseUserDateTime: today-relative HH:MM[:SS]",
          "[dashboard_config][datetime]") {
    REQUIRE(d2r::parseUserDateTime("14:15",     kNow)
            == localEpoch(2026, 7, 29, 14, 15, 0));
    REQUIRE(d2r::parseUserDateTime("14:15:30",  kNow)
            == localEpoch(2026, 7, 29, 14, 15, 30));
    REQUIRE(d2r::parseUserDateTime("00:00:00",  kNow)
            == localEpoch(2026, 7, 29, 0, 0, 0));
    REQUIRE(d2r::parseUserDateTime("23:59:59",  kNow)
            == localEpoch(2026, 7, 29, 23, 59, 59));
}

TEST_CASE("parseUserDateTime: rejects out-of-range fields",
          "[dashboard_config][datetime]") {
    REQUIRE_FALSE(d2r::parseUserDateTime("2026-13-01",         kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("2026-00-01",         kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("2026-07-32",         kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("2026-07-29 24:00",   kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("2026-07-29 12:60",   kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("1969-12-31",         kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("2101-01-01",         kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("25:00",              kNow).has_value());
}

TEST_CASE("parseUserDateTime: rejects nonsense strings",
          "[dashboard_config][datetime]") {
    REQUIRE_FALSE(d2r::parseUserDateTime("tomorrow", kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("nowish",   kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("29-07-26", kNow).has_value());
    REQUIRE_FALSE(d2r::parseUserDateTime("2026/7/29",kNow).has_value());
}
