// Recovery: point-in-time restore of one file's bytes.

#include "d2r/Recovery.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace d2r {

RecoveryReport recoverFile(BackupDb&           db,
                           BackupScheduler&    scheduler,
                           const RecoverySpec& spec) {
    RecoveryReport report;

    // 1. Find the row to restore.
    const auto row = db.at(spec.filename, spec.atUnix);
    if (!row) {
        // Nothing predates the ask -- caller can decide what to do.
        return report;
    }
    // Query the actual row date (a small extra roundtrip, only for the
    // report; keeps the BackupDb::Row shape narrow).
    {
        auto history = db.historyFor(spec.filename, 1024);
        for (const auto& h : history) {
            if (h.date <= spec.atUnix) {
                report.recoveredDate = h.date;
                break;
            }
        }
    }

    // 2. Ensure the destination directory exists.
    std::error_code ec;
    if (!std::filesystem::is_directory(spec.destDir, ec)) {
        if (!std::filesystem::create_directories(spec.destDir, ec) && ec) {
            throw std::runtime_error(
                "recoverFile: cannot create destDir '" +
                spec.destDir.string() + "': " + ec.message());
        }
    }
    const auto destPath = spec.destDir / spec.filename;

    // 3. Pre-recovery snapshot of whatever is currently at destPath.
    if (spec.preRecoveryBackup &&
        std::filesystem::exists(destPath, ec)) {
        const std::array<std::filesystem::path, 1> one = {destPath};
        scheduler.takeManualSnapshot(one);
        report.preSnapshotTaken = true;
    }

    // 4. Apply the row.
    if (row->state == BackupDb::State::Deleted) {
        report.wasTombstone = true;
        if (!spec.allowTombstoneRestore) {
            // Report what we found without touching the destination.
            return report;
        }
        // Remove the destination if present; report as restored.
        std::filesystem::remove(destPath, ec);
        // ENOENT is fine: the destination was already absent, so
        // "restore this moment" is a no-op.
        report.restored = true;
        return report;
    }

    // 5. Write bytes atomically: write to a temporary sibling then rename.
    const auto tmpPath = destPath.string() + ".d2rsave-tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error(
                "recoverFile: cannot open temp file '" + tmpPath +
                "' for writing");
        }
        if (!row->data.empty()) {
            out.write(reinterpret_cast<const char*>(row->data.data()),
                      static_cast<std::streamsize>(row->data.size()));
        }
        out.close();
        if (!out) {
            std::error_code rmec;
            std::filesystem::remove(tmpPath, rmec);
            throw std::runtime_error(
                "recoverFile: failed writing temp file '" + tmpPath + "'");
        }
    }
    std::filesystem::rename(tmpPath, destPath, ec);
    if (ec) {
        std::error_code rmec;
        std::filesystem::remove(tmpPath, rmec);
        throw std::runtime_error(
            "recoverFile: cannot rename '" + tmpPath + "' -> '" +
            destPath.string() + "': " + ec.message());
    }
    report.restored     = true;
    report.bytesWritten = static_cast<std::int64_t>(row->data.size());
    return report;
}

} // namespace d2r
