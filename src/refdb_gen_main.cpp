// d2r_refdb_gen — reads a D2R install directly via CascLib and emits the
// SQL seed files under data/sql/*.sql. Read-only: never writes back into
// the D2R install path.
//
// Usage:
//   d2r_refdb_gen --d2r-install <path> [--out <dir>] [--file <name>]...
//
// If --file is omitted, all 17 tables plus MANIFEST are rewritten. Repeat
// --file to emit a subset; the argument matches either the SQL table name
// ("armor") or the on-disk basename without the numeric prefix
// ("armor.sql"). MANIFEST is only rewritten on a full generation.

#include "d2r/CascStorage.hpp"
#include "d2r/ExcelTable.hpp"
#include "d2r/SqlEmitter.hpp"
#include "d2r/StringTable.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

enum class SourceKind { Excel, Json };

struct TableEntry {
    int index;                 // sequence used for the file prefix
    std::string_view name;     // SQL table + basename stem
    std::string_view cascPath; // path inside CASC storage
    SourceKind kind;
};

constexpr std::array<TableEntry, 17> kCatalogue{{
    { 1,  "itemstatcost", "data:data\\global\\excel\\itemstatcost.txt", SourceKind::Excel },
    { 2,  "properties",   "data:data\\global\\excel\\properties.txt",   SourceKind::Excel },
    { 3,  "runes",        "data:data\\global\\excel\\runes.txt",        SourceKind::Excel },
    { 4,  "gems",         "data:data\\global\\excel\\gems.txt",         SourceKind::Excel },
    { 5,  "armor",        "data:data\\global\\excel\\armor.txt",        SourceKind::Excel },
    { 6,  "weapons",      "data:data\\global\\excel\\weapons.txt",      SourceKind::Excel },
    { 7,  "misc",         "data:data\\global\\excel\\misc.txt",         SourceKind::Excel },
    { 8,  "uniqueitems",  "data:data\\global\\excel\\uniqueitems.txt",  SourceKind::Excel },
    { 9,  "setitems",     "data:data\\global\\excel\\setitems.txt",     SourceKind::Excel },
    { 10, "sets",         "data:data\\global\\excel\\sets.txt",         SourceKind::Excel },
    { 11, "magicprefix",  "data:data\\global\\excel\\magicprefix.txt",  SourceKind::Excel },
    { 12, "magicsuffix",  "data:data\\global\\excel\\magicsuffix.txt",  SourceKind::Excel },
    { 13, "rareprefix",   "data:data\\global\\excel\\rareprefix.txt",   SourceKind::Excel },
    { 14, "raresuffix",   "data:data\\global\\excel\\raresuffix.txt",   SourceKind::Excel },
    { 15, "hireling",     "data:data\\global\\excel\\hireling.txt",     SourceKind::Excel },
    { 16, "item_names",   "data:data\\local\\lng\\strings\\item-names.json", SourceKind::Json },
    { 17, "item_runes",   "data:data\\local\\lng\\strings\\item-runes.json", SourceKind::Json },
}};

std::string basenameFor(const TableEntry& e) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d_", e.index);
    return std::string(buf) + std::string(e.name) + ".sql";
}

std::string excelSourceNameFor(const TableEntry& e) {
    // Trim leading directory from cascPath to just the basename.
    // cascPath uses backslashes (CASC storage layout).
    const auto slash = e.cascPath.find_last_of("\\/");
    if (slash == std::string_view::npos) return std::string(e.cascPath);
    return std::string(e.cascPath.substr(slash + 1));
}

void emitExcel(const d2r::CascStorage& casc,
               const TableEntry& e,
               const fs::path& outFile) {
    const auto text = casc.readFileText(e.cascPath);
    const auto table = d2r::parseExcelTable(text);
    const auto attribution = d2r::excelAttribution(excelSourceNameFor(e));

    std::ofstream out(outFile);
    if (!out) throw std::runtime_error("cannot open " + outFile.string() + " for writing");
    d2r::writeExcelSql(out, e.name, table, attribution);

    std::cout << "  " << basenameFor(e) << "  <- " << e.cascPath
              << "  (" << table.rows.size() << " rows, "
              << table.columns.size() << " cols)\n";
}

void emitJson(const d2r::CascStorage& casc,
              const TableEntry& e,
              const fs::path& outFile) {
    const auto text = casc.readFileText(e.cascPath);
    const auto rows = d2r::parseStringTable(text);
    const auto attribution = d2r::jsonAttribution(excelSourceNameFor(e));

    std::ofstream out(outFile);
    if (!out) throw std::runtime_error("cannot open " + outFile.string() + " for writing");
    d2r::writeStringTableSql(out, e.name, rows, attribution);

    std::cout << "  " << basenameFor(e) << "  <- " << e.cascPath
              << "  (" << rows.size() << " rows)\n";
}

void emitManifest(const fs::path& outDir) {
    // Preserves the pre-existing "00_schema.sql" as the first entry.
    std::ofstream out(outDir / "MANIFEST");
    if (!out) throw std::runtime_error("cannot open MANIFEST for writing");
    out << "00_schema.sql\n";
    for (const auto& e : kCatalogue) {
        out << basenameFor(e) << "\n";
    }
}

bool matchesFilter(const TableEntry& e,
                   const std::vector<std::string>& filters) {
    if (filters.empty()) return true;
    for (const auto& f : filters) {
        if (f == e.name || f == basenameFor(e)) return true;
    }
    return false;
}

struct Args {
    fs::path d2rInstall;
    fs::path outDir = ".";
    std::vector<std::string> files;
};

std::optional<Args> parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "error: " << flag << " requires an argument\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--d2r-install") a.d2rInstall = next("--d2r-install");
        else if (arg == "--out")    a.outDir     = next("--out");
        else if (arg == "--file")   a.files.push_back(next("--file"));
        else if (arg == "--help" || arg == "-h") {
            std::cout <<
                "Usage: d2r_refdb_gen --d2r-install <path> [--out <dir>] [--file <name>]...\n"
                "\n"
                "Reads read-only from the D2R install (via CascLib) and emits\n"
                "SQL seed files matching data/sql/*.sql. --out defaults to '.'.\n"
                "--file may repeat and matches SQL table name (e.g. 'armor') or\n"
                "output basename (e.g. '05_armor.sql'). If omitted, all tables\n"
                "plus MANIFEST are rewritten.\n";
            return std::nullopt;
        }
        else {
            std::cerr << "error: unknown argument: " << arg << "\n";
            std::exit(2);
        }
    }
    if (a.d2rInstall.empty()) {
        std::cerr << "error: --d2r-install is required\n";
        std::exit(2);
    }
    return a;
}

} // namespace

int main(int argc, char** argv) {
    auto parsed = parseArgs(argc, argv);
    if (!parsed) return 0;
    const Args& args = *parsed;

    try {
        std::error_code ec;
        fs::create_directories(args.outDir, ec);
        if (ec) {
            std::cerr << "error: cannot create --out dir '" << args.outDir
                      << "': " << ec.message() << "\n";
            return 1;
        }

        std::cout << "Opening D2R storage at: " << args.d2rInstall << "\n";
        d2r::CascStorage casc(args.d2rInstall);

        std::cout << "Emitting SQL to: " << args.outDir << "\n";
        std::size_t emitted = 0;
        for (const auto& e : kCatalogue) {
            if (!matchesFilter(e, args.files)) continue;
            const fs::path outFile = args.outDir / basenameFor(e);
            switch (e.kind) {
                case SourceKind::Excel: emitExcel(casc, e, outFile); break;
                case SourceKind::Json:  emitJson(casc, e, outFile);  break;
            }
            ++emitted;
        }

        if (args.files.empty()) {
            emitManifest(args.outDir);
            std::cout << "  MANIFEST\n";
        }

        std::cout << "Done. " << emitted << " table(s) emitted.\n";
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
