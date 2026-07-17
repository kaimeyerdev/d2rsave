// Point-in-time recovery for backed-up save files.
//
// Recovery is a query on (filename, date) + a filesystem write. Always
// takes a pre-recovery snapshot of whatever is currently at the
// destination so the operation is reversible via a repeat recovery.

#pragma once

#include "d2r/BackupDb.hpp"
#include "d2r/BackupScheduler.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace d2r {

struct RecoverySpec {
    // Directory to write the restored file into. The final path is
    // `destDir / filename`. For "restore in place" this is the saves
    // dir; for "restore to a folder" this is an alternate path.
    std::filesystem::path destDir;

    // Basename of the file to restore (e.g. "Kai.d2s").
    std::string           filename;

    // Recover the row that was current at this unix second.
    std::int64_t          atUnix = 0;

    // Snapshot whatever currently lives at `destDir/filename` before
    // overwriting. Recommended (default): true.
    bool                  preRecoveryBackup = true;

    // If the row is a tombstone (State::Deleted), what should happen?
    //   true  -> remove the destination file (matches "restore this
    //            moment fully, including its deletion state").
    //   false -> skip: report `wasTombstone=true, restored=false`.
    // Recommended: true for in-place restores, false for alt-dir.
    bool                  allowTombstoneRestore = true;
};

struct RecoveryReport {
    // Something happened: bytes were written or a tombstone was applied.
    bool         restored       = false;

    // The row that was found was a tombstone (state=Deleted).
    bool         wasTombstone   = false;

    // For non-tombstone restores.
    std::int64_t bytesWritten   = 0;

    // Actual date of the backup row that was restored (may be earlier
    // than atUnix if no exact match was found).
    std::int64_t recoveredDate  = 0;

    // Set to true if the pre-recovery snapshot was taken (a file
    // existed at destDir/filename before the write).
    bool         preSnapshotTaken = false;
};

// Recover one file. Throws std::runtime_error on filesystem or DB errors
// that leave the destination in an inconsistent state.
RecoveryReport recoverFile(BackupDb&           db,
                           BackupScheduler&    scheduler,
                           const RecoverySpec& spec);

} // namespace d2r
