// Directory-change watcher. Wraps Linux inotify(7) behind a tiny API so the
// CLI can implement `--watch` without leaking platform details.
//
// The watcher can attach to two kinds of targets:
//   * The primary save directory (its files trigger a command re-run).
//   * The running executable itself (a re-link triggers a re-exec so the
//     freshly-built binary takes over the watch loop).
//
// This header is only usable when compiled with D2R_HAVE_INOTIFY. See
// CMakeLists.txt for the check_include_file gate.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2r {

class WatcherError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class DirectoryWatcher {
public:
    // One file's contribution to a debounce window. `mask` is the OR of
    // every inotify event mask observed for `name` during the window
    // (typically some combination of IN_MODIFY / IN_CLOSE_WRITE / IN_DELETE
    // etc.). Consumers use the mask to decide whether the file is worth
    // reading (CLOSE_WRITE) or was removed (IN_DELETE).
    struct ChangedFile {
        std::string   name;
        std::uint32_t mask = 0;
    };

    // What woke the caller. `Executable` events take priority over primary
    // events in the same debounce window (the caller wants to re-exec
    // rather than re-run stale code). `files` is only populated for
    // `Primary` triggers.
    struct Trigger {
        enum class Kind { Primary, Executable };
        Kind                     kind = Kind::Primary;
        std::vector<ChangedFile> files;
    };

    // Watch `primaryDir` non-recursively. Throws WatcherError on failure.
    explicit DirectoryWatcher(const std::filesystem::path& primaryDir);
    ~DirectoryWatcher();

    DirectoryWatcher(const DirectoryWatcher&)            = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

    // Also react when `exePath` is re-linked. We watch the exe's parent
    // directory and filter events by basename so the file survives the
    // classic "unlink + create" pattern that cmake/ld use. Silently returns
    // false when the parent isn't a directory we can watch.
    bool alsoWatchExecutable(const std::filesystem::path& exePath);

    // Block until at least one relevant event arrives, then coalesce any
    // additional events within `debounce`. Returns std::nullopt when
    // interrupted by a signal (e.g. SIGINT) or when `shutdown()` has been
    // called from another thread. When both an executable event and a
    // primary-dir event fire in the same window, Executable wins.
    [[nodiscard]] std::optional<Trigger> waitForChange(
        std::chrono::milliseconds debounce = std::chrono::milliseconds{250});

    // Wake any thread currently blocked in `waitForChange` so it returns
    // std::nullopt. Idempotent; safe to call from any thread. Once
    // triggered, subsequent `waitForChange` calls also return immediately
    // with nullopt (there's no way to un-shutdown a watcher).
    void shutdown() noexcept;

private:
    int fd_          = -1;
    int primaryWd_   = -1;
    int exeWd_       = -1;
    int wakeFd_      = -1;   // eventfd for cross-thread shutdown wakes
    std::string exeBasename_;
};

} // namespace d2r

