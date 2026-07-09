// d2r_casc_dump: minimal read-only CLI for pulling arbitrary files out of
// a D2R CASC install. Deliberately separate from d2r_refdb_gen so that
// exploratory extraction (e.g. locating desecratedzones.json) doesn't
// require touching that tool's fixed catalogue.
//
// Usage:
//   d2r_casc_dump --d2r-install <path> --file <casc-path> [--out <local>]
//
// Prints the file to stdout by default, or writes it to --out. The casc
// path uses D2R's backslash form, e.g.
//   "data:data\global\excel\armor.txt"
//   "data:data\hd\global\excel\desecratedzones.json"

#include "d2r/CascStorage.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s --d2r-install <path> --file <casc-path> [--out <local>]\n\n"
        "Read-only dump of a single file from a D2R CASC storage.\n"
        "casc-path uses D2R's backslash form, e.g.\n"
        "  data:data\\hd\\global\\excel\\desecratedzones.json\n",
        prog);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    fs::path    install;
    std::string cascPath;
    fs::path    outPath;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s requires an argument\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        if      (a == "--d2r-install") install  = next("--d2r-install");
        else if (a == "--file")        cascPath = next("--file");
        else if (a == "--out")         outPath  = next("--out");
        else if (a == "-h" || a == "--help") return usage(argv[0]);
        else {
            std::fprintf(stderr, "error: unknown argument: %s\n", argv[i]);
            return usage(argv[0]);
        }
    }
    if (install.empty() || cascPath.empty()) {
        return usage(argv[0]);
    }

    try {
        d2r::CascStorage casc(install);
        const auto bytes = casc.readFile(cascPath);

        if (outPath.empty()) {
            std::fwrite(bytes.data(), 1, bytes.size(), stdout);
        } else {
            std::ofstream out(outPath, std::ios::binary);
            if (!out) {
                std::fprintf(stderr, "error: cannot open %s for writing\n",
                             outPath.string().c_str());
                return 1;
            }
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            std::fprintf(stderr, "wrote %zu bytes to %s\n",
                         bytes.size(), outPath.string().c_str());
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 0;
}
