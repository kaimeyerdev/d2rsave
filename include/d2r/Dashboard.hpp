// Entry point for the FTXUI-based interactive dashboard. Only compiled
// when the ftxui vcpkg port is available (D2R_HAVE_DASHBOARD).

#pragma once

#include <filesystem>
#include <string>

namespace d2r {

// Runs the interactive dashboard until the user quits (`q` / Ctrl-C).
// Returns 0 on clean exit, non-zero on setup failure. Requires the
// reference DB to be locatable (`findReferenceDb`).
[[nodiscard]] int runDashboard(const std::filesystem::path& savePath,
                               const std::string& referenceDbOverride,
                               const std::filesystem::path& exePath);

} // namespace d2r
