// Cross-version parse smoke test against vendored save files from paladijn's
// d2rsavegameparser Java project. See tests/fixtures/vendor/NOTICE.md.
//
// The bar is deliberately low: for every .d2s in the vendor tree we require
// that the character header parses without throwing and that the reported
// item count is plausible (>= 0, <= 2000). We do NOT require the item bit
// stream to fully decode -- some vendored files exercise edge cases we may
// not yet handle. Tighten individual asserts in follow-up commits as
// coverage improves.

#include "d2r/CharacterParser.hpp"
#include "d2r/Save.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

namespace {

fs::path vendorRoot() {
    if (const char* env = std::getenv("D2R_VENDOR_DIR")) return env;
#ifdef D2R_TEST_VENDOR_DIR
    return fs::path(D2R_TEST_VENDOR_DIR);
#else
    return fs::path("tests/fixtures/vendor");
#endif
}

// Files vendored from upstream that are known to be unusable and should be
// skipped rather than fixed in place (see tests/fixtures/vendor/NOTICE.md
// -- we deliberately do not modify vendored files). Paths are relative to
// the vendor root.
bool isSkipped(const fs::path& rel) {
    static constexpr std::string_view kSkip[] = {
        // The upstream committed a UTF-8-mangled copy of this file: the
        // magic bytes 0x55AA55AA appear as 55 EF BF BD 55 EF BF BD (i.e.
        // 0xAA re-encoded as U+FFFD twice). No parser can read it and
        // upstream's own tests reference the sibling Fjoerich-max-res.d2s
        // instead.
        "1.6.77312/Fjoerich.d2s",
    };
    const auto s = rel.generic_string();
    for (auto p : kSkip) if (s == p) return true;
    return false;
}

} // namespace

TEST_CASE("vendored corpus: every .d2s parses its character header",
          "[vendor][fixtures]") {
    const auto root = vendorRoot();
    REQUIRE(fs::exists(root));

    std::size_t seen = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.path().extension() != ".d2s") continue;
        const auto rel = fs::relative(entry.path(), root);
        if (isSkipped(rel)) continue;
        INFO(entry.path().string());
        const auto bytes = d2r::readFile(entry.path());
        d2r::Character ch;
        try {
            ch = d2r::parseCharacter(bytes);
        } catch (const std::exception& e) {
            FAIL("parse threw: " << e.what()
                 << " (file: " << entry.path().string() << ")");
        }
        // Note: some vendored files deliberately have blank names to
        // exercise corruption handling. We only require that the parser
        // did not throw and that level/itemCount are within plausible
        // ranges. Tighten in follow-up commits.
        REQUIRE(ch.level <= 99);
        REQUIRE(ch.itemCount <= 2000);
        ++seen;
    }
    // The vendored corpus should not be empty; if it is, the copy step was
    // skipped or the compile define is misconfigured.
    REQUIRE(seen > 0);
}
