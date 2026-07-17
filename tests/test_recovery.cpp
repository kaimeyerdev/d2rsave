// Catch2 tests for recovery.

#include "d2r/BackupDb.hpp"
#include "d2r/BackupScheduler.hpp"
#include "d2r/Recovery.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using d2r::BackupDb;
using d2r::BackupScheduler;
using d2r::RecoverySpec;

namespace {

struct Scratch {
    fs::path root;
    fs::path savesDir;
    fs::path altDir;
    fs::path dbPath;
    Scratch() {
        static std::atomic<int> counter{0};
        const auto pid = static_cast<long>(::getpid());
        const auto n   = counter.fetch_add(1, std::memory_order_relaxed);
        const auto ts  = std::chrono::system_clock::now().time_since_epoch().count();
        char name[128];
        std::snprintf(name, sizeof(name), "d2rsave-recovery-%ld-%ld-%d",
                      pid, static_cast<long>(ts), n);
        root     = fs::temp_directory_path() / name;
        savesDir = root / "saves";
        altDir   = root / "alt";
        dbPath   = root / "backups.sqlite";
        fs::create_directories(savesDir);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

std::span<const std::byte> asBytes(std::string_view s) {
    return { reinterpret_cast<const std::byte*>(s.data()), s.size() };
}

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::string   s{std::istreambuf_iterator<char>(f), {}};
    return s;
}

} // namespace

TEST_CASE("Recovery: round-trip to primary saves dir",
          "[backup][recovery]") {
    Scratch sc;
    BackupDb db(sc.dbPath);
    BackupScheduler sched(db, sc.savesDir);

    // Seed: two versions of Kai.d2s in the backup DB.
    db.insert("Kai.d2s", 1000, BackupDb::State::Autosave, asBytes("old"));
    db.insert("Kai.d2s", 2000, BackupDb::State::Autosave, asBytes("new"));

    // Recover the "old" version to the saves dir.
    RecoverySpec spec;
    spec.destDir  = sc.savesDir;
    spec.filename = "Kai.d2s";
    spec.atUnix   = 1500;   // between old and new
    spec.preRecoveryBackup = false;   // nothing to snapshot; dest is empty

    const auto rep = d2r::recoverFile(db, sched, spec);
    REQUIRE(rep.restored);
    REQUIRE_FALSE(rep.wasTombstone);
    REQUIRE(rep.bytesWritten == 3);
    REQUIRE(rep.recoveredDate == 1000);
    REQUIRE(readAll(sc.savesDir / "Kai.d2s") == "old");
}

TEST_CASE("Recovery: to alternate folder is byte-identical",
          "[backup][recovery]") {
    Scratch sc;
    BackupDb db(sc.dbPath);
    BackupScheduler sched(db, sc.savesDir);

    const std::string blob = "some\0binary\0bytes\0with-nulls";
    db.insert("Shared.d2i", 1000, BackupDb::State::SaveAndExit,
              std::span{reinterpret_cast<const std::byte*>(blob.data()), blob.size()});

    RecoverySpec spec;
    spec.destDir  = sc.altDir;   // doesn't exist yet
    spec.filename = "Shared.d2i";
    spec.atUnix   = 5000;
    spec.preRecoveryBackup     = false;
    spec.allowTombstoneRestore = false;

    const auto rep = d2r::recoverFile(db, sched, spec);
    REQUIRE(rep.restored);
    REQUIRE(rep.bytesWritten == static_cast<std::int64_t>(blob.size()));
    REQUIRE(fs::exists(sc.altDir / "Shared.d2i"));
    const auto got = readAll(sc.altDir / "Shared.d2i");
    REQUIRE(got.size() == blob.size());
    REQUIRE(std::memcmp(got.data(), blob.data(), blob.size()) == 0);
}

TEST_CASE("Recovery: pre-recovery snapshot captures current bytes",
          "[backup][recovery]") {
    Scratch sc;
    BackupDb db(sc.dbPath);
    // Lenient retention so the seeded t=1000 row isn't pruned by the
    // manual snapshot's own retention pass.
    BackupScheduler sched(db, sc.savesDir, d2r::RetentionConfig{1'000'000, 0});

    // Existing file on disk (a "current" version not yet in the DB).
    {
        std::ofstream f(sc.savesDir / "Shared.d2i", std::ios::binary);
        f << "current-live-bytes";
    }
    // Seed the DB with an older version to restore.
    db.insert("Shared.d2i", 1000, BackupDb::State::Autosave,
              asBytes("older-version-bytes"));

    RecoverySpec spec;
    spec.destDir  = sc.savesDir;
    spec.filename = "Shared.d2i";
    spec.atUnix   = 2000;
    spec.preRecoveryBackup     = true;
    spec.allowTombstoneRestore = false;

    const auto rep = d2r::recoverFile(db, sched, spec);
    REQUIRE(rep.restored);
    REQUIRE(rep.preSnapshotTaken);
    REQUIRE(readAll(sc.savesDir / "Shared.d2i") == "older-version-bytes");

    // The DB now has two Shared.d2i rows: the older (t=1000) plus the
    // pre-recovery snapshot (t = now, State::Autosave, current-live-bytes).
    const auto hist = db.historyFor("Shared.d2i", 10);
    REQUIRE(hist.size() == 2);
    // Newest row is the pre-recovery snapshot.
    REQUIRE(hist[0].date >= 1000);
    REQUIRE(hist[0].state == BackupDb::State::Autosave);
}

TEST_CASE("Recovery: tombstone to primary removes the file",
          "[backup][recovery]") {
    Scratch sc;
    BackupDb db(sc.dbPath);
    BackupScheduler sched(db, sc.savesDir);

    // A file currently exists on disk.
    {
        std::ofstream f(sc.savesDir / "Kai.d2s", std::ios::binary);
        f << "current";
    }
    // The DB says: at t=1000 the file was deleted.
    db.insertTombstone("Kai.d2s", 1000);

    RecoverySpec spec;
    spec.destDir              = sc.savesDir;
    spec.filename             = "Kai.d2s";
    spec.atUnix               = 2000;
    spec.preRecoveryBackup    = true;
    spec.allowTombstoneRestore = true;

    const auto rep = d2r::recoverFile(db, sched, spec);
    REQUIRE(rep.restored);
    REQUIRE(rep.wasTombstone);
    REQUIRE_FALSE(fs::exists(sc.savesDir / "Kai.d2s"));
    // Pre-recovery snapshot captured the "current" bytes -> retrievable.
    auto row = db.at("Kai.d2s", 9'999'999'999);
    REQUIRE(row.has_value());
    // Newest row after recovery is the tombstone re-applied by the
    // manual snapshot? No -- the pre-recovery snapshot happened first
    // (when the file existed) so the newest row before the tombstone
    // read is that captured 'current' body. But wait: `at()` orders by
    // date DESC and the pre-recovery snapshot's date == now, which is
    // >= the tombstone's date=1000, so we get the pre-recovery snapshot.
    REQUIRE(row->state == BackupDb::State::Autosave);
}

TEST_CASE("Recovery: tombstone to alt dir with allowTombstoneRestore=false",
          "[backup][recovery]") {
    Scratch sc;
    BackupDb db(sc.dbPath);
    BackupScheduler sched(db, sc.savesDir);

    db.insertTombstone("Kai.d2s", 1000);

    RecoverySpec spec;
    spec.destDir              = sc.altDir;
    spec.filename             = "Kai.d2s";
    spec.atUnix               = 2000;
    spec.preRecoveryBackup     = false;
    spec.allowTombstoneRestore = false;

    const auto rep = d2r::recoverFile(db, sched, spec);
    REQUIRE(rep.wasTombstone);
    REQUIRE_FALSE(rep.restored);
    // Alt dir may exist but the file was not created.
    REQUIRE_FALSE(fs::exists(sc.altDir / "Kai.d2s"));
}

TEST_CASE("Recovery: no backup predating the ask returns nothing",
          "[backup][recovery]") {
    Scratch sc;
    BackupDb db(sc.dbPath);
    BackupScheduler sched(db, sc.savesDir);
    db.insert("Kai.d2s", 2000, BackupDb::State::Autosave, asBytes("late"));

    RecoverySpec spec;
    spec.destDir  = sc.altDir;
    spec.filename = "Kai.d2s";
    spec.atUnix   = 1000;   // earlier than any backup
    spec.preRecoveryBackup = false;

    const auto rep = d2r::recoverFile(db, sched, spec);
    REQUIRE_FALSE(rep.restored);
    REQUIRE_FALSE(rep.wasTombstone);
    REQUIRE_FALSE(fs::exists(sc.altDir / "Kai.d2s"));
}
