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
#include <functional>
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

    // Fires once per successful backup insert (State::Startup,
    // Autosave, SaveAndExit) and once per tombstone. Dedup-skips and
    // read failures do NOT fire. Runs synchronously on whatever thread
    // handles the burst; the callback must be cheap and thread-safe --
    // in the dashboard the watcher thread calls this, while the UI
    // thread reads the buffer. Passing an empty function detaches.
    using InsertCallback = std::function<void(std::string_view filename,
                                              std::int64_t     dateUnix,
                                              BackupDb::State  state)>;
    void setInsertCallback(InsertCallback cb) { onInsert_ = std::move(cb); }

    // Fires when a burst matches `isLaunchBurst` (D2R.exe launch
    // pattern). The dashboard uses this to update the AppSession
    // singleton's `autoStartEpoch` without recording anything to the
    // BackupDb -- launches are observational, not persisted. Runs on
    // the watcher thread; passing an empty function detaches.
    using LaunchCallback = std::function<void(std::int64_t whenUnix)>;
    void setLaunchCallback(LaunchCallback cb) { onLaunch_ = std::move(cb); }

private:
    void writeFileAsState(const std::filesystem::path& path,
                          BackupDb::State              state,
                          std::int64_t                 nowUnix);
    void runRetention(std::int64_t nowUnix);

    BackupDb&              db_;
    std::filesystem::path  savesDir_;
    RetentionConfig        retention_;
    InsertCallback         onInsert_;
    LaunchCallback         onLaunch_;
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

// True iff `files` looks like a D2R.exe launch burst.
//
// Signature (see docs/session-logic.md + the analysis in
// /memories/session/auto-modes-plan.md):
//   * Zero writes across the burst (no IN_CLOSE_WRITE / IN_MODIFY /
//     IN_MOVED_TO / IN_CREATE on any file).
//   * AT LEAST THREE distinct non-`.d2s`/`.d2i` file reads in the same
//     burst. Concretely: 3+ files ending in `.ctl`, `.key`, `.ma*`,
//     `.map`, or named `Settings.json`. A real launch reads dozens of
//     these files at once; single-file mid-gameplay reads (e.g. D2R
//     touching Settings.json when the options menu opens) can't reach
//     the threshold, so mid-session false positives that would
//     retroactively shift AppSession.autoStartEpoch don't fire.
//
// The threshold is safe for accounts with >= 1 character (a real
// launch reads Settings.json + at least 5 auxiliary files per
// character). 0-character launches don't reach the threshold, but
// they also have no runes / items to track.
//
// See docs/session-logic.md for the full derivation and the trace
// analysis in memory for the empirical basis.
[[nodiscard]] bool isLaunchBurst(
    std::span<const DirectoryWatcher::ChangedFile>);

// Extract Character.timestamp (unix seconds) from the .d2s header. Returns
// 0 if the buffer isn't big enough. Kept here so tests can exercise it
// against fixtures without pulling the character parser.
[[nodiscard]] std::int64_t extractD2sTimestamp(std::span<const std::byte>);

// Per-file-type change-detection fingerprint. .d2s uses the game's own
// rotate-add checksum from Save.hpp (bytes 0x0C..0x0F excluded from the
// sum, so it's stable across saves that don't touch content). .d2i and
// everything else use CRC-32 (IEEE polynomial 0xEDB88320) as a small
// content hash -- not cryptographic, but plenty for "did anything
// change?" queries. Two calls on the same bytes always return the same
// value.
[[nodiscard]] std::uint32_t computeFileChecksum(
    std::string_view name, std::span<const std::byte> bytes) noexcept;

} // namespace backup_scheduler_detail

} // namespace d2r
