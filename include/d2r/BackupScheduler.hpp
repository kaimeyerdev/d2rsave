// Bridges DirectoryWatcher events to BackupDb inserts.
//
// One instance is owned by the dashboard for the duration of the run.
// takeStartupSnapshot() is called once at bootstrap; handleWatcherEvents()
// is called once per debounce burst. Retention is enforced after every
// burst according to the RetentionConfig passed at construction.

#pragma once

#include "d2r/BackupDb.hpp"
#include "d2r/Watcher.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2r {

struct RetentionConfig {
    int days            = 30;   // rule (a): keep rows within this many days
    int sessionsPerFile = 100;  // rule (b): keep this many sessions per .d2s
};

class BackupScheduler {
public:
    BackupScheduler(BackupDb&                    db,
                    std::filesystem::path        savesDir,
                    RetentionConfig              retention = {});

    // Scan `savesDir` once and insert one State::Startup row per relevant
    // file (currently *.d2s and *.d2i). Errors reading individual files
    // are logged and skipped -- the sweep must not fail catastrophically
    // if one file is momentarily unreadable.
    void takeStartupSnapshot();

    // Called from the dashboard's watcher-thread body after a debounce
    // burst returns. Classifies the burst (SaveAndExit vs Autosave),
    // reads bytes for each CLOSE_WRITE'd .d2s / .d2i, and inserts one
    // row per file. IN_DELETE for a .d2s / .d2i inserts a tombstone.
    // IN_MODIFY without CLOSE_WRITE is ignored (torn-file protection).
    void handleWatcherEvents(std::span<const DirectoryWatcher::ChangedFile>);

    // Snapshot a specific set of files right now, tagged as
    // State::Autosave (this is what Recovery calls before overwriting
    // destination bytes). Missing files are recorded as tombstones.
    void takeManualSnapshot(std::span<const std::filesystem::path> files);

    // Access for tests + the CLI/pane retention editor.
    [[nodiscard]] const RetentionConfig& retention() const noexcept { return retention_; }
    void setRetention(RetentionConfig cfg) noexcept { retention_ = cfg; }

private:
    void writeFileAsState(const std::filesystem::path& path,
                          BackupDb::State              state,
                          std::int64_t                 nowUnix);
    void runRetention(std::int64_t nowUnix);

    BackupDb&              db_;
    std::filesystem::path  savesDir_;
    RetentionConfig        retention_;
};

// Helpers exposed for testing.
namespace backup_scheduler_detail {

// Returns true iff `name` is a file we persist bytes for (.d2s or .d2i).
[[nodiscard]] bool isPersistedFile(std::string_view name);

// Returns true iff `name` is a classifier signal for SaveAndExit
// (any *.ctl or Settings.json).
[[nodiscard]] bool isSaveAndExitSignal(std::string_view name);

// Given the changed-file list of one burst, compute the state that
// should be applied to every persisted file. Deletes are handled at a
// higher level and don't feed this function.
[[nodiscard]] BackupDb::State classifyBurst(
    std::span<const DirectoryWatcher::ChangedFile>);

// Extract Character.timestamp (unix seconds) from the .d2s header. Returns
// 0 if the buffer isn't big enough. Kept here so tests can exercise it
// against fixtures without pulling the character parser.
[[nodiscard]] std::int64_t extractD2sTimestamp(std::span<const std::byte>);

} // namespace backup_scheduler_detail

} // namespace d2r
