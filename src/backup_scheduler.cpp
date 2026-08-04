// BackupScheduler: pipe DirectoryWatcher events into BackupDb writes.

#include "d2r/BackupScheduler.hpp"

#include "d2r/Save.hpp"

#include <sys/inotify.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <system_error>
#include <utility>

namespace d2r {

// ---------------------------------------------------------------------------
// Helpers (also exposed via backup_scheduler_detail for tests).
// ---------------------------------------------------------------------------

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

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char x = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
        const char y = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (x != y) return false;
    }
    return true;
}

std::int64_t nowUnix() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

// CRC-32 (IEEE 802.3 polynomial 0xEDB88320), reflected, initial 0xFFFFFFFF,
// final XOR 0xFFFFFFFF. Table built once at first call. Not for
// cryptographic use -- purely a change-detection fingerprint for .d2i
// files that don't carry a native checksum.
std::uint32_t crc32Ieee(std::span<const std::byte> data) noexcept {
    static const std::array<std::uint32_t, 256> kTable = []{
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (const auto b : data) {
        c = kTable[(c ^ static_cast<std::uint32_t>(b)) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

} // namespace

namespace backup_scheduler_detail {

bool isPersistedFile(std::string_view name) {
    return endsWithNoCase(name, ".d2s") || endsWithNoCase(name, ".d2i");
}

bool isSaveAndExitSignal(std::string_view name) {
    return endsWithNoCase(name, ".ctl") || equalsNoCase(name, "Settings.json");
}

BackupDb::State classifyBurst(
    std::span<const DirectoryWatcher::ChangedFile> files) {
    // Classifier tiers, in priority order:
    //   1. Any .ctl or Settings.json with CLOSE_WRITE  -> SaveAndExit
    //      (the game writes these companions only when the user quits).
    //   2. Any .d2s / .d2i with CLOSE_WRITE            -> Autosave
    //      (in-game write path; save file finalised in-place).
    //   3. Anything else that touched a .d2s / .d2i    -> Other
    //      (an external process replaced the file: rsync, cp, syncthing,
    //      manual editing, ...). We still want a backup so the DB
    //      captures the imported state, but we can't honestly claim it
    //      was an in-game save-and-exit or autosave.
    bool sawSaveExitSignal = false;
    bool sawPersistedClose = false;
    bool sawPersistedTouch = false;
    for (const auto& f : files) {
        const bool persisted = isPersistedFile(f.name);
        const bool signal    = isSaveAndExitSignal(f.name);
        const bool cw        = (f.mask & IN_CLOSE_WRITE) != 0;
        const bool moved     = (f.mask & IN_MOVED_TO)    != 0;
        if (cw && signal)                       sawSaveExitSignal = true;
        if (cw && persisted)                    sawPersistedClose = true;
        if (persisted && (cw || moved))         sawPersistedTouch = true;
    }
    if (sawSaveExitSignal) return BackupDb::State::SaveAndExit;
    if (sawPersistedClose) return BackupDb::State::Autosave;
    if (sawPersistedTouch) return BackupDb::State::Other;
    // Nothing persisted was actually touched (e.g. a .ma0 wrote alone).
    // Return Autosave as a conservative default; the caller only writes
    // rows for persisted files, so this state won't actually reach the DB.
    return BackupDb::State::Autosave;
}

std::int64_t extractD2sTimestamp(std::span<const std::byte> bytes) {
    // Layout: 32-bit LE unix seconds at offset 0x20. See
    // src/character_parser.cpp kTimestampOffset.
    constexpr std::size_t kTimestampOffset = 0x20;
    if (bytes.size() < kTimestampOffset + sizeof(std::uint32_t)) return 0;
    std::uint32_t v = 0;
    std::memcpy(&v, bytes.data() + kTimestampOffset, sizeof(v));
    return static_cast<std::int64_t>(v);
}

std::uint32_t computeFileChecksum(std::string_view name,
                                  std::span<const std::byte> bytes) noexcept {
    // .d2s: use the game's own checksum. It excludes the 4 bytes at
    // offset 0x0C from the sum, so two saves that differ only in the
    // stored-checksum field would collide -- in practice the two are
    // computed together and the on-disk checksum matches whenever the
    // rest of the file matches, so this is a reliable change signal.
    if (endsWithNoCase(name, ".d2s")) {
        return computeChecksum(bytes);
    }
    // Everything else: CRC-32.
    return crc32Ieee(bytes);
}

bool isLaunchBurst(
    std::span<const DirectoryWatcher::ChangedFile> files) {
    // The launch signature is "reads-only burst with at least one read
    // on a non-.d2s/.d2i file". See BackupScheduler.hpp + the analysis
    // in memories for the derivation.
    constexpr std::uint32_t kWriteBits =
        IN_CLOSE_WRITE | IN_MODIFY | IN_MOVED_TO | IN_CREATE;
    constexpr std::uint32_t kReadBits =
        IN_ACCESS | IN_OPEN | IN_CLOSE_NOWRITE;
    bool sawNonPersistedRead = false;
    for (const auto& f : files) {
        if (f.name.empty()) continue;   // ISDIR-only rows carry an empty name
        if ((f.mask & kWriteBits) != 0) return false;
        if ((f.mask & kReadBits)  == 0) continue;
        if (!isPersistedFile(f.name)) sawNonPersistedRead = true;
    }
    return sawNonPersistedRead;
}

} // namespace backup_scheduler_detail

// ---------------------------------------------------------------------------
// BackupScheduler
// ---------------------------------------------------------------------------

BackupScheduler::BackupScheduler(BackupDb&                    db,
                                 std::filesystem::path        savesDir,
                                 RetentionConfig              retention)
    : db_(db),
      savesDir_(std::move(savesDir)),
      retention_(retention) {}

// Read a file's bytes, compute its per-type checksum, and insert only
// when the bytes have actually changed since the last row for this
// filename. `nowUnix` is the fallback timestamp; .d2s files use their
// in-file header timestamp instead. Errors (missing file, permission
// denied) are logged to stderr and swallowed -- the dashboard should
// not crash because one save temporarily disappeared.
void BackupScheduler::writeFileAsState(const std::filesystem::path& path,
                                       BackupDb::State              state,
                                       std::int64_t                 nowUnix) {
    std::vector<std::byte> bytes;
    try {
        bytes = readFile(path);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[backup] cannot read %s: %s\n",
                     path.string().c_str(), ex.what());
        return;
    }
    const auto name = path.filename().string();
    const auto checksum = backup_scheduler_detail::computeFileChecksum(name, bytes);

    // Dedup: if the last row for this file has the same checksum, the
    // bytes haven't changed since the last backup. Skip the insert.
    // A tombstone-or-legacy last row is reported as nullopt, so we
    // always insert in that case.
    if (auto last = db_.lastChecksumFor(name); last && *last == checksum) {
        return;
    }

    std::int64_t date = nowUnix;
    if (endsWithNoCase(name, ".d2s")) {
        const auto ts = backup_scheduler_detail::extractD2sTimestamp(bytes);
        if (ts > 0) date = ts;
    }
    try {
        db_.insert(name, date, state, checksum, bytes);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[backup] insert failed for %s: %s\n",
                     name.c_str(), ex.what());
        return;
    }
    if (onInsert_) {
        try { onInsert_(name, date, state); }
        catch (const std::exception&) { /* callback bugs must not kill the backup path */ }
    }
}

void BackupScheduler::runRetention(std::int64_t nowUnix) {
    try {
        (void) db_.enforceRetention(retention_.days,
                                    retention_.sessionsPerFile,
                                    nowUnix);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[backup] retention failed: %s\n", ex.what());
    }
}

// ---- Public entry points ---------------------------------------------------

void BackupScheduler::takeStartupSnapshot() {
    const auto now = nowUnix();
    std::error_code ec;
    if (!std::filesystem::is_directory(savesDir_, ec)) {
        std::fprintf(stderr, "[backup] startup: %s is not a directory\n",
                     savesDir_.string().c_str());
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(savesDir_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const auto name = entry.path().filename().string();
        if (!backup_scheduler_detail::isPersistedFile(name)) continue;
        writeFileAsState(entry.path(), BackupDb::State::Startup, now);
    }
    runRetention(now);
}

void BackupScheduler::handleWatcherEvents(
    std::span<const DirectoryWatcher::ChangedFile> files) {
    if (files.empty()) return;
    const auto now = nowUnix();

    // Launch-burst callback fires independently of the write-classifier
    // tier. It's an observational signal; we don't record anything to
    // the DB. A burst can only be EITHER a launch burst (reads only,
    // non-persisted read present) OR contain writes -- the isLaunchBurst
    // predicate excludes bursts with any write bit set, so writes still
    // reach classifyBurst below via the normal path.
    if (onLaunch_ && backup_scheduler_detail::isLaunchBurst(files)) {
        try { onLaunch_(now); }
        catch (const std::exception&) {}
        return;   // reads-only burst: nothing to persist.
    }

    const auto burstState = backup_scheduler_detail::classifyBurst(files);

    for (const auto& f : files) {
        if (!backup_scheduler_detail::isPersistedFile(f.name)) continue;
        // Deletion wins: if IN_DELETE or IN_MOVED_FROM fired for this
        // file at any point in the burst, record a tombstone. (D2R
        // itself doesn't delete saves, but users occasionally do.)
        if ((f.mask & (IN_DELETE | IN_MOVED_FROM)) != 0) {
            try {
                db_.insertTombstone(f.name, now);
                if (onInsert_) {
                    try { onInsert_(f.name, now, BackupDb::State::Deleted); }
                    catch (const std::exception&) {}
                }
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "[backup] tombstone insert failed for %s: %s\n",
                             f.name.c_str(), ex.what());
            }
            continue;
        }
        // Otherwise only act on CLOSE_WRITE or MOVED_TO. CLOSE_WRITE is
        // the game's own save path (write-then-close in place). MOVED_TO
        // is the atomic-rename path used by rsync/cp/etc: a temp file
        // is written elsewhere and renamed on top of the target, so we
        // never see CLOSE_WRITE on the target basename -- only the
        // rename landing. Torn / mid-write IN_MODIFY events are still
        // filtered out.
        if ((f.mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) == 0) continue;
        writeFileAsState(savesDir_ / f.name, burstState, now);
    }
    runRetention(now);
}

void BackupScheduler::takeManualSnapshot(
    std::span<const std::filesystem::path> files) {
    const auto now = nowUnix();
    for (const auto& path : files) {
        std::error_code ec;
        const auto name = path.filename().string();
        if (std::filesystem::exists(path, ec) &&
            std::filesystem::is_regular_file(path, ec)) {
            writeFileAsState(path, BackupDb::State::Autosave, now);
        } else {
            // File is gone at the moment of the manual snapshot; record
            // a tombstone so recovery can reproduce this state.
            try {
                db_.insertTombstone(name, now);
                if (onInsert_) {
                    try { onInsert_(name, now, BackupDb::State::Deleted); }
                    catch (const std::exception&) {}
                }
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "[backup] tombstone insert failed for %s: %s\n",
                             name.c_str(), ex.what());
            }
        }
    }
    runRetention(now);
}

} // namespace d2r
