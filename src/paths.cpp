#include "d2r/Paths.hpp"

#include <cstdlib>
#include <stdexcept>
#include <system_error>

namespace d2r {

std::filesystem::path userDataDir() {
    namespace fs = std::filesystem;
    fs::path base;
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        base = fs::path(xdg) / "d2rsave";
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        base = fs::path(home) / ".local" / "share" / "d2rsave";
    } else {
        throw std::runtime_error(
            "cannot locate d2rsave data dir: neither $XDG_DATA_HOME nor "
            "$HOME is set");
    }
    std::error_code ec;
    fs::create_directories(base, ec);
    if (ec) {
        throw std::runtime_error(
            "cannot create d2rsave data dir '" + base.string()
            + "': " + ec.message());
    }
    return base;
}

std::filesystem::path dashboardConfigDbPath() {
    return userDataDir() / "dashboard.sqlite";
}

std::filesystem::path backupDbPath() {
    return userDataDir() / "backups.sqlite";
}

} // namespace d2r
