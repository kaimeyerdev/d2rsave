// BackupDb: single-table SQLite backing store for the D2R saves directory.

#include "d2r/BackupDb.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace d2r {

namespace {

// One row of PRAGMAs + the schema, applied on first open. Idempotent
// via IF NOT EXISTS so subsequent opens are cheap.
//
// The `checksum` column was added after v1 shipped. Existing DBs that
// don't have it are migrated below in BackupDb::BackupDb().
constexpr const char* kSchema =
    "PRAGMA journal_mode = WAL;"
    "PRAGMA synchronous  = NORMAL;"
    "CREATE TABLE IF NOT EXISTS backup ("
    "  filename TEXT    NOT NULL,"
    "  date     INTEGER NOT NULL,"
    "  state    INTEGER NOT NULL,"
    "  checksum INTEGER,"
    "  data     BLOB"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_backup_filename_date"
    "  ON backup (filename, date DESC);";

[[noreturn]] void raiseSqlite(sqlite3* db, const std::string& what) {
    std::string msg = what;
    if (db) {
        msg += ": ";
        msg += sqlite3_errmsg(db);
    }
    throw BackupDbError(std::move(msg));
}

// Prepared-statement RAII helper. Not shared with RefDb::Statement -- that
// one is read-only and doesn't bind BLOBs.
class Stmt {
public:
    Stmt(sqlite3* db, std::string_view sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql.data(),
                               static_cast<int>(sql.size()),
                               &stmt_, nullptr) != SQLITE_OK) {
            raiseSqlite(db, "sqlite3_prepare_v2");
        }
    }
    ~Stmt() { if (stmt_) sqlite3_finalize(stmt_); }

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    sqlite3_stmt* raw() const noexcept { return stmt_; }

    void bindText(int i, std::string_view s) {
        if (sqlite3_bind_text(stmt_, i, s.data(),
                              static_cast<int>(s.size()),
                              SQLITE_TRANSIENT) != SQLITE_OK) {
            raiseSqlite(db_, "sqlite3_bind_text");
        }
    }
    void bindInt64(int i, std::int64_t v) {
        if (sqlite3_bind_int64(stmt_, i, v) != SQLITE_OK) {
            raiseSqlite(db_, "sqlite3_bind_int64");
        }
    }
    void bindBlob(int i, const void* data, int nBytes) {
        if (sqlite3_bind_blob(stmt_, i, data, nBytes,
                              SQLITE_TRANSIENT) != SQLITE_OK) {
            raiseSqlite(db_, "sqlite3_bind_blob");
        }
    }
    void bindNull(int i) {
        if (sqlite3_bind_null(stmt_, i) != SQLITE_OK) {
            raiseSqlite(db_, "sqlite3_bind_null");
        }
    }

    // Returns true iff a row is available.
    bool step() {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW)  return true;
        if (rc == SQLITE_DONE) return false;
        raiseSqlite(db_, "sqlite3_step");
    }

    void run() {
        (void) step();  // for statements with no result rows
    }

    std::int64_t     columnInt64(int c) const { return sqlite3_column_int64(stmt_, c); }
    std::string_view columnText(int c) const {
        const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, c));
        const auto  n = sqlite3_column_bytes(stmt_, c);
        return {p ? p : "", static_cast<std::size_t>(n)};
    }
    bool             columnIsNull(int c) const { return sqlite3_column_type(stmt_, c) == SQLITE_NULL; }
    std::vector<std::byte> columnBlob(int c) const {
        const auto* p = sqlite3_column_blob(stmt_, c);
        const auto  n = sqlite3_column_bytes(stmt_, c);
        std::vector<std::byte> out(static_cast<std::size_t>(n));
        if (n > 0 && p) std::memcpy(out.data(), p, static_cast<std::size_t>(n));
        return out;
    }

private:
    sqlite3*      db_   = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

void execOrThrow(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = "sqlite3_exec(";
        msg += sql;
        msg += "): ";
        msg += err ? err : "unknown";
        if (err) sqlite3_free(err);
        throw BackupDbError(std::move(msg));
    }
}

} // namespace

BackupDb::BackupDb(const std::filesystem::path& dbPath) {
    const auto path = dbPath.string();
    if (sqlite3_open_v2(path.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
        const std::string msg = db_ ? sqlite3_errmsg(db_) : "sqlite3_open_v2 failed";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw BackupDbError("cannot open backup db (" + path + "): " + msg);
    }
    try {
        execOrThrow(db_, kSchema);
        // Migrate: `checksum` column was added after the initial release.
        // ADD COLUMN is idempotent-safe when we check first via
        // pragma_table_info.
        bool hasChecksum = false;
        {
            Stmt s(db_,
                "SELECT COUNT(*) FROM pragma_table_info('backup') "
                " WHERE name = 'checksum'");
            (void) s.step();
            hasChecksum = s.columnInt64(0) > 0;
        }
        if (!hasChecksum) {
            execOrThrow(db_, "ALTER TABLE backup ADD COLUMN checksum INTEGER");
        }
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}

BackupDb::~BackupDb() {
    if (db_) sqlite3_close(db_);
}

BackupDb::BackupDb(BackupDb&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)) {}

BackupDb& BackupDb::operator=(BackupDb&& other) noexcept {
    if (this != &other) {
        if (db_) sqlite3_close(db_);
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

void BackupDb::insert(std::string_view filename,
                      std::int64_t     dateUnix,
                      State            state,
                      std::uint32_t    checksum,
                      std::span<const std::byte> data) {
    Stmt s(db_,
        "INSERT INTO backup (filename, date, state, checksum, data) "
        "VALUES (?, ?, ?, ?, ?)");
    s.bindText(1, filename);
    s.bindInt64(2, dateUnix);
    s.bindInt64(3, static_cast<std::int64_t>(state));
    s.bindInt64(4, static_cast<std::int64_t>(checksum));
    // sqlite3_bind_blob wants int; guard against overflow (unlikely: our
    // files are tens of KB).
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw BackupDbError("backup blob too large for sqlite3 bind");
    }
    s.bindBlob(5, data.data(), static_cast<int>(data.size()));
    s.run();
    checkpointWal();
}

void BackupDb::insertTombstone(std::string_view filename, std::int64_t dateUnix) {
    Stmt s(db_,
        "INSERT INTO backup (filename, date, state, checksum, data) "
        "VALUES (?, ?, ?, NULL, NULL)");
    s.bindText(1, filename);
    s.bindInt64(2, dateUnix);
    s.bindInt64(3, static_cast<std::int64_t>(State::Deleted));
    s.run();
    checkpointWal();
}

void BackupDb::checkpointWal() noexcept {
    // Journal mode is WAL, so committed inserts sit in the -wal sidecar
    // until an auto-checkpoint (~1000 pages / hundreds of saves) or a
    // clean shutdown migrates them into the main file. That's fine for
    // durability -- readers on the same connection see WAL data -- but
    // it means an external `cp backups.sqlite` snapshots a stale view.
    // Truncate after every write so the main file always reflects the
    // committed state; at our write rate (~one every few seconds) the
    // extra fsync is trivial. TRUNCATE is best-effort: if a reader is
    // holding the WAL open the call returns SQLITE_BUSY and we simply
    // skip -- the next write will retry.
    if (db_) {
        (void)sqlite3_wal_checkpoint_v2(
            db_, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
    }
}

std::optional<BackupDb::Row>
BackupDb::at(std::string_view filename, std::int64_t askUnix) const {
    Stmt s(db_,
        "SELECT state, checksum, data FROM backup "
        " WHERE filename = ? AND date <= ? "
        " ORDER BY date DESC LIMIT 1");
    s.bindText(1, filename);
    s.bindInt64(2, askUnix);
    if (!s.step()) return std::nullopt;
    Row row;
    row.state = static_cast<State>(s.columnInt64(0));
    if (!s.columnIsNull(1)) {
        row.checksum = static_cast<std::uint32_t>(s.columnInt64(1));
    }
    if (!s.columnIsNull(2)) {
        row.data = s.columnBlob(2);
    }
    return row;
}

std::optional<std::uint32_t>
BackupDb::lastChecksumFor(std::string_view filename) const {
    Stmt s(db_,
        "SELECT checksum FROM backup "
        " WHERE filename = ? "
        " ORDER BY date DESC LIMIT 1");
    s.bindText(1, filename);
    if (!s.step()) return std::nullopt;   // no rows for this file
    if (s.columnIsNull(0)) return std::nullopt;   // tombstone or legacy row
    return static_cast<std::uint32_t>(s.columnInt64(0));
}

std::vector<BackupDb::FileSummary> BackupDb::summariseFiles() const {
    // Aggregate in one pass: max date, matching state, count, and count
    // of state=1 rows per filename.
    Stmt s(db_,
        "SELECT filename, "
        "       MAX(date)                                  AS last_date, "
        "       COUNT(*)                                   AS backup_count, "
        "       SUM(CASE WHEN state = 1 THEN 1 ELSE 0 END) AS session_count "
        "  FROM backup "
        " GROUP BY filename "
        " ORDER BY last_date DESC");
    std::vector<FileSummary> out;
    while (s.step()) {
        FileSummary fs;
        fs.filename     = std::string(s.columnText(0));
        fs.lastDate     = s.columnInt64(1);
        fs.backupCount  = s.columnInt64(2);
        fs.sessionCount = s.columnInt64(3);
        out.push_back(std::move(fs));
    }
    // Second pass: fill in lastState for each filename.
    Stmt s2(db_,
        "SELECT state FROM backup "
        " WHERE filename = ? "
        " ORDER BY date DESC LIMIT 1");
    for (auto& fs : out) {
        s2.bindText(1, fs.filename);
        if (s2.step()) {
            fs.lastState = static_cast<State>(s2.columnInt64(0));
        }
        sqlite3_reset(s2.raw());
        sqlite3_clear_bindings(s2.raw());
    }
    return out;
}

std::vector<BackupDb::HistoryRow>
BackupDb::historyFor(std::string_view filename, std::size_t limit) const {
    Stmt s(db_,
        "SELECT date, state, "
        "       CASE WHEN data IS NULL THEN 0 ELSE length(data) END AS n, "
        "       checksum "
        "  FROM backup "
        " WHERE filename = ? "
        " ORDER BY date DESC LIMIT ?");
    s.bindText(1, filename);
    s.bindInt64(2, static_cast<std::int64_t>(limit));
    std::vector<HistoryRow> out;
    while (s.step()) {
        HistoryRow r;
        r.date      = s.columnInt64(0);
        r.state     = static_cast<State>(s.columnInt64(1));
        r.sizeBytes = s.columnInt64(2);
        if (!s.columnIsNull(3)) {
            r.checksum = static_cast<std::uint32_t>(s.columnInt64(3));
        }
        out.push_back(r);
    }
    return out;
}

// -----------------------------------------------------------------------
// Retention.
//
// Semantics recap from the plan: a row survives if EITHER
//   (a) date >= now - days*86400, OR
//   (b) it's a .d2s file that belongs to the last `sessionsPerFile`
//       sessions of that file, OR
//   (c) it's a .d2i file whose date falls within any preserved .d2s
//       session's date range.
//
// Implemented in C++ rather than as one giant SQL query so each rule is
// clear and unit-testable. Runs inside a single transaction.
// -----------------------------------------------------------------------

namespace {

bool endsWithNoCase(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    const auto off = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[off + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

bool isCharacterFile(std::string_view name) { return endsWithNoCase(name, ".d2s"); }
bool isSharedStash(std::string_view name)   { return endsWithNoCase(name, ".d2i"); }

} // namespace

std::int64_t BackupDb::enforceRetention(int          days,
                                        int          sessionsPerFile,
                                        std::int64_t nowUnix) {
    if (days < 0)             days = 0;
    if (sessionsPerFile < 0)  sessionsPerFile = 0;

    // Load every row into memory. For the row counts we expect (thousands
    // at worst) this is trivial.
    struct RowInfo {
        std::int64_t rowid    = 0;
        std::string  filename;
        std::int64_t date     = 0;
        State        state    = State::Autosave;
    };
    std::vector<RowInfo> rows;
    {
        Stmt s(db_,
            "SELECT rowid, filename, date, state FROM backup "
            " ORDER BY filename ASC, date DESC");
        while (s.step()) {
            RowInfo r;
            r.rowid    = s.columnInt64(0);
            r.filename = std::string(s.columnText(1));
            r.date     = s.columnInt64(2);
            r.state    = static_cast<State>(s.columnInt64(3));
            rows.push_back(std::move(r));
        }
    }
    if (rows.empty()) return 0;

    const std::int64_t dateCutoff = (days > 0)
        ? nowUnix - static_cast<std::int64_t>(days) * 86400
        : std::numeric_limits<std::int64_t>::max();

    std::unordered_set<std::int64_t> keep;

    // Rule (a): every row within `days` of now.
    for (const auto& r : rows) {
        if (r.date >= dateCutoff) keep.insert(r.rowid);
    }

    // Group rows by filename (they're already sorted by (filename, -date)).
    std::unordered_map<std::string, std::vector<const RowInfo*>> byFile;
    for (const auto& r : rows) byFile[r.filename].push_back(&r);

    // Rule (b): character files -- keep rows belonging to the last
    // `sessionsPerFile` sessions. A session ends with state=SaveAndExit.
    // Cutoff for keeping = date of the (sessionsPerFile+1)-th most-recent
    // state=1 row for that file; keep rows with date > cutoff. If a file
    // has <= sessionsPerFile state=1 rows, keep everything.
    // Also collect the (lo, hi) date ranges of each preserved character
    // "block" for rule (c).
    struct DateRange { std::int64_t lo, hi; };
    std::vector<DateRange> preservedCharRanges;
    for (auto& [fname, list] : byFile) {
        if (!isCharacterFile(fname)) continue;
        if (sessionsPerFile == 0) {
            // Session rule keeps nothing; only rule (a) applies.
            continue;
        }
        // Count save_and_exit rows and find the (sessionsPerFile+1)-th
        // in date-descending order. `list` is already date-DESC.
        std::vector<std::int64_t> exitDates;
        exitDates.reserve(list.size());
        for (const auto* r : list) {
            if (r->state == State::SaveAndExit) exitDates.push_back(r->date);
        }
        std::int64_t cutoff = std::numeric_limits<std::int64_t>::min();
        if (exitDates.size() > static_cast<std::size_t>(sessionsPerFile)) {
            // exitDates[sessionsPerFile] is the (sessionsPerFile+1)-th
            // most-recent; keep rows with date > that.
            cutoff = exitDates[static_cast<std::size_t>(sessionsPerFile)];
        }
        // Collect kept rows + track (lo, hi) for the range.
        std::int64_t lo = std::numeric_limits<std::int64_t>::max();
        std::int64_t hi = std::numeric_limits<std::int64_t>::min();
        bool anyKept = false;
        for (const auto* r : list) {
            if (r->date > cutoff) {
                keep.insert(r->rowid);
                lo = std::min(lo, r->date);
                hi = std::max(hi, r->date);
                anyKept = true;
            }
        }
        if (anyKept) preservedCharRanges.push_back({lo, hi});
    }

    // Rule (c): shared-stash rows that fall within any preserved
    // character session's range.
    if (!preservedCharRanges.empty()) {
        for (auto& [fname, list] : byFile) {
            if (!isSharedStash(fname)) continue;
            for (const auto* r : list) {
                if (keep.count(r->rowid)) continue;
                for (const auto& range : preservedCharRanges) {
                    if (r->date >= range.lo && r->date <= range.hi) {
                        keep.insert(r->rowid);
                        break;
                    }
                }
            }
        }
    }

    // Delete every row NOT in the keep-set. One transaction; batch
    // deletes into a single SQL by binding rowid lists in chunks.
    execOrThrow(db_, "BEGIN;");
    std::int64_t deleted = 0;
    try {
        Stmt del(db_, "DELETE FROM backup WHERE rowid = ?");
        for (const auto& r : rows) {
            if (keep.count(r.rowid)) continue;
            del.bindInt64(1, r.rowid);
            del.run();
            deleted += sqlite3_changes(db_);
            sqlite3_reset(del.raw());
            sqlite3_clear_bindings(del.raw());
        }
        execOrThrow(db_, "COMMIT;");
    } catch (...) {
        execOrThrow(db_, "ROLLBACK;");
        throw;
    }
    checkpointWal();
    return deleted;
}

} // namespace d2r
