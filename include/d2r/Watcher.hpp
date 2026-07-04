// Directory-change watcher. Wraps Linux inotify(7) behind a tiny API so the
// CLI can implement `--watch` without leaking platform details.
//
// This header is only usable when compiled with D2R_HAVE_INOTIFY. See
// CMakeLists.txt for the check_include_file gate.

#pragma once

#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace d2r {

class WatcherError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class DirectoryWatcher {
public:
    // Register an inotify watch on `dir` (non-recursive). Throws WatcherError
    // when inotify setup fails or when `dir` isn't a directory.
    explicit DirectoryWatcher(const std::filesystem::path& dir);
    ~DirectoryWatcher();

    DirectoryWatcher(const DirectoryWatcher&)            = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

    // Block until at least one relevant change event arrives, then drain any
    // additional events that follow within `debounce` (D2R saves several
    // files per snapshot; the debounce coalesces the burst into a single
    // reload). Returns false when interrupted by a signal (e.g. SIGINT) so
    // the caller can exit gracefully.
    [[nodiscard]] bool waitForChange(
        std::chrono::milliseconds debounce = std::chrono::milliseconds{250});

private:
    int fd_ = -1;
    int wd_ = -1;
};

} // namespace d2r
