// Entry point for the FTXUI-based interactive dashboard. Only compiled
// when the ftxui vcpkg port is available (D2R_HAVE_DASHBOARD).

#pragma once

#include <filesystem>
#include <string>

namespace d2r {

// Non-interactive-mode knobs. Defaults preserve the interactive TUI.
struct DashboardOptions {
    // Render the current layout once to stdout as an ANSI-escaped
    // string and exit. No watcher, no backup DB, no writes back to
    // the config DB. Useful for debugging / screenshots without a
    // live terminal.
    bool printOnce = false;
    // Dimensions used when `printOnce == true`. Interactive mode
    // ignores these and reads the tty size.
    int printWidth  = 200;
    int printHeight = 60;
};

// Runs the interactive dashboard until the user quits (`q` / Ctrl-C).
// Returns 0 on clean exit, non-zero on setup failure. Requires the
// reference DB to be locatable (`findReferenceDb`).
[[nodiscard]] int runDashboard(const std::filesystem::path& savePath,
                               const std::string& referenceDbOverride,
                               const std::filesystem::path& exePath,
                               const DashboardOptions& options = {});

} // namespace d2r
