// Linux inotify implementation of DirectoryWatcher.
//
// One inotify fd, up to two watches: the primary save directory and,
// optionally, the parent directory of the running executable (used to
// detect a fresh link). Events are parsed inline so we can classify each
// wake-up as Primary vs. Executable. `read()` blocks until an event
// arrives; when SIGINT interrupts it, `errno == EINTR` and we return
// nullopt so the CLI loop can exit cleanly.

#include "d2r/Watcher.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace d2r {

namespace {

// Events we care about on the primary save directory.
//
// IN_MODIFY is included specifically for the shared-stash case: D2R (via
// Wine) can keep the `.d2i` file open for the whole session and write to
// it in-place, so IN_CLOSE_WRITE never fires. IN_MODIFY does. Bursty
// writes are coalesced by the 250 ms debounce in waitForChange().
constexpr std::uint32_t kSaveDirMask =
    IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM |
    IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF;

// For the exe's parent we only care about creates/renames-in — that's how
// ld / cmake atomically replaces a running binary.
constexpr std::uint32_t kExeDirMask =
    IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE;

} // namespace

DirectoryWatcher::DirectoryWatcher(const std::filesystem::path& primaryDir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(primaryDir, ec)) {
        throw WatcherError("--watch: not a directory: " + primaryDir.string());
    }
    fd_ = ::inotify_init1(IN_CLOEXEC);
    if (fd_ < 0) {
        throw WatcherError(std::string("inotify_init1: ") + std::strerror(errno));
    }
    // Second fd for cross-thread wake-ups. `poll` sees it as readable once
    // `shutdown()` writes to it.
    wakeFd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wakeFd_ < 0) {
        const int e = errno;
        ::close(fd_);
        fd_ = -1;
        throw WatcherError(std::string("eventfd: ") + std::strerror(e));
    }
    primaryWd_ = ::inotify_add_watch(fd_, primaryDir.c_str(), kSaveDirMask);
    if (primaryWd_ < 0) {
        const int e = errno;
        ::close(wakeFd_);
        ::close(fd_);
        fd_ = -1;
        wakeFd_ = -1;
        throw WatcherError("inotify_add_watch " + primaryDir.string() + ": " +
                           std::strerror(e));
    }
}

DirectoryWatcher::~DirectoryWatcher() {
    if (primaryWd_ >= 0 && fd_ >= 0) ::inotify_rm_watch(fd_, primaryWd_);
    if (exeWd_     >= 0 && fd_ >= 0) ::inotify_rm_watch(fd_, exeWd_);
    if (fd_     >= 0) ::close(fd_);
    if (wakeFd_ >= 0) ::close(wakeFd_);
}

void DirectoryWatcher::shutdown() noexcept {
    if (wakeFd_ < 0) return;
    // Fire the eventfd. `poll` in the watcher thread will wake immediately;
    // waitForChange returns nullopt. EAGAIN is fine (already signalled).
    const std::uint64_t one = 1;
    ssize_t r = ::write(wakeFd_, &one, sizeof(one));
    (void) r;
}

bool DirectoryWatcher::alsoWatchExecutable(const std::filesystem::path& exePath) {
    if (fd_ < 0 || exePath.empty()) return false;
    const auto parent = exePath.parent_path();
    std::error_code ec;
    if (parent.empty() || !std::filesystem::is_directory(parent, ec)) return false;
    exeWd_ = ::inotify_add_watch(fd_, parent.c_str(), kExeDirMask);
    if (exeWd_ < 0) return false;
    exeBasename_ = exePath.filename().string();
    return true;
}

std::optional<DirectoryWatcher::Trigger>
DirectoryWatcher::waitForChange(std::chrono::milliseconds debounce) {
    alignas(alignof(inotify_event)) char buf[8192];
    std::vector<char> events;

    // 1. Poll on both the inotify fd and the shutdown eventfd. Whichever
    // becomes readable first wakes us. If the eventfd fires we exit early
    // with nullopt so the caller can join us.
    struct pollfd pfds[2] = {
        {fd_,     POLLIN, 0},
        {wakeFd_, POLLIN, 0},
    };
    for (;;) {
        const int pr = ::poll(pfds, 2, -1);
        if (pr < 0 && errno == EINTR) continue;
        if (pr < 0) return std::nullopt;
        if (pfds[1].revents & POLLIN) return std::nullopt; // shutdown
        if (pfds[0].revents & POLLIN) break;
    }

    // 2. Read the first burst of inotify events.
    const ssize_t first = ::read(fd_, buf, sizeof(buf));
    if (first <= 0) return std::nullopt;
    events.insert(events.end(), buf, buf + first);

    // 3. Debounce: pause briefly, then drain any additional events that
    // arrived during the window. Switch the fd non-blocking so read() will
    // return EAGAIN once the queue is empty; restore the original flags
    // afterwards so signals still interrupt future blocking reads cleanly.
    std::this_thread::sleep_for(debounce);
    const int origFlags = ::fcntl(fd_, F_GETFL, 0);
    (void) ::fcntl(fd_, F_SETFL, origFlags | O_NONBLOCK);
    for (;;) {
        const ssize_t n = ::read(fd_, buf, sizeof(buf));
        if (n > 0) {
            events.insert(events.end(), buf, buf + n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    (void) ::fcntl(fd_, F_SETFL, origFlags);

    // 4. Also check the shutdown fd once more before returning a Trigger;
    // avoids racing with a shutdown that came in during the debounce.
    struct pollfd wake = {wakeFd_, POLLIN, 0};
    if (::poll(&wake, 1, 0) > 0 && (wake.revents & POLLIN)) return std::nullopt;

    // 5. Classify. Executable wins when both fire in the same window.
    bool sawPrimary = false;
    bool sawExe     = false;
    for (std::size_t i = 0; i + sizeof(inotify_event) <= events.size();) {
        const auto* ev = reinterpret_cast<const inotify_event*>(events.data() + i);
        const std::string_view name{ev->name, ev->len ? ::strnlen(ev->name, ev->len) : 0};
        if (ev->wd == exeWd_ && name == exeBasename_) {
            sawExe = true;
        } else if (ev->wd == primaryWd_) {
            sawPrimary = true;
        }
        i += sizeof(inotify_event) + ev->len;
    }
    if (sawExe)     return Trigger::Executable;
    if (sawPrimary) return Trigger::Primary;
    // No relevant classification -> spurious wake-up. Treat as primary so the
    // caller re-runs; that's safer than pretending nothing happened.
    return Trigger::Primary;
}

} // namespace d2r

