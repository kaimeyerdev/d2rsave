// Backup database for the D2R saves directory.
//
// One SQLite file (`$XDG_DATA_HOME/d2rsave/backups.sqlite`), one table.
// Each row is a single moment in a file's history:
//
//   filename TEXT NOT NULL       basename inside the saves dir
//   date     INTEGER NOT NULL    unix seconds; from Character.timestamp
//                                (offset 0x20) for .d2s files, else wall
//                                clock sampled once per burst
//   state    INTEGER NOT NULL    0=deleted, 1=save_and_exit,
//                                2=autosave, 3=startup
//   checksum INTEGER             per-file-type fingerprint of `data` --
//                                the D2R rotate-add checksum (offset
//                                0x0C) for .d2s, CRC-32 for .d2i, NULL
//                                for tombstones and legacy rows written
//                                before this column existed
//   data     BLOB                the raw file bytes; NULL iff state=0
//
// Recovery is a point-in-time query on (filename, date). Retention is
// session-aware: keep rows within X days OR belonging to the last Y
// sessions per character (with shared-stash rows inheriting the union of
// preserved character session date ranges). Change-detection uses
// `checksum` to skip inserting bytes that are identical to the most
// recent row for the same filename.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace d2r {

class BackupDbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class BackupDb {
public:
    enum class State : int {
        Deleted     = 0,
        SaveAndExit = 1,
        Autosave    = 2,
        Startup     = 3,
        // File content changed but the surrounding IO pattern didn't
        // look like a game-driven save (no CLOSE_WRITE on any .d2s /
        // .d2i / .ctl). Typical trigger: an external tool (rsync, cp,
        // a syncthing client) replaces the file via atomic rename, so
        // we see IN_MOVED_TO instead of the game's write-then-close
        // sequence. Preserving the change as `Other` keeps every
        // distinct on-disk version in the backup DB without lying
        // about what caused it.
        Other       = 4,
    };

    // Open/create the DB at `dbPath`. Applies WAL + synchronous=NORMAL
    // and creates the schema if missing.
    explicit BackupDb(const std::filesystem::path& dbPath);
    ~BackupDb();

    BackupDb(const BackupDb&)            = delete;
    BackupDb& operator=(const BackupDb&) = delete;
    BackupDb(BackupDb&& other) noexcept;
    BackupDb& operator=(BackupDb&& other) noexcept;

    // Insert a full backup row.
    void insert(std::string_view filename,
                std::int64_t     dateUnix,
                State            state,
                std::uint32_t    checksum,
                std::span<const std::byte> data);

    // Insert a tombstone row (state=Deleted, data=NULL, checksum=NULL).
    void insertTombstone(std::string_view filename, std::int64_t dateUnix);

    // Fetch the latest row where date <= askUnix, or nullopt if none.
    struct Row {
        State                        state = State::Autosave;
        std::optional<std::uint32_t> checksum;   // NULL for tombstones / legacy
        std::vector<std::byte>       data;       // empty when state=Deleted
    };
    [[nodiscard]] std::optional<Row>
    at(std::string_view filename, std::int64_t askUnix) const;

    // Fetch the checksum of the most-recent row for `filename`, or
    // nullopt if the file has no rows OR the newest row is a tombstone
    // OR the newest row was written before the `checksum` column
    // existed. Used by BackupScheduler for change detection: an equal
    // checksum means the bytes haven't changed since the last insert.
    [[nodiscard]] std::optional<std::uint32_t>
    lastChecksumFor(std::string_view filename) const;

    // Delete rows that fail both the date rule (date < now - days*86400)
    // and the session rule (belong to the last `sessionsPerFile` sessions
    // of a character, plus shared-stash rows that fall within any
    // preserved character session's date range). Runs in one transaction.
    // Returns the number of rows deleted.
    //
    // A session is the run of saves that ENDS at a state=SaveAndExit
    // backup: all autosaves + startup rows preceding that S&E belong to
    // the same session. Retention keeps whole sessions (including their
    // terminating S&E), so a count of 2 keeps the two newest COMPLETED
    // sessions plus everything newer than the oldest kept S&E -- i.e.,
    // the current in-progress session if the user is mid-play.
    std::int64_t enforceRetention(int          days,
                                  int          sessionsPerFile,
                                  std::int64_t nowUnix);

    // One row per known filename, most recent snapshot summary. Ordered
    // by lastDate descending. Suitable for the Backups pane summary view.
    struct FileSummary {
        std::string  filename;
        std::int64_t lastDate     = 0;
        State        lastState    = State::Autosave;
        std::int64_t backupCount  = 0;
        // Count of state=SaveAndExit rows. Since a session ends with an
        // S&E, this is exactly the number of COMPLETED sessions we have
        // on record for the file (an in-progress session -- autosaves
        // with no S&E yet -- contributes 0).
        std::int64_t sessionCount = 0;
    };
    [[nodiscard]] std::vector<FileSummary> summariseFiles() const;

    // Per-file history, most-recent first, up to `limit` rows.
    struct HistoryRow {
        std::int64_t                 date      = 0;
        State                        state     = State::Autosave;
        std::int64_t                 sizeBytes = 0;   // 0 for tombstones
        std::optional<std::uint32_t> checksum;         // NULL for tombstones / legacy
    };
    [[nodiscard]] std::vector<HistoryRow>
    historyFor(std::string_view filename, std::size_t limit) const;

    [[nodiscard]] sqlite3* raw() const noexcept { return db_; }

private:
    // Force any WAL-only rows into the main database file. Called after
    // every write so an external `cp backups.sqlite` snapshots a fully
    // up-to-date view (WAL mode otherwise defers this until an
    // auto-checkpoint or clean shutdown). Best-effort: SQLITE_BUSY is
    // silently ignored -- the next write retries.
    void checkpointWal() noexcept;

    sqlite3* db_ = nullptr;
};

} // namespace d2r
