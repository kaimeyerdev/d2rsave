#pragma once

// Thin RAII wrapper over CascLib for read-only access to a D2R install.
// Callers construct with the game's root directory (the one containing
// `Data/`); the wrapper opens the storage on construction and closes it on
// destruction. All file access is read-only; this wrapper never writes to the
// D2R install.

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace d2r {

class CascError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CascStorage {
public:
    explicit CascStorage(const std::filesystem::path& gameRoot);
    ~CascStorage();

    CascStorage(const CascStorage&) = delete;
    CascStorage& operator=(const CascStorage&) = delete;
    CascStorage(CascStorage&&) noexcept;
    CascStorage& operator=(CascStorage&&) noexcept;

    // Read a file out of the CASC storage into memory. `cascPath` is the
    // in-storage filename using D2R's namespaced backslash form, e.g.
    // "data:data\\global\\excel\\armor.txt" or
    // "data:data\\local\\lng\\strings\\item-names.json".
    std::vector<std::byte> readFile(std::string_view cascPath) const;

    // Convenience: read a file and return it as a UTF-8 string. Fails loudly
    // if the file is missing.
    std::string readFileText(std::string_view cascPath) const;

private:
    void* handle_ = nullptr; // HANDLE (void*) from CascLib
};

} // namespace d2r
