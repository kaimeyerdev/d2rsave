// Shared-stash file parser. See include/d2r/SharedStash.hpp for the model
// and the reference SharedStashParser.java in the Java project.

#pragma once

#include "d2r/SharedStash.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace d2r {

class RefDb;

class SharedStashParser {
public:
    explicit SharedStashParser(const RefDb& refDb) noexcept : refDb_(refDb) {}

    // Locate the 55 AA 55 AA magic values that begin each tab. Public for testing.
    [[nodiscard]] static std::vector<std::size_t> findTabOffsets(std::span<const std::byte> bytes) noexcept;

    // Parse an entire shared-stash file.
    [[nodiscard]] SharedStash parse(std::span<const std::byte> bytes);

    // Parse only the chronicle tab (last tab in a 7-tab RotW file).
    [[nodiscard]] ChronicleTab parseChronicleOnly(std::span<const std::byte> bytes);

private:
    const RefDb& refDb_;
};

} // namespace d2r
