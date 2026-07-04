#include "d2r/CascStorage.hpp"

#include <CascLib.h>

#include <cerrno>
#include <cstring>
#include <sstream>
#include <utility>

namespace d2r {

namespace {

std::string cascError(const std::string& what) {
    std::ostringstream ss;
    ss << what << " (CascLib errno=" << GetCascError() << ")";
    return ss.str();
}

} // namespace

CascStorage::CascStorage(const std::filesystem::path& gameRoot) {
    // CascLib accepts the game root, the Data/ subdirectory, or a fully
    // qualified build-config path. Passing the root is the simplest and works
    // for a standard D2R install.
    const std::string param = gameRoot.string();
    HANDLE h = nullptr;
    if (!CascOpenStorage(param.c_str(), 0, &h)) {
        throw CascError(cascError("CascOpenStorage failed for '" + param + "'"));
    }
    handle_ = h;
}

CascStorage::~CascStorage() {
    if (handle_) {
        CascCloseStorage(handle_);
        handle_ = nullptr;
    }
}

CascStorage::CascStorage(CascStorage&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

CascStorage& CascStorage::operator=(CascStorage&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            CascCloseStorage(handle_);
        }
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

std::vector<std::byte> CascStorage::readFile(std::string_view cascPath) const {
    if (!handle_) {
        throw CascError("CascStorage: no storage open");
    }

    const std::string path(cascPath);
    HANDLE hFile = nullptr;
    if (!CascOpenFile(handle_, path.c_str(), 0, CASC_OPEN_BY_NAME, &hFile)) {
        throw CascError(cascError("CascOpenFile failed for '" + path + "'"));
    }

    struct FileGuard {
        HANDLE h;
        ~FileGuard() { if (h) CascCloseFile(h); }
    } guard{hFile};

    DWORD sizeHigh = 0;
    const DWORD sizeLow = CascGetFileSize(hFile, &sizeHigh);
    if (sizeLow == CASC_INVALID_SIZE) {
        throw CascError(cascError("CascGetFileSize failed for '" + path + "'"));
    }
    if (sizeHigh != 0) {
        throw CascError("CascStorage: file '" + path + "' larger than 4 GiB (unsupported)");
    }

    std::vector<std::byte> buffer(sizeLow);
    if (sizeLow > 0) {
        DWORD readCount = 0;
        if (!CascReadFile(hFile, buffer.data(), sizeLow, &readCount) || readCount != sizeLow) {
            throw CascError(cascError("CascReadFile short read for '" + path + "'"));
        }
    }

    return buffer;
}

std::string CascStorage::readFileText(std::string_view cascPath) const {
    const auto bytes = readFile(cascPath);
    std::string s;
    s.resize(bytes.size());
    if (!bytes.empty()) {
        std::memcpy(s.data(), bytes.data(), bytes.size());
    }
    return s;
}

} // namespace d2r
