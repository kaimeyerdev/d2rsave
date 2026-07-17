// Catch2 tests for BackupDb (schema, insert, retrieve, retention).

#include "d2r/BackupDb.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using d2r::BackupDb;

namespace {

// Unique scratch dir per test case, cleaned up on scope exit.
struct ScratchDir {
    fs::path path;
    ScratchDir() {
        static std::atomic<int> counter{0};
        const auto pid = static_cast<long>(::getpid());
        const auto n   = counter.fetch_add(1, std::memory_order_relaxed);
        const auto ts  = std::chrono::system_clock::now().time_since_epoch().count();
        char name[128];
        std::snprintf(name, sizeof(name), "d2rsave-backuptest-%ld-%ld-%d",
                      pid, static_cast<long>(ts), n);
        path = fs::temp_directory_path() / name;
        fs::create_directories(path);
    }
    ~ScratchDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::span<const std::byte> asBytes(std::string_view s) {
    return { reinterpret_cast<const std::byte*>(s.data()), s.size() };
}

} // namespace

TEST_CASE("BackupDb creates its schema on first open", "[backup][db]") {
    ScratchDir sd;
    const auto dbPath = sd.path / "backups.sqlite";
    REQUIRE_FALSE(fs::exists(dbPath));
    {
        BackupDb db(dbPath);
        REQUIRE(fs::exists(dbPath));
    }
    // Re-open on the same path is a no-op (schema uses IF NOT EXISTS).
    BackupDb db(dbPath);
    (void) db;
}

TEST_CASE("BackupDb insert + at round-trip", "[backup][db]") {
    ScratchDir sd;
    BackupDb db(sd.path / "backups.sqlite");
    const std::string payload = "hello world";
    db.insert("Kai.d2s", 1000, BackupDb::State::Autosave, asBytes(payload));

    auto row = db.at("Kai.d2s", 1500);
    REQUIRE(row.has_value());
    REQUIRE(row->state == BackupDb::State::Autosave);
    REQUIRE(row->data.size() == payload.size());
    REQUIRE(std::string(reinterpret_cast<const char*>(row->data.data()),
                        row->data.size()) == payload);

    // Nothing predates t=999.
    REQUIRE_FALSE(db.at("Kai.d2s", 999).has_value());
}

TEST_CASE("BackupDb picks the newest row <= askUnix", "[backup][db]") {
    ScratchDir sd;
    BackupDb db(sd.path / "backups.sqlite");
    db.insert("Kai.d2s", 1000, BackupDb::State::Startup,     asBytes("a"));
    db.insert("Kai.d2s", 2000, BackupDb::State::Autosave,    asBytes("b"));
    db.insert("Kai.d2s", 3000, BackupDb::State::SaveAndExit, asBytes("c"));

    REQUIRE(std::string(reinterpret_cast<const char*>(db.at("Kai.d2s", 1500)->data.data()),
                        db.at("Kai.d2s", 1500)->data.size()) == "a");
    REQUIRE(std::string(reinterpret_cast<const char*>(db.at("Kai.d2s", 2500)->data.data()),
                        db.at("Kai.d2s", 2500)->data.size()) == "b");
    REQUIRE(std::string(reinterpret_cast<const char*>(db.at("Kai.d2s", 9999)->data.data()),
                        db.at("Kai.d2s", 9999)->data.size()) == "c");
}

TEST_CASE("BackupDb tombstone: NULL data + state=Deleted", "[backup][db]") {
    ScratchDir sd;
    BackupDb db(sd.path / "backups.sqlite");
    db.insertTombstone("Kai.d2s", 1000);
    auto row = db.at("Kai.d2s", 1500);
    REQUIRE(row.has_value());
    REQUIRE(row->state == BackupDb::State::Deleted);
    REQUIRE(row->data.empty());
}

TEST_CASE("BackupDb summariseFiles + historyFor", "[backup][db]") {
    ScratchDir sd;
    BackupDb db(sd.path / "backups.sqlite");
    db.insert("Kai.d2s",  1000, BackupDb::State::Startup,     asBytes("k1"));
    db.insert("Kai.d2s",  1200, BackupDb::State::Autosave,    asBytes("k2"));
    db.insert("Kai.d2s",  1400, BackupDb::State::SaveAndExit, asBytes("k3"));
    db.insert("Warlock.d2s", 500, BackupDb::State::SaveAndExit, asBytes("w"));

    const auto sums = db.summariseFiles();
    REQUIRE(sums.size() == 2);
    // Ordered by most-recent lastDate first.
    REQUIRE(sums[0].filename == "Kai.d2s");
    REQUIRE(sums[0].lastDate == 1400);
    REQUIRE(sums[0].lastState == BackupDb::State::SaveAndExit);
    REQUIRE(sums[0].backupCount == 3);
    REQUIRE(sums[0].sessionCount == 1);   // one state=1 row
    REQUIRE(sums[1].filename == "Warlock.d2s");
    REQUIRE(sums[1].sessionCount == 1);

    const auto hist = db.historyFor("Kai.d2s", 10);
    REQUIRE(hist.size() == 3);
    REQUIRE(hist[0].date == 1400);
    REQUIRE(hist[0].state == BackupDb::State::SaveAndExit);
    REQUIRE(hist[0].sizeBytes == 2);
    REQUIRE(hist[2].date == 1000);
}

TEST_CASE("BackupDb enforceRetention: date rule alone", "[backup][db][retention]") {
    ScratchDir sd;
    BackupDb db(sd.path / "backups.sqlite");
    // 5 rows, one per day going back.
    for (int i = 0; i < 5; ++i) {
        db.insert("Kai.d2s", 10000 - i * 86400,
                  BackupDb::State::Autosave, asBytes("x"));
    }
    // Keep 2 days worth; sessionsPerFile=0 disables session rule.
    const auto deleted = db.enforceRetention(/*days=*/2,
                                              /*sessionsPerFile=*/0,
                                              /*now=*/10000);
    // dateCutoff = 10000 - 2*86400 = -162800.
    // Rows kept: date >= -162800. All rows are 10000, 10000-86400,
    // 10000-2*86400, 10000-3*86400, 10000-4*86400 = 10000, -76400,
    // -162800, -249200, -335600. dates < -162800 -> 2 rows deleted.
    REQUIRE(deleted == 2);
    REQUIRE(db.historyFor("Kai.d2s", 100).size() == 3);
}

TEST_CASE("BackupDb enforceRetention: session rule keeps last Y sessions",
          "[backup][db][retention]") {
    ScratchDir sd;
    BackupDb db(sd.path / "backups.sqlite");
    // Build a history of 4 completed sessions on Kai.d2s. Each session:
    // (Startup or Autosave) then (SaveAndExit). Session K starts at
    // date K*100 and ends at K*100 + 50.
    for (int k = 1; k <= 4; ++k) {
        db.insert("Kai.d2s",  k * 100 +  0, BackupDb::State::Startup,     asBytes("s"));
        db.insert("Kai.d2s",  k * 100 + 20, BackupDb::State::Autosave,    asBytes("a"));
        db.insert("Kai.d2s",  k * 100 + 50, BackupDb::State::SaveAndExit, asBytes("e"));
    }
    // Keep last 2 sessions. Date cutoff for session rule = date of
    // 3rd-most-recent state=1 = 200 + 50 = 250. Rows with date > 250
    // survive: those from sessions 3 (dates 300, 320, 350) and 4
    // (dates 400, 420, 450) = 6 rows kept.
    // days=0 -> date cutoff = INT64_MAX -> no date-based keep.
    const auto deleted = db.enforceRetention(/*days=*/0,
                                              /*sessionsPerFile=*/2,
                                              /*now=*/9999);
    REQUIRE(deleted == 6);
    const auto hist = db.historyFor("Kai.d2s", 100);
    REQUIRE(hist.size() == 6);
    // All surviving dates should be > 250.
    for (const auto& r : hist) REQUIRE(r.date > 250);
}

TEST_CASE("BackupDb enforceRetention: fewer sessions than target keeps all",
          "[backup][db][retention]") {
    ScratchDir sd;
    BackupDb db(sd.path / "backups.sqlite");
    // Only 1 completed session; ask for 5.
    db.insert("Kai.d2s", 100, BackupDb::State::Startup,     asBytes("s"));
    db.insert("Kai.d2s", 200, BackupDb::State::SaveAndExit, asBytes("e"));
    db.insert("Kai.d2s", 250, BackupDb::State::Autosave,    asBytes("a"));  // in-progress

    const auto deleted = db.enforceRetention(0, 5, 9999);
    REQUIRE(deleted == 0);
    REQUIRE(db.historyFor("Kai.d2s", 100).size() == 3);
}

TEST_CASE("BackupDb enforceRetention: stash inherits kept character range",
          "[backup][db][retention]") {
    ScratchDir sd;
    BackupDb db(sd.path / "backups.sqlite");
    // Kai session 1: dates 100..200 (SaveAndExit at 200).
    // Kai session 2: dates 300..400 (SaveAndExit at 400).
    db.insert("Kai.d2s", 100, BackupDb::State::Startup,     asBytes("s"));
    db.insert("Kai.d2s", 200, BackupDb::State::SaveAndExit, asBytes("e"));
    db.insert("Kai.d2s", 300, BackupDb::State::Autosave,    asBytes("a"));
    db.insert("Kai.d2s", 400, BackupDb::State::SaveAndExit, asBytes("e"));

    // Stash writes at 150 (in session 1), 350 (in session 2), and 500
    // (after everything).
    db.insert("Shared.d2i", 150, BackupDb::State::Autosave, asBytes("i150"));
    db.insert("Shared.d2i", 350, BackupDb::State::Autosave, asBytes("i350"));
    db.insert("Shared.d2i", 500, BackupDb::State::Autosave, asBytes("i500"));

    // Keep last 1 Kai session -> Kai rows at 300 and 400 survive.
    // Kai range for rule (c) becomes [300, 400].
    // Stash rows: 150 (out of range, deleted), 350 (in range, kept),
    //             500 (out of range, deleted).
    const auto deleted = db.enforceRetention(0, 1, 9999);
    REQUIRE(deleted == 4);   // Kai 100+200 + Stash 150+500

    const auto kai = db.historyFor("Kai.d2s", 10);
    REQUIRE(kai.size() == 2);
    const auto stash = db.historyFor("Shared.d2i", 10);
    REQUIRE(stash.size() == 1);
    REQUIRE(stash[0].date == 350);
}
