// Catch2 tests for DashboardModel helpers exposed to the FTXUI layer.
// Focus areas: the SessionState's per-rune stack snapshot (populated
// by makeSessionStateFromSnapshot from a DashboardSnapshot's inventory)
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

TEST_CASE("SessionState.runeStacks: empty inventory -> empty map",
          "[dashboard_model][runes]") {
    d2r::DashboardSnapshot snap;
    const auto anchor = d2r::makeSessionStateFromSnapshot(snap);
    REQUIRE(anchor.runeStacks.empty());
}

TEST_CASE("SessionState.runeStacks: counts one entry per rune instance",
          "[dashboard_model][runes]") {
    d2r::DashboardSnapshot snap;
    // 3 x Hel, 1 x Ko, 2 x Um.
    snap.inventory.push_back(makeRune("r15", "Hel Rune"));
    snap.inventory.push_back(makeRune("r15", "Hel Rune"));
    snap.inventory.push_back(makeRune("r15", "Hel Rune"));
    snap.inventory.push_back(makeRune("r18", "Ko Rune"));
    snap.inventory.push_back(makeRune("r22", "Um Rune"));
    snap.inventory.push_back(makeRune("r22", "Um Rune"));

    const auto anchor = d2r::makeSessionStateFromSnapshot(snap);
    REQUIRE(anchor.runeStacks.size() == 3);
    REQUIRE(anchor.runeStacks.at("r15") == 3);
    REQUIRE(anchor.runeStacks.at("r18") == 1);
    REQUIRE(anchor.runeStacks.at("r22") == 2);
}

TEST_CASE("SessionState.runeStacks: ignores non-rune codes",
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

    const auto anchor = d2r::makeSessionStateFromSnapshot(snap);
    REQUIRE(anchor.runeStacks.size() == 1);
    REQUIRE(anchor.runeStacks.at("r08") == 1);
}

TEST_CASE("SessionState: unique/set fingerprints go into itemKeys",
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

    const auto anchor = d2r::makeSessionStateFromSnapshot(snap);
    REQUIRE(anchor.itemKeys.size() == 3);
    REQUIRE(anchor.itemKeys.contains({0xAAAA1111u, d2r::ItemQuality::Unique}));
    REQUIRE(anchor.itemKeys.contains({0xBBBB2222u, d2r::ItemQuality::Unique}));
    REQUIRE(anchor.itemKeys.contains({0xCCCC3333u, d2r::ItemQuality::Set}));
    REQUIRE_FALSE(anchor.itemKeys.contains({0xDDDD4444u, d2r::ItemQuality::Unique}));
    REQUIRE_FALSE(anchor.itemKeys.contains({0u,         d2r::ItemQuality::Unique}));
}

TEST_CASE("SessionState round-trip: session-loot diff finds only positive rune deltas",
          "[dashboard_model][session_loot]") {
    // Anchor snapshot: 2 Hel + 1 Ko.
    d2r::DashboardSnapshot anchorSnap;
    anchorSnap.inventory.push_back(makeRune("r15", "Hel Rune"));
    anchorSnap.inventory.push_back(makeRune("r15", "Hel Rune"));
    anchorSnap.inventory.push_back(makeRune("r18", "Ko Rune"));
    const auto anchor = d2r::makeSessionStateFromSnapshot(anchorSnap);

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

TEST_CASE("SessionState diff: rune moved between locations is not 'new'",
          "[dashboard_model][session_loot][cross_character]") {
    // Regression for the "socketed runes appear as new when armor moves
    // via shared stash" bug. The user reported: a body armor with Ith,
    // Ber, Jah socketed was moved from one character to another via the
    // shared stash during a session; the runes then surfaced in the
    // Session Info pane as "new" even though the account owned them the
    // whole time.
    //
    // The invariant this test pins: the diff sees an INSTANCE COUNT per
    // code, not a per-character allotment. If the anchor snapshot (as
    // reconstructed by buildSessionState) accounts for the runes on
    // whichever character owned them at anchor time, moving them
    // elsewhere by "now" leaves the count unchanged -> zero new runes.
    //
    // Sanity: the fix in buildSessionState is what makes the anchor
    // snapshot include runes owned by NON-active-player characters at
    // anchor time. This test only verifies makeSessionState +
    // diff-logic play their part correctly once the anchor snapshot is
    // right.
    d2r::DashboardSnapshot anchorSnap;
    // Anchor state: Ith / Ber / Jah are socketed in an armor on
    // 'UniqueSwordsEl.d2s' (any 'other' character will do; the location
    // string just needs to differ from the active player's below).
    anchorSnap.inventory.push_back(makeRune("r06", "Ith Rune",  "UniqueSwordsEl.d2s"));
    anchorSnap.inventory.push_back(makeRune("r30", "Ber Rune",  "UniqueSwordsEl.d2s"));
    anchorSnap.inventory.push_back(makeRune("r31", "Jah Rune",  "UniqueSwordsEl.d2s"));
    const auto anchor = d2r::makeSessionStateFromSnapshot(anchorSnap);

    // Now state: same three runes, now on 'Barbarian.d2s' (armor moved
    // via shared stash). Counts per code are unchanged.
    d2r::DashboardSnapshot nowSnap;
    nowSnap.inventory.push_back(makeRune("r06", "Ith Rune", "Barbarian.d2s"));
    nowSnap.inventory.push_back(makeRune("r30", "Ber Rune", "Barbarian.d2s"));
    nowSnap.inventory.push_back(makeRune("r31", "Jah Rune", "Barbarian.d2s"));

    std::unordered_map<std::string, std::uint32_t> nowStacks;
    for (const auto& it : nowSnap.inventory) {
        if (it.code.size() == 3 && it.code[0] == 'r'
            && it.code[1] >= '0' && it.code[1] <= '9'
            && it.code[2] >= '0' && it.code[2] <= '9') {
            ++nowStacks[it.code];
        }
    }
    // Diff should produce zero rows: every code has the same count.
    int newRows = 0;
    for (const auto& [code, now] : nowStacks) {
        const auto it = anchor.runeStacks.find(code);
        const std::uint32_t before = it == anchor.runeStacks.end() ? 0u : it->second;
        if (now > before) ++newRows;
    }
    REQUIRE(newRows == 0);
}

TEST_CASE("SessionState diff: empty anchor -> everything current is 'new'",
          "[dashboard_model][session_loot][pre_history_pin]") {
    // Pins the invariant behind the "pin before any save existed"
    // request: buildSessionState's pinned branch fetches per-file
    // bytes at pin. Files with no backup at/before pin get their
    // items stripped from `temp`. When the pin predates every save,
    // every file's contribution is stripped, so the anchor's
    // inventory is empty. Once the anchor is empty, the diff must
    // report every currently-owned item as "new" -- including any
    // that were on some OTHER character at anchor time (they didn't
    // exist yet at anchor time either). This test verifies the diff
    // half of that path via makeSessionStateFromSnapshot on an
    // empty snapshot.
    const d2r::DashboardSnapshot emptyAnchor;
    const auto anchor = d2r::makeSessionStateFromSnapshot(emptyAnchor);
    REQUIRE(anchor.itemKeys.empty());
    REQUIRE(anchor.runeStacks.empty());

    d2r::DashboardSnapshot nowSnap;
    // A handful of runes across two characters + a unique.
    nowSnap.inventory.push_back(makeRune("r06",  "Ith Rune", "Barbarian.d2s"));
    nowSnap.inventory.push_back(makeRune("r30",  "Ber Rune", "Barbarian.d2s"));
    nowSnap.inventory.push_back(makeRune("r31",  "Jah Rune", "Warlock.d2s"));
    nowSnap.inventory.push_back(makeUnique(0xAAAA1111u, "Windforce"));

    // Uniques diff: fingerprint set is empty in anchor -> the unique
    // is "new".
    int newUniques = 0;
    for (const auto& it : nowSnap.inventory) {
        if (!it.identified) continue;
        if (it.fingerprint == 0) continue;
        if (it.quality != d2r::ItemQuality::Unique
         && it.quality != d2r::ItemQuality::Set) continue;
        if (anchor.itemKeys.contains({it.fingerprint, it.quality})) continue;
        ++newUniques;
    }
    REQUIRE(newUniques == 1);

    // Runes diff: nowStacks vs empty anchor stacks -> every code is new.
    std::unordered_map<std::string, std::uint32_t> nowStacks;
    for (const auto& it : nowSnap.inventory) {
        if (it.code.size() == 3 && it.code[0] == 'r'
            && it.code[1] >= '0' && it.code[1] <= '9'
            && it.code[2] >= '0' && it.code[2] <= '9') {
            ++nowStacks[it.code];
        }
    }
    int newRuneRows = 0;
    for (const auto& [code, now] : nowStacks) {
        const auto it = anchor.runeStacks.find(code);
        const std::uint32_t before = it == anchor.runeStacks.end() ? 0u : it->second;
        if (now > before) ++newRuneRows;
    }
    REQUIRE(newRuneRows == 3);
}

// ---------------------------------------------------------------------------
// groupRunsForFile: chronological grouping of a file's HistoryRow list
// into Runs bounded by SaveAndExit backups.
// ---------------------------------------------------------------------------

namespace {

// Fabricate a HistoryRow with only the fields the grouping cares about.
// `historyFor` returns rows date-DESC, so callers should push newest
// first when building fixtures.
d2r::BackupDb::HistoryRow mkRow(std::int64_t date, d2r::BackupDb::State st) {
    d2r::BackupDb::HistoryRow r;
    r.date  = date;
    r.state = st;
    return r;
}

} // namespace

TEST_CASE("groupRunsForFile: empty history -> no runs",
          "[dashboard_model][runs]") {
    std::vector<d2r::BackupDb::HistoryRow> hist;
    const auto runs = d2r::groupRunsForFile("Kai.d2s", hist, /*sessionStart=*/0);
    REQUIRE(runs.empty());
}

TEST_CASE("groupRunsForFile: two complete runs separated by a SaveAndExit",
          "[dashboard_model][runs]") {
    // Chronology (oldest -> newest):
    //   100 Autosave     (Play click)
    //   200 Autosave     (gem combine)
    //   300 SaveAndExit  (end of run #1)
    //   400 Autosave     (Play click, run #2 starts)
    //   500 Autosave
    //   600 Autosave
    //   700 SaveAndExit  (end of run #2)
    //
    // historyFor returns date DESC, so we build the vector newest first.
    std::vector<d2r::BackupDb::HistoryRow> hist{
        mkRow(700, d2r::BackupDb::State::SaveAndExit),
        mkRow(600, d2r::BackupDb::State::Autosave),
        mkRow(500, d2r::BackupDb::State::Autosave),
        mkRow(400, d2r::BackupDb::State::Autosave),
        mkRow(300, d2r::BackupDb::State::SaveAndExit),
        mkRow(200, d2r::BackupDb::State::Autosave),
        mkRow(100, d2r::BackupDb::State::Autosave),
    };
    const auto runs = d2r::groupRunsForFile("Kai.d2s", hist, /*sessionStart=*/0);
    REQUIRE(runs.size() == 2);

    // Runs are emitted oldest-first.
    REQUIRE(runs[0].characterFile == "Kai.d2s");
    REQUIRE(runs[0].startEpoch    == 0);      // session start (no clip)
    REQUIRE(runs[0].endEpoch      == 300);
    REQUIRE_FALSE(runs[0].inProgress);
    REQUIRE(runs[0].autosaveCount == 2);
    REQUIRE(runs[0].autosaveDates == std::vector<std::int64_t>{100, 200});

    REQUIRE(runs[1].startEpoch    == 300);    // previous SaveAndExit
    REQUIRE(runs[1].endEpoch      == 700);
    REQUIRE_FALSE(runs[1].inProgress);
    REQUIRE(runs[1].autosaveCount == 3);
    REQUIRE(runs[1].autosaveDates == std::vector<std::int64_t>{400, 500, 600});
}

TEST_CASE("groupRunsForFile: trailing autosaves emit an in-progress run",
          "[dashboard_model][runs]") {
    // 100 Autosave
    // 200 SaveAndExit   <- closes run #1
    // 300 Autosave      <- run #2 starts (user is still playing)
    // 400 Autosave
    std::vector<d2r::BackupDb::HistoryRow> hist{
        mkRow(400, d2r::BackupDb::State::Autosave),
        mkRow(300, d2r::BackupDb::State::Autosave),
        mkRow(200, d2r::BackupDb::State::SaveAndExit),
        mkRow(100, d2r::BackupDb::State::Autosave),
    };
    const auto runs = d2r::groupRunsForFile("Barbarian.d2s", hist,
                                             /*sessionStart=*/0);
    REQUIRE(runs.size() == 2);

    REQUIRE(runs[0].endEpoch      == 200);
    REQUIRE_FALSE(runs[0].inProgress);

    REQUIRE(runs[1].startEpoch    == 200);
    REQUIRE(runs[1].endEpoch      == 0);
    REQUIRE(runs[1].inProgress);
    REQUIRE(runs[1].autosaveCount == 2);
    REQUIRE(runs[1].autosaveDates == std::vector<std::int64_t>{300, 400});
}

TEST_CASE("groupRunsForFile: sessionStart clips earlier runs; straddling run "
          "has its startEpoch clipped",
          "[dashboard_model][runs]") {
    // Session starts at 350.
    // 100 Autosave      (skipped: before session)
    // 200 SaveAndExit   (skipped: before session)
    // 300 Autosave      (skipped: before session)
    // 400 Autosave      (in session, run #1 body)
    // 500 SaveAndExit   (in session, closes run #1)
    // 600 Autosave      (in session, run #2 body -- in-progress)
    std::vector<d2r::BackupDb::HistoryRow> hist{
        mkRow(600, d2r::BackupDb::State::Autosave),
        mkRow(500, d2r::BackupDb::State::SaveAndExit),
        mkRow(400, d2r::BackupDb::State::Autosave),
        mkRow(300, d2r::BackupDb::State::Autosave),
        mkRow(200, d2r::BackupDb::State::SaveAndExit),
        mkRow(100, d2r::BackupDb::State::Autosave),
    };
    const auto runs = d2r::groupRunsForFile("Kai.d2s", hist,
                                             /*sessionStart=*/350);
    REQUIRE(runs.size() == 2);

    // Run that straddles the boundary has left bound clipped to sessionStart.
    REQUIRE(runs[0].startEpoch    == 350);
    REQUIRE(runs[0].endEpoch      == 500);
    REQUIRE_FALSE(runs[0].inProgress);
    REQUIRE(runs[0].autosaveCount == 1);
    REQUIRE(runs[0].autosaveDates == std::vector<std::int64_t>{400});

    // Next run's left bound is the previous SaveAndExit at 500, not
    // the sessionStart.
    REQUIRE(runs[1].startEpoch    == 500);
    REQUIRE(runs[1].endEpoch      == 0);
    REQUIRE(runs[1].inProgress);
    REQUIRE(runs[1].autosaveCount == 1);
    REQUIRE(runs[1].autosaveDates == std::vector<std::int64_t>{600});
}

TEST_CASE("groupRunsForFile: Deleted and Startup rows are non-run signals",
          "[dashboard_model][runs]") {
    // A Startup row (dashboard bootstrap) and a Deleted row (tombstone)
    // land between two Autosaves; neither should count against the
    // Run accumulator's autosaveCount or open/close any run.
    std::vector<d2r::BackupDb::HistoryRow> hist{
        mkRow(500, d2r::BackupDb::State::SaveAndExit),
        mkRow(400, d2r::BackupDb::State::Autosave),
        mkRow(350, d2r::BackupDb::State::Startup),   // ignored
        mkRow(300, d2r::BackupDb::State::Autosave),
        mkRow(250, d2r::BackupDb::State::Deleted),   // ignored
        mkRow(200, d2r::BackupDb::State::Autosave),
    };
    const auto runs = d2r::groupRunsForFile("Kai.d2s", hist,
                                             /*sessionStart=*/0);
    REQUIRE(runs.size() == 1);
    REQUIRE(runs[0].endEpoch      == 500);
    REQUIRE(runs[0].autosaveCount == 3);
    REQUIRE(runs[0].autosaveDates ==
            std::vector<std::int64_t>{200, 300, 400});
}
