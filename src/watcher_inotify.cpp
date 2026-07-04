// Linux inotify implementation of DirectoryWatcher.
//
// We ask inotify to notify us on file-write completion (`IN_CLOSE_WRITE`),
// atomic-rename-in (`IN_MOVED_TO`, the pattern D2R uses to update its save
// files) and creates/deletes. `read()` blocks until an event arrives; when
// SIGINT interrupts it, `errno == EINTR` and we return false so the CLI
// loop can exit cleanly.

#include "d2r/Watcher.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>

namespace d2r {

namespace {
constexpr std::uint32_t kMask =
    IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM |
    IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF;
} // namespace

DirectoryWatcher::DirectoryWatcher(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        throw WatcherError("--watch: not a directory: " + dir.string());
    }
    fd_ = ::inotify_init1(IN_CLOEXEC);
    if (fd_ < 0) {
        throw WatcherError(std::string("inotify_init1: ") + std::strerror(errno));
    }
    wd_ = ::inotify_add_watch(fd_, dir.c_str(), kMask);
    if (wd_ < 0) {
        const int e = errno;
        ::close(fd_);
        fd_ = -1;
        throw WatcherError("inotify_add_watch " + dir.string() + ": " + std::strerror(e));
    }
}

DirectoryWatcher::~DirectoryWatcher() {
    if (wd_ >= 0 && fd_ >= 0) ::inotify_rm_watch(fd_, wd_);
    if (fd_ >= 0) ::close(fd_);
}

bool DirectoryWatcher::waitForChange(std::chrono::milliseconds debounce) {
    // 1. Block until the first event.
    char buf[4096] __attribute__((aligned(alignof(struct inotify_event))));
    const ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n <= 0) return false; // EINTR (signal) or error -> caller exits.

    // 2. Debounce: drain any additional events that arrive within `debounce`
    // to coalesce bursts (D2R may rewrite the .d2s, .ma0, .map, .ctl, .key
    // set together).
    std::this_thread::sleep_for(debounce);
    // Drain non-blocking.
    while (true) {
        pollfd pfd{fd_, POLLIN, 0};
        const int pr = ::poll(&pfd, 1, 0);
        if (pr <= 0) break;
        char discard[4096];
        (void) ::read(fd_, discard, sizeof(discard));
    }
    return true;
}

} // namespace d2r
