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
//   data     BLOB                the raw file bytes; NULL iff state=0
//
// Recovery is a point-in-time query on (filename, date). Retention is
// session-aware: keep rows within X days OR belonging to the last Y
// sessions per character (with shared-stash rows inheriting the union of
// preserved character session date ranges).

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
                std::span<const std::byte> data);

    // Insert a tombstone row (state=Deleted, data=NULL).
    void insertTombstone(std::string_view filename, std::int64_t dateUnix);

    // Fetch the latest row where date <= askUnix, or nullopt if none.
    struct Row {
        State                   state = State::Autosave;
        std::vector<std::byte>  data;   // empty when state=Deleted
    };
    [[nodiscard]] std::optional<Row>
    at(std::string_view filename, std::int64_t askUnix) const;

    // Delete rows that fail both the date rule (date < now - days*86400)
    // and the session rule (belong to the last `sessionsPerFile` sessions
    // of a character, plus shared-stash rows that fall within any
    // preserved character session's date range). Runs in one transaction.
    // Returns the number of rows deleted.
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
        std::int64_t sessionCount = 0;   // rows with state=SaveAndExit
    };
    [[nodiscard]] std::vector<FileSummary> summariseFiles() const;

    // Per-file history, most-recent first, up to `limit` rows.
    struct HistoryRow {
        std::int64_t date      = 0;
        State        state     = State::Autosave;
        std::int64_t sizeBytes = 0;   // 0 for tombstones
    };
    [[nodiscard]] std::vector<HistoryRow>
    historyFor(std::string_view filename, std::size_t limit) const;

    [[nodiscard]] sqlite3* raw() const noexcept { return db_; }

private:
    sqlite3* db_ = nullptr;
};

} // namespace d2r
