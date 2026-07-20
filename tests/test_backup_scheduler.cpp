// Catch2 tests for BackupScheduler.

#include "d2r/BackupDb.hpp"
#include "d2r/BackupScheduler.hpp"
#include "d2r/Watcher.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sys/inotify.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using d2r::BackupDb;
using d2r::BackupScheduler;
using d2r::DirectoryWatcher;
namespace bs = d2r::backup_scheduler_detail;

namespace {

struct Scratch {
    fs::path savesDir;
    fs::path dbPath;
    Scratch() {
        static std::atomic<int> counter{0};
        const auto pid = static_cast<long>(::getpid());
        const auto n   = counter.fetch_add(1, std::memory_order_relaxed);
        const auto ts  = std::chrono::system_clock::now().time_since_epoch().count();
        char name[128];
        std::snprintf(name, sizeof(name), "d2rsave-scheduler-%ld-%ld-%d",
                      pid, static_cast<long>(ts), n);
        savesDir = fs::temp_directory_path() / name;
        fs::create_directories(savesDir);
        dbPath = savesDir / ".." / (std::string(name) + ".sqlite");
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(savesDir, ec);
        fs::remove(dbPath, ec);
    }
};

// Write bytes to `savesDir/name`; returns absolute path.
fs::path writeFile(const fs::path& savesDir, std::string_view name,
                   std::string_view bytes) {
    const auto p = savesDir / std::string(name);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p;
}

// Build a minimal .d2s buffer with a specific header timestamp at
// offset 0x20. Real .d2s files start with magic bytes and other fields;
// we only need enough bytes for readFile + extractD2sTimestamp to work.
std::string makeD2sBuffer(std::uint32_t timestamp,
                          std::string_view tailPayload = "extra-payload") {
    std::string s(0x20, '\0');
    s.append(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
    s.append(tailPayload);
    return s;
}

DirectoryWatcher::ChangedFile mkFile(std::string name, std::uint32_t mask) {
    DirectoryWatcher::ChangedFile f;
    f.name = std::move(name);
    f.mask = mask;
    return f;
}

} // namespace

TEST_CASE("classifyBurst: .ctl or Settings.json signal SaveAndExit",
          "[backup][scheduler]") {
    const std::vector<DirectoryWatcher::ChangedFile> plainAutosave = {
        mkFile("Kai.d2s", IN_MODIFY | IN_CLOSE_WRITE),
        mkFile("Shared.d2i", IN_MODIFY | IN_CLOSE_WRITE),
    };
    REQUIRE(bs::classifyBurst(plainAutosave) == BackupDb::State::Autosave);

    const std::vector<DirectoryWatcher::ChangedFile> saveExit = {
        mkFile("Kai.d2s", IN_CLOSE_WRITE),
        mkFile("Kai.ctl", IN_CLOSE_WRITE),
    };
    REQUIRE(bs::classifyBurst(saveExit) == BackupDb::State::SaveAndExit);

    const std::vector<DirectoryWatcher::ChangedFile> saveExitViaSettings = {
        mkFile("Kai.d2s", IN_CLOSE_WRITE),
        mkFile("Settings.json", IN_CLOSE_WRITE),
    };
    REQUIRE(bs::classifyBurst(saveExitViaSettings) == BackupDb::State::SaveAndExit);
}

TEST_CASE("classifyBurst: signal without CLOSE_WRITE is not enough",
          "[backup][scheduler]") {
    // MODIFY-only on Settings.json shouldn't be trusted.
    const std::vector<DirectoryWatcher::ChangedFile> modifyOnly = {
        mkFile("Kai.d2s", IN_MODIFY | IN_CLOSE_WRITE),
        mkFile("Settings.json", IN_MODIFY),
    };
    REQUIRE(bs::classifyBurst(modifyOnly) == BackupDb::State::Autosave);
}

TEST_CASE("classifyBurst: atomic-rename bursts classify as Other",
          "[backup][scheduler]") {
    // rsync / cp / syncthing use write-temp + rename, so we see
    // IN_MOVED_TO on the target basename but never IN_CLOSE_WRITE on
    // a persisted file. Batches often include many files at once.
    const std::vector<DirectoryWatcher::ChangedFile> rsyncPull = {
        mkFile("Kai.d2s",                          IN_MOVED_TO),
        mkFile("ModernSharedStashSoftCoreV2.d2i",  IN_MOVED_TO),
        mkFile("Kai.ctl",                          IN_MOVED_TO),
    };
    REQUIRE(bs::classifyBurst(rsyncPull) == BackupDb::State::Other);
}

TEST_CASE("classifyBurst: MOVED_TO plus in-game CLOSE_WRITE still Autosave",
          "[backup][scheduler]") {
    // Mixed burst -- shouldn't happen in practice, but if a MOVED_TO
    // races with a real autosave in the same window we prefer the
    // stronger classification.
    const std::vector<DirectoryWatcher::ChangedFile> mixed = {
        mkFile("Kai.d2s",   IN_MOVED_TO),
        mkFile("Warlock.d2s", IN_CLOSE_WRITE | IN_MODIFY),
    };
    REQUIRE(bs::classifyBurst(mixed) == BackupDb::State::Autosave);
}

TEST_CASE("isPersistedFile: only .d2s and .d2i", "[backup][scheduler]") {
    REQUIRE(bs::isPersistedFile("Kai.d2s"));
    REQUIRE(bs::isPersistedFile("Shared.d2i"));
    REQUIRE_FALSE(bs::isPersistedFile("Kai.ctl"));
    REQUIRE_FALSE(bs::isPersistedFile("Settings.json"));
    REQUIRE_FALSE(bs::isPersistedFile("Kai.ma0"));
    REQUIRE_FALSE(bs::isPersistedFile("random.txt"));
    // Case-insensitive suffix match.
    REQUIRE(bs::isPersistedFile("Kai.D2S"));
}

TEST_CASE("extractD2sTimestamp reads offset 0x20 little-endian",
          "[backup][scheduler]") {
    const auto s = makeD2sBuffer(0x12345678u);
    REQUIRE(bs::extractD2sTimestamp(
        std::span(reinterpret_cast<const std::byte*>(s.data()), s.size())
    ) == 0x12345678);

    // Buffer shorter than the header offset -> 0.
    const std::string tiny(0x10, '\0');
    REQUIRE(bs::extractD2sTimestamp(
        std::span(reinterpret_cast<const std::byte*>(tiny.data()), tiny.size())
    ) == 0);
}

TEST_CASE("Scheduler.takeStartupSnapshot writes one State::Startup row per file",
          "[backup][scheduler]") {
    Scratch sc;
    writeFile(sc.savesDir, "Kai.d2s",       makeD2sBuffer(999));
    writeFile(sc.savesDir, "Warlock.d2s",   makeD2sBuffer(888));
    writeFile(sc.savesDir, "Shared.d2i",    "stash-bytes");
    writeFile(sc.savesDir, "Kai.ma0",       "map-bytes"); // ignored
    writeFile(sc.savesDir, "Kai.ctl",       "ctl-bytes"); // ignored

    BackupDb db(sc.dbPath);
    BackupScheduler sched(db, sc.savesDir);
    sched.takeStartupSnapshot();

    const auto sums = db.summariseFiles();
    REQUIRE(sums.size() == 3);
    for (const auto& fs : sums) {
        REQUIRE(fs.lastState == BackupDb::State::Startup);
        REQUIRE(fs.backupCount == 1);
    }
    // .d2s uses its in-file timestamp, not wall clock.
    auto row = db.at("Kai.d2s", 999);
    REQUIRE(row.has_value());
    REQUIRE(row->state == BackupDb::State::Startup);
    // .d2i uses wall clock (which is > 0).
    auto stashSum = std::find_if(sums.begin(), sums.end(),
        [](const auto& s) { return s.filename == "Shared.d2i"; });
    REQUIRE(stashSum != sums.end());
    REQUIRE(stashSum->lastDate > 1'700'000'000);  // sanity: current-era unix time
}

TEST_CASE("Scheduler.handleWatcherEvents on save-and-exit burst",
          "[backup][scheduler]") {
    Scratch sc;
    writeFile(sc.savesDir, "Kai.d2s",     makeD2sBuffer(555));
    writeFile(sc.savesDir, "Shared.d2i",  "stash-bytes");
    writeFile(sc.savesDir, "Kai.ctl",     "ctl-bytes");
    writeFile(sc.savesDir, "Settings.json", "settings");

    BackupDb db(sc.dbPath);
    BackupScheduler sched(db, sc.savesDir);

    const std::vector<DirectoryWatcher::ChangedFile> burst = {
        mkFile("Kai.d2s",       IN_MODIFY | IN_CLOSE_WRITE),
        mkFile("Shared.d2i",    IN_MODIFY | IN_CLOSE_WRITE),
        mkFile("Kai.ctl",       IN_MODIFY | IN_CLOSE_WRITE),
        mkFile("Settings.json", IN_MODIFY | IN_CLOSE_WRITE),
    };
    sched.handleWatcherEvents(burst);

    // Only .d2s and .d2i should be persisted, both as SaveAndExit.
    const auto sums = db.summariseFiles();
    REQUIRE(sums.size() == 2);
    for (const auto& fs : sums) {
        REQUIRE(fs.lastState == BackupDb::State::SaveAndExit);
        REQUIRE(fs.sessionCount == 1);
    }
    // .d2s date comes from the header.
    auto row = db.at("Kai.d2s", 555);
    REQUIRE(row.has_value());
}

TEST_CASE("Scheduler.handleWatcherEvents ignores IN_MODIFY without CLOSE_WRITE",
          "[backup][scheduler]") {
    Scratch sc;
    writeFile(sc.savesDir, "Kai.d2s", makeD2sBuffer(555));

    BackupDb db(sc.dbPath);
    BackupScheduler sched(db, sc.savesDir);

    const std::vector<DirectoryWatcher::ChangedFile> burst = {
        mkFile("Kai.d2s", IN_MODIFY),   // no CLOSE_WRITE
    };
    sched.handleWatcherEvents(burst);

    REQUIRE(db.summariseFiles().empty());
}

TEST_CASE("Scheduler.handleWatcherEvents records tombstone on IN_DELETE",
          "[backup][scheduler]") {
    Scratch sc;
    // File doesn't have to exist for a delete event to be recorded.
    BackupDb db(sc.dbPath);
    BackupScheduler sched(db, sc.savesDir);

    const std::vector<DirectoryWatcher::ChangedFile> burst = {
        mkFile("Kai.d2s", IN_DELETE),
    };
    sched.handleWatcherEvents(burst);

    const auto sums = db.summariseFiles();
    REQUIRE(sums.size() == 1);
    REQUIRE(sums[0].lastState == BackupDb::State::Deleted);
    auto row = db.at("Kai.d2s", 9'999'999'999);
    REQUIRE(row.has_value());
    REQUIRE(row->state == BackupDb::State::Deleted);
    REQUIRE(row->data.empty());
}

TEST_CASE("computeFileChecksum picks the right algorithm per extension",
          "[backup][scheduler][checksum]") {
    const std::string d2sBytes = makeD2sBuffer(1234, "payload-alpha");
    const std::string d2iBytes = "arbitrary-stash-bytes";

    const auto d2sSum = bs::computeFileChecksum(
        "Kai.d2s",
        std::span{reinterpret_cast<const std::byte*>(d2sBytes.data()), d2sBytes.size()});
    const auto d2iSum = bs::computeFileChecksum(
        "Shared.d2i",
        std::span{reinterpret_cast<const std::byte*>(d2iBytes.data()), d2iBytes.size()});

    // Same bytes must produce the same result.
    REQUIRE(bs::computeFileChecksum(
        "Kai.d2s",
        std::span{reinterpret_cast<const std::byte*>(d2sBytes.data()), d2sBytes.size()}
    ) == d2sSum);
    REQUIRE(bs::computeFileChecksum(
        "Shared.d2i",
        std::span{reinterpret_cast<const std::byte*>(d2iBytes.data()), d2iBytes.size()}
    ) == d2iSum);

    // Different bytes yield different results (spot check).
    const std::string d2iOther = "different-stash-bytes";
    REQUIRE(bs::computeFileChecksum(
        "Shared.d2i",
        std::span{reinterpret_cast<const std::byte*>(d2iOther.data()), d2iOther.size()}
    ) != d2iSum);
}

TEST_CASE("Scheduler dedups: second startup snapshot skips unchanged files",
          "[backup][scheduler][dedup]") {
    Scratch sc;
    writeFile(sc.savesDir, "Kai.d2s",    makeD2sBuffer(555));
    writeFile(sc.savesDir, "Shared.d2i", "stash-bytes-v1");

    BackupDb db(sc.dbPath);
    BackupScheduler sched(db, sc.savesDir);

    // First sweep: both files land as new rows.
    sched.takeStartupSnapshot();
    REQUIRE(db.historyFor("Kai.d2s",    10).size() == 1);
    REQUIRE(db.historyFor("Shared.d2i", 10).size() == 1);

    // Second sweep: bytes are unchanged, so no new rows.
    sched.takeStartupSnapshot();
    REQUIRE(db.historyFor("Kai.d2s",    10).size() == 1);
    REQUIRE(db.historyFor("Shared.d2i", 10).size() == 1);

    // Mutate the stash bytes -- next sweep inserts one new row for it
    // and leaves the character alone.
    writeFile(sc.savesDir, "Shared.d2i", "stash-bytes-v2");
    sched.takeStartupSnapshot();
    REQUIRE(db.historyFor("Kai.d2s",    10).size() == 1);
    REQUIRE(db.historyFor("Shared.d2i", 10).size() == 2);
}

TEST_CASE("Scheduler dedups: identical watcher-event bytes don't insert",
          "[backup][scheduler][dedup]") {
    Scratch sc;
    writeFile(sc.savesDir, "Kai.d2s", makeD2sBuffer(555, "same-payload"));

    BackupDb db(sc.dbPath);
    BackupScheduler sched(db, sc.savesDir);

    const std::vector<DirectoryWatcher::ChangedFile> burst = {
        mkFile("Kai.d2s", IN_MODIFY | IN_CLOSE_WRITE),
    };
    sched.handleWatcherEvents(burst);
    REQUIRE(db.historyFor("Kai.d2s", 10).size() == 1);

    // Same file, same bytes -- no new row.
    sched.handleWatcherEvents(burst);
    REQUIRE(db.historyFor("Kai.d2s", 10).size() == 1);

    // Change the file (even in a byte that keeps the header timestamp);
    // now a second row lands.
    writeFile(sc.savesDir, "Kai.d2s", makeD2sBuffer(555, "different-payload"));
    sched.handleWatcherEvents(burst);
    REQUIRE(db.historyFor("Kai.d2s", 10).size() == 2);
}

TEST_CASE("BackupDb.lastChecksumFor returns NULL for tombstones and legacy",
          "[backup][db][checksum]") {
    Scratch sc;
    BackupDb db(sc.dbPath);

    // Empty file history.
    REQUIRE_FALSE(db.lastChecksumFor("nothing.d2s").has_value());

    // Regular insert -> checksum is retrievable.
    const std::string bytes = "abc";
    db.insert("Kai.d2s", 1000, BackupDb::State::Autosave, 0xABCDu,
              std::span{reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()});
    auto ck = db.lastChecksumFor("Kai.d2s");
    REQUIRE(ck.has_value());
    REQUIRE(*ck == 0xABCDu);

    // Tombstone -> newest row has no checksum, so lastChecksumFor is NULL.
    db.insertTombstone("Kai.d2s", 2000);
    REQUIRE_FALSE(db.lastChecksumFor("Kai.d2s").has_value());
}
