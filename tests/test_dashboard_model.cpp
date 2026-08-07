// Catch2 tests for DashboardModel helpers exposed to the FTXUI layer.
// Focus areas: the SessionState's per-rune stack snapshot (populated
// by makeSessionStateFromSnapshot from a DashboardSnapshot's inventory)
// and the item-key set that drives the "new uniques / sets" diff.
//
// These tests build synthetic DashboardSnapshots directly rather than
// parsing real .d2s files, so they're independent of fixtures.

#include "d2r/DashboardModel.hpp"
#include "d2r/Item.hpp"
#include "d2r/RefDb.hpp"
#include "d2r/Save.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>

#include <unistd.h>

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

// Rune stack as it appears in the RotW material tab: a single
// InventoryItem with `stacks = N` instead of N individual instances.
d2r::InventoryItem makeRuneStack(std::string_view code, std::string_view name,
                                  std::uint16_t stackSize,
                                  std::string_view loc = "stash tab 6") {
    auto inv = makeRune(code, name, loc);
    inv.stacks = stackSize;
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

TEST_CASE("SessionState.runeStacks: sums stack sizes for material-tab piles",
          "[dashboard_model][runes]") {
    // D2R's material/rune tab stores each rune pile as a single
    // InventoryItem with `stacks = N`. Aggregation must sum stacks
    // instead of incrementing by 1 or a stack of 99 Amn Runes shows
    // up as 1 in "New Runes" / the Runes pane.
    d2r::DashboardSnapshot snap;
    snap.inventory.push_back(makeRuneStack("r11", "Amn Rune", 99));  // full stack
    snap.inventory.push_back(makeRuneStack("r05", "Eth Rune", 49));
    // Extra loose Amn on a character adds 1 (stacks == 0 -> treated as 1).
    snap.inventory.push_back(makeRune("r11", "Amn Rune", "Kai.d2s"));
    // A stack of 1 must still contribute 1 (not double-counted as 0+1).
    snap.inventory.push_back(makeRuneStack("r33", "Zod Rune", 1));

    const auto anchor = d2r::makeSessionStateFromSnapshot(snap);
    REQUIRE(anchor.runeStacks.size() == 3);
    REQUIRE(anchor.runeStacks.at("r11") == 100);  // 99 stack + 1 loose
    REQUIRE(anchor.runeStacks.at("r05") == 49);
    REQUIRE(anchor.runeStacks.at("r33") == 1);
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

TEST_CASE("SessionState diff: rune picked up during session and stashed shows "
          "as new",
          "[dashboard_model][session_loot][stash]") {
    // Direct reproduction of the user report: 'runes I store in the
    // stash don't count towards session loot'.
    //
    // Session start: character 'Kai' owns no runes; stash is empty of
    //                the code we'll test.
    // During play:   Kai picks up 2x Vex Rune (r26), moves them to
    //                shared stash tab 3, then Save & Exits.
    // Session end:   snapshot has 2x r26 at "stash tab 3".
    //
    // The Session Info pane's rune diff must surface r26 = +2. This
    // test exercises both makeSessionStateFromSnapshot (start-state
    // build) and the pane's inline nowStacks - startState arithmetic.
    d2r::DashboardSnapshot startSnap;
    startSnap.inventory.push_back(
        makeRune("r08", "Ral Rune", "stash tab 6"));   // pre-existing
    const auto startState = d2r::makeSessionStateFromSnapshot(startSnap);
    REQUIRE(startState.runeStacks.at("r08") == 1);
    REQUIRE(startState.runeStacks.count("r26") == 0);

    d2r::DashboardSnapshot nowSnap;
    // Pre-existing rune still there.
    nowSnap.inventory.push_back(
        makeRune("r08", "Ral Rune", "stash tab 6"));
    // Newly-stashed runes from this session.
    nowSnap.inventory.push_back(
        makeRune("r26", "Vex Rune", "stash tab 3"));
    nowSnap.inventory.push_back(
        makeRune("r26", "Vex Rune", "stash tab 3"));

    std::unordered_map<std::string, std::uint32_t> nowStacks;
    for (const auto& it : nowSnap.inventory) {
        if (it.code.size() == 3 && it.code[0] == 'r'
            && it.code[1] >= '0' && it.code[1] <= '9'
            && it.code[2] >= '0' && it.code[2] <= '9') {
            ++nowStacks[it.code];
        }
    }
    REQUIRE(nowStacks.at("r08") == 1);
    REQUIRE(nowStacks.at("r26") == 2);

    // Diff -- r08 unchanged, r26 = +2.
    std::unordered_map<std::string, std::int32_t> deltas;
    for (const auto& [code, now] : nowStacks) {
        const auto it = startState.runeStacks.find(code);
        const std::uint32_t before =
            (it == startState.runeStacks.end()) ? 0u : it->second;
        if (now > before) {
            deltas[code] = static_cast<std::int32_t>(now - before);
        }
    }
    REQUIRE(deltas.size() == 1);
    REQUIRE(deltas.count("r26") == 1);
    REQUIRE(deltas.at("r26") == 2);
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

// ---------------------------------------------------------------------------
// runDurationSecs.
// ---------------------------------------------------------------------------

TEST_CASE("runDurationSecs: closed run uses startEpoch -> endEpoch",
          "[dashboard_model][runs]") {
    // Run start is the previous SaveAndExit / session start (whichever
    // is later), NOT the first autosave. Duration must cover the whole
    // interval so that the sum across all runs in a session equals the
    // session duration -- i.e. the menu/select time between runs is
    // credited to the containing run.
    d2r::Run r;
    r.characterFile = "Kai.d2s";
    r.startEpoch    = 100;
    r.endEpoch      = 500;
    r.inProgress    = false;
    r.autosaveCount = 3;
    r.autosaveDates = {200, 300, 400};
    REQUIRE(d2r::runDurationSecs(r) == 400);   // 500 - 100
}

TEST_CASE("runDurationSecs: in-progress run with sessionEnd",
          "[dashboard_model][runs]") {
    // In-progress runs credit the span from startEpoch to sessionEnd.
    // This keeps the session-duration invariant satisfied even when a
    // run is still open (the trailing run absorbs the tail from the
    // previous SaveAndExit to the session's end).
    d2r::Run r;
    r.characterFile = "Kai.d2s";
    r.startEpoch    = 100;
    r.endEpoch      = 0;
    r.inProgress    = true;
    r.autosaveCount = 3;
    r.autosaveDates = {200, 300, 400};
    REQUIRE(d2r::runDurationSecs(r, /*sessionEnd=*/600) == 500);   // 600 - 100
}

TEST_CASE("runDurationSecs: in-progress run without sessionEnd falls back to "
          "last autosave",
          "[dashboard_model][runs]") {
    // Bare unit calls without a session-end context still return a
    // meaningful (but under-counted) value.
    d2r::Run r;
    r.characterFile = "Kai.d2s";
    r.startEpoch    = 100;
    r.endEpoch      = 0;
    r.inProgress    = true;
    r.autosaveCount = 3;
    r.autosaveDates = {200, 300, 400};
    REQUIRE(d2r::runDurationSecs(r) == 300);   // 400 - 100
}

TEST_CASE("runDurationSecs: closed run with no autosaves still counts menu time",
          "[dashboard_model][runs]") {
    // A bare SaveAndExit with no autosaves represents "the user
    // entered game and quit immediately" -- there was still menu/
    // load time between the previous SaveAndExit and this one, and
    // that time gets credited here.
    d2r::Run r;
    r.characterFile = "Kai.d2s";
    r.startEpoch    = 400;
    r.endEpoch      = 500;
    r.inProgress    = false;
    r.autosaveCount = 0;
    r.autosaveDates = {};
    REQUIRE(d2r::runDurationSecs(r) == 100);   // 500 - 400
}

// ---------------------------------------------------------------------------
// computeSessionRunStats.
// ---------------------------------------------------------------------------

namespace {

// Local scratch dir for backup DB tests.
struct RunStatsScratch {
    std::filesystem::path path;
    RunStatsScratch() {
        static std::atomic<int> counter{0};
        const auto pid = static_cast<long>(::getpid());
        const auto n   = counter.fetch_add(1, std::memory_order_relaxed);
        const auto ts  = std::chrono::system_clock::now().time_since_epoch().count();
        char name[128];
        std::snprintf(name, sizeof(name), "d2rsave-runstats-%ld-%ld-%d",
                      pid, static_cast<long>(ts), n);
        path = std::filesystem::temp_directory_path() / name;
        std::filesystem::create_directories(path);
    }
    ~RunStatsScratch() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

// Insert a row with the right shape for grouping (no data / checksum
// needed -- computeSessionRunStats only inspects date + state).
void insertRow(d2r::BackupDb& db, std::string_view filename,
                std::int64_t date, d2r::BackupDb::State st) {
    const std::string tiny = "x";
    db.insert(filename, date, st, /*checksum=*/0u,
              std::span<const std::byte>{
                  reinterpret_cast<const std::byte*>(tiny.data()),
                  tiny.size()});
}

// Add a fake d2s cache entry with a character name.
void addCacheEntry(d2r::DashboardFileCache& cache,
                    const std::string& filename,
                    const std::string& characterName) {
    d2r::DashboardFileCache::D2sEntry entry;
    entry.character.name = characterName;
    cache.d2s.emplace(filename, std::move(entry));
}

} // namespace

TEST_CASE("computeSessionRunStats: nullptr backupDb -> empty stats",
          "[dashboard_model][runs][stats]") {
    d2r::DashboardFileCache cache;
    addCacheEntry(cache, "Kai.d2s", "Kai");
    const auto stats = d2r::computeSessionRunStats(nullptr, cache, 0, 0);
    REQUIRE(stats.perCharacter.empty());
    REQUIRE(stats.totalRuns == 0);
    REQUIRE_FALSE(stats.anyInProgress);
    REQUIRE(stats.totalSecs == 0);
}

TEST_CASE("computeSessionRunStats: aggregates runs across two characters",
          "[dashboard_model][runs][stats]") {
    RunStatsScratch sc;
    d2r::BackupDb db(sc.path / "backups.sqlite");

    // Kai: two closed runs.
    //  Run 1: startEpoch=0 (sessionStart), SaveAndExit at 300 -> 300.
    //  Run 2: startEpoch=300 (prev SaveAndExit), SaveAndExit at 600 -> 300.
    insertRow(db, "Kai.d2s", 100, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 200, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 300, d2r::BackupDb::State::SaveAndExit);
    insertRow(db, "Kai.d2s", 400, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 500, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 600, d2r::BackupDb::State::SaveAndExit);
    // Barbarian: one closed run + trailing in-progress.
    //  Run 1: startEpoch=0, SaveAndExit at 350 -> 350.
    //  Run 2 in progress: startEpoch=350, sessionEnd=0 in this test
    //    (fallback to last autosave at 550) -> 200.
    insertRow(db, "Barbarian.d2s", 150, d2r::BackupDb::State::Autosave);
    insertRow(db, "Barbarian.d2s", 250, d2r::BackupDb::State::Autosave);
    insertRow(db, "Barbarian.d2s", 350, d2r::BackupDb::State::SaveAndExit);
    insertRow(db, "Barbarian.d2s", 450, d2r::BackupDb::State::Autosave);
    insertRow(db, "Barbarian.d2s", 550, d2r::BackupDb::State::Autosave);

    d2r::DashboardFileCache cache;
    addCacheEntry(cache, "Kai.d2s",       "Kai");
    addCacheEntry(cache, "Barbarian.d2s", "Karsh");

    const auto stats = d2r::computeSessionRunStats(&db, cache,
                                                     /*sessionStart=*/0,
                                                     /*sessionEnd=*/0);
    REQUIRE(stats.perCharacter.size() == 2);
    REQUIRE(stats.totalRuns == 3);      // 2 + 1 closed
    REQUIRE(stats.anyInProgress);
    REQUIRE(stats.totalSecs == 1150);   // 300 + 300 + 350 + 200

    // Order: descending by accumulatedSecs (Kai 600 > Karsh 550).
    REQUIRE(stats.perCharacter[0].characterName    == "Kai");
    REQUIRE(stats.perCharacter[0].runCount         == 2);
    REQUIRE_FALSE(stats.perCharacter[0].hasInProgress);
    REQUIRE(stats.perCharacter[0].accumulatedSecs  == 600);

    REQUIRE(stats.perCharacter[1].characterName    == "Karsh");
    REQUIRE(stats.perCharacter[1].runCount         == 1);
    REQUIRE(stats.perCharacter[1].hasInProgress);
    REQUIRE(stats.perCharacter[1].accumulatedSecs  == 550);
}

TEST_CASE("computeSessionRunStats: sessionStart clips out earlier runs",
          "[dashboard_model][runs][stats]") {
    RunStatsScratch sc;
    d2r::BackupDb db(sc.path / "backups.sqlite");
    // Run 1: dates 100..300 (before session).
    // Run 2: dates 400..600 (in session; sessionStart = 350).
    insertRow(db, "Kai.d2s", 100, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 200, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 300, d2r::BackupDb::State::SaveAndExit);
    insertRow(db, "Kai.d2s", 400, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 500, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 600, d2r::BackupDb::State::SaveAndExit);

    d2r::DashboardFileCache cache;
    addCacheEntry(cache, "Kai.d2s", "Kai");

    const auto stats = d2r::computeSessionRunStats(&db, cache,
                                                     /*sessionStart=*/350,
                                                     /*sessionEnd=*/0);
    REQUIRE(stats.perCharacter.size() == 1);
    REQUIRE(stats.totalRuns == 1);
    // Run 2's startEpoch is clipped to sessionStart (350) since the
    // previous SaveAndExit (300) is before it. Duration = 600 - 350.
    REQUIRE(stats.totalSecs == 250);
}

TEST_CASE("computeSessionRunStats: sessionEnd clips out later runs",
          "[dashboard_model][runs][stats]") {
    RunStatsScratch sc;
    d2r::BackupDb db(sc.path / "backups.sqlite");
    // Run 1 (in-window): dates 100..300, SaveAndExit at 300.
    // Run 2 (past-end): dates 400..600, SaveAndExit at 600.
    insertRow(db, "Kai.d2s", 100, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 200, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 300, d2r::BackupDb::State::SaveAndExit);
    insertRow(db, "Kai.d2s", 400, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 500, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 600, d2r::BackupDb::State::SaveAndExit);

    d2r::DashboardFileCache cache;
    addCacheEntry(cache, "Kai.d2s", "Kai");

    // sessionEnd = 350 excludes Run 2 (whose SaveAndExit is 600 > 350).
    const auto stats = d2r::computeSessionRunStats(&db, cache,
                                                     /*sessionStart=*/0,
                                                     /*sessionEnd=*/350);
    REQUIRE(stats.totalRuns == 1);
    // Run 1: startEpoch=0 (sessionStart), endEpoch=300. Duration = 300.
    REQUIRE(stats.totalSecs == 300);
}

TEST_CASE("computeSessionRunStats: sum-of-runs covers the entire session "
          "window (contiguous accounting)",
          "[dashboard_model][runs][stats]") {
    // The user-visible invariant this refactor was written to satisfy:
    // for a session with sessionStart <= earliest activity, the sum of
    // per-character accumulatedSecs equals the span from sessionStart
    // to the newest activity we count. No gaps between runs, no
    // double-count.
    RunStatsScratch sc;
    d2r::BackupDb db(sc.path / "backups.sqlite");
    // Kai: 3 closed runs; final SaveAndExit is at 900.
    insertRow(db, "Kai.d2s", 150, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 300, d2r::BackupDb::State::SaveAndExit);
    insertRow(db, "Kai.d2s", 450, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 600, d2r::BackupDb::State::SaveAndExit);
    insertRow(db, "Kai.d2s", 750, d2r::BackupDb::State::Autosave);
    insertRow(db, "Kai.d2s", 900, d2r::BackupDb::State::SaveAndExit);

    d2r::DashboardFileCache cache;
    addCacheEntry(cache, "Kai.d2s", "Kai");

    const std::int64_t sessionStart = 100;   // before any activity
    const std::int64_t sessionEnd   = 900;   // == last SaveAndExit
    const auto stats = d2r::computeSessionRunStats(&db, cache,
                                                     sessionStart,
                                                     sessionEnd);
    REQUIRE(stats.totalRuns == 3);
    REQUIRE_FALSE(stats.anyInProgress);
    // Session covers [100, 900] = 800 seconds. Sum of runs must equal.
    REQUIRE(stats.totalSecs == sessionEnd - sessionStart);
}

TEST_CASE("computeSessionRunStats: character name falls back to filename stem",
          "[dashboard_model][runs][stats]") {
    RunStatsScratch sc;
    d2r::BackupDb db(sc.path / "backups.sqlite");
    insertRow(db, "Warlock.d2s", 100, d2r::BackupDb::State::Autosave);
    insertRow(db, "Warlock.d2s", 200, d2r::BackupDb::State::SaveAndExit);

    d2r::DashboardFileCache cache;
    // Cache entry with EMPTY character name -> should fall back to
    // "Warlock" (stripping the ".d2s" suffix).
    addCacheEntry(cache, "Warlock.d2s", "");

    const auto stats = d2r::computeSessionRunStats(&db, cache, 0, 0);
    REQUIRE(stats.perCharacter.size() == 1);
    REQUIRE(stats.perCharacter[0].characterName == "Warlock");
}

// ---------------------------------------------------------------------------
// End-to-end: real shared-stash fixture -> aggregate -> makeSessionState.
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path fixturePath(const char* name) {
    if (const char* dir = std::getenv("D2R_FIXTURE_DIR")) {
        return std::filesystem::path(dir) / name;
    }
#ifdef D2R_TEST_FIXTURE_DIR
    return std::filesystem::path(D2R_TEST_FIXTURE_DIR) / name;
#else
    return std::filesystem::path("../../D2R_Saves") / name;
#endif
}

std::filesystem::path refDbFixturePath() {
    if (const char* env = std::getenv("D2R_REFERENCE_DB")) return env;
#ifdef D2R_TEST_REFERENCE_DB
    return D2R_TEST_REFERENCE_DB;
#else
    return "reference.sqlite";
#endif
}

d2r::RefDb& sharedFixtureDb() {
    static d2r::RefDb db(refDbFixturePath());
    static bool loaded = false;
    if (!loaded) { db.loadItemTables(); loaded = true; }
    return db;
}

} // namespace

TEST_CASE("Shared-stash fixture: stash runes flow into SessionState.runeStacks",
          "[dashboard_model][runes][stash][fixtures]") {
    // End-to-end reproduction of the user report 'runes I store in the
    // stash don't count towards session loot'. Reads the checked-in
    // fixture stash file, overlays it into a DashboardSnapshot, and
    // asserts:
    //   * runes at 'stash tab N' locations survive the overlay,
    //   * makeSessionStateFromSnapshot counts them as ONE entry per
    //     instance (a stash with 2 x Thul must show r10 = 2, not 1),
    //   * a follow-up snapshot with an extra rune produces a positive
    //     diff (nowStacks - startState.runeStacks) via the same
    //     arithmetic renderSessionLootPane uses.
    const auto stashBytes = d2r::readFile(
        fixturePath("ModernSharedStashSoftCoreV2.d2i").string());

    d2r::DashboardSnapshot snap;
    REQUIRE(d2r::overrideSharedStashFromBytes(snap, sharedFixtureDb(),
                                                stashBytes));

    // Collect stash-located runes we saw.
    std::unordered_map<std::string, std::uint32_t> stashRuneCounts;
    for (const auto& it : snap.inventory) {
        if (it.location.rfind("stash tab ", 0) != 0) continue;
        if (it.code.size() == 3 && it.code[0] == 'r'
            && it.code[1] >= '0' && it.code[1] <= '9'
            && it.code[2] >= '0' && it.code[2] <= '9') {
            ++stashRuneCounts[it.code];
        }
    }
    // Confirm the parser is producing per-instance rune rows: the
    // fixture is known to hold at least one code with multiple copies.
    // (The user asked to 'double check that you can see more than one
    // of each rune when you count from the stash'.)
    bool anyStackedInStash = false;
    for (const auto& [code, count] : stashRuneCounts) {
        if (count > 1) { anyStackedInStash = true; break; }
    }
    REQUIRE(anyStackedInStash);

    // makeSessionStateFromSnapshot must roll up ALL rune-code instances
    // from snap.inventory (including stash-located ones).
    const auto state = d2r::makeSessionStateFromSnapshot(snap);
    for (const auto& [code, count] : stashRuneCounts) {
        REQUIRE(state.runeStacks.count(code) == 1);
        REQUIRE(state.runeStacks.at(code) >= count);   // includes any char-side too
    }

    // Simulate 'picked up 3 x r33 (Zod) mid-session, stashed them'.
    // startState = snap (no r33 present in fixture stash count),
    // nowSnap   = snap + 3 x r33 at 'stash tab 3'.
    const auto beforeZod = state.runeStacks.count("r33")
                              ? state.runeStacks.at("r33") : 0u;
    d2r::DashboardSnapshot nowSnap = snap;
    for (int i = 0; i < 3; ++i) {
        nowSnap.inventory.push_back(
            makeRune("r33", "Zod Rune", "stash tab 3"));
    }
    // Recompute nowStacks the way renderSessionLootPane does.
    std::unordered_map<std::string, std::uint32_t> nowStacks;
    for (const auto& it : nowSnap.inventory) {
        if (it.code.size() == 3 && it.code[0] == 'r'
            && it.code[1] >= '0' && it.code[1] <= '9'
            && it.code[2] >= '0' && it.code[2] <= '9') {
            ++nowStacks[it.code];
        }
    }
    REQUIRE(nowStacks.at("r33") == beforeZod + 3);
    // Diff must equal +3.
    const std::uint32_t startZod = state.runeStacks.count("r33")
                                       ? state.runeStacks.at("r33") : 0u;
    REQUIRE((nowStacks.at("r33") - startZod) == 3u);
}

TEST_CASE("Full-account buildSnapshot fixture: stash + character runes appear "
          "with per-instance counts",
          "[dashboard_model][runes][stash][fixtures]") {
    // Higher-level than the previous test: walks the ENTIRE account
    // (all .d2s + the .d2i) via buildSnapshot, then asserts that
    // stash-located runes actually land in snap.inventory. This is
    // the path that runs at every dashboard rebuild -- if it drops
    // stash items on the floor, no downstream fix can rescue it.
    const auto fixtureDir  = fixturePath("").parent_path();
    const auto stashPath   = fixturePath("ModernSharedStashSoftCoreV2.d2i");
    const auto snap        = d2r::buildSnapshot(sharedFixtureDb(),
                                                 fixtureDir, stashPath);

    // Count rune instances at each location kind.
    std::size_t stashRuneInstances = 0;
    std::size_t charRuneInstances  = 0;
    std::unordered_map<std::string, std::uint32_t> stashByCode;
    for (const auto& it : snap.inventory) {
        if (it.code.size() != 3 || it.code[0] != 'r') continue;
        if (it.code[1] < '0' || it.code[1] > '9') continue;
        if (it.code[2] < '0' || it.code[2] > '9') continue;
        if (it.location.rfind("stash tab ", 0) == 0) {
            ++stashRuneInstances;
            ++stashByCode[it.code];
        } else {
            ++charRuneInstances;
        }
    }
    // The fixture is known to hold plenty of runes in the stash --
    // this is the concrete assertion the user asked for: "you can
    // see more than one of each rune when you count from the stash."
    REQUIRE(stashRuneInstances > 0);
    bool sawMultiInstance = false;
    for (const auto& [code, count] : stashByCode) {
        if (count > 1) { sawMultiInstance = true; break; }
    }
    REQUIRE(sawMultiInstance);

    // SessionState.runeStacks must include EVERY stash rune instance;
    // a code with N copies in stash gets a count of at least N (may
    // be higher when a character also holds the same code).
    const auto state = d2r::makeSessionStateFromSnapshot(snap);
    for (const auto& [code, count] : stashByCode) {
        REQUIRE(state.runeStacks.at(code) >= count);
    }
}
