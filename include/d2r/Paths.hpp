// Filesystem-path helpers shared by the config DB and the backup DB.
//
// Both DBs live in the same per-user directory (XDG_DATA_HOME with a
// $HOME/.local/share fallback); this header centralises the lookup so
// they stay in sync.

#pragma once

#include <filesystem>

namespace d2r {

// Resolve the per-user data directory for d2rsave and create it if it
// doesn't exist. Uses `$XDG_DATA_HOME/d2rsave` when set, otherwise
// `$HOME/.local/share/d2rsave`. Throws std::runtime_error if neither
// environment variable is set or the directory cannot be created.
[[nodiscard]] std::filesystem::path userDataDir();

// Convenience wrappers matching the two files the dashboard writes.
[[nodiscard]] std::filesystem::path dashboardConfigDbPath();
[[nodiscard]] std::filesystem::path backupDbPath();

} // namespace d2r
