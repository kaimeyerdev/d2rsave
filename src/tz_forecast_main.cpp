// d2r_tz_forecast: standalone CLI to forecast terror zones for a given
// window. Deliberately isolated from the main d2rsave binary so it can be
// run against candidate (anchor, hash) pairs during algorithm validation
// without touching the interactive dashboard.

#include "d2r/RefDb.hpp"
#include "d2r/TerrorZones.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

using namespace std::chrono;

// Parse an ISO8601 datetime "YYYY-MM-DDTHH:MM:SS[Z]" as UTC.
sys_seconds parseISO(const char* s) {
    int y, mo, d, h, mi, se;
    if (std::sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) {
        throw std::runtime_error(std::string("bad datetime (want YYYY-MM-DDTHH:MM:SS[Z]): ") + s);
    }
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon  = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min  = mi;
    tm.tm_sec  = se;
    // timegm is a GNU extension; interprets tm as UTC regardless of $TZ.
    const std::time_t t = timegm(&tm);
    return sys_seconds{seconds{t}};
}

std::string formatUTC(sys_seconds t) {
    const std::time_t tt =
        system_clock::to_time_t(system_clock::time_point{t});
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

int usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options]\n\n"
        "Offline forecast of Diablo II: Resurrected terror zones.\n"
        "Terror zones rotate every 30 minutes on the UTC clock.\n\n"
        "Options:\n"
        "  --anchor ISO8601        Epoch used as the RNG reference. Default:\n"
        "                          2023-02-16T19:00:00Z (community-published,\n"
        "                          UNVERIFIED -- override with --anchor to\n"
        "                          search for the right value).\n"
        "  --at ISO8601            Start of the forecast window. Default: now.\n"
        "  --slots N               Number of 30-minute slots to emit (default 12,\n"
        "                          = 6 hours of coverage).\n"
        "  --zones N               Zones per slot (default 1, matching\n"
        "                          d2tz.info/offline).\n"
        "  --reference-db PATH     Override the reference SQLite DB path.\n"
        "  -h, --help              Show this help.\n\n"
        "Example: enumerate a whole day starting at midnight UTC\n"
        "  %s --at 2026-07-06T00:00:00Z --slots 48\n",
        prog, prog);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    using namespace std::chrono;

    sys_seconds anchor = d2r::kDefaultTerrorZoneAnchor;
    sys_seconds at     = time_point_cast<seconds>(system_clock::now());
    int         slotsAhead   = 12;
    int         zonesPerSlot = 1;
    std::string dbOverride;

    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto need = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s expects an argument\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        try {
            if      (a == "--anchor")        anchor = parseISO(need("--anchor"));
            else if (a == "--at")            at     = parseISO(need("--at"));
            else if (a == "--slots")         slotsAhead   = std::atoi(need("--slots"));
            else if (a == "--zones")         zonesPerSlot = std::atoi(need("--zones"));
            else if (a == "--reference-db")  dbOverride   = need("--reference-db");
            else if (a == "-h" || a == "--help") return usage(argv[0]);
            else {
                std::fprintf(stderr, "error: unknown argument '%s'\n", argv[i]);
                return usage(argv[0]);
            }
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "error: %s\n", ex.what());
            return 2;
        }
    }

    const auto dbPath = d2r::findReferenceDb(argv[0], dbOverride);
    if (!dbPath) {
        std::fprintf(stderr, "error: reference DB not found. Set --reference-db or\n"
                             "       $D2R_REFERENCE_DB.\n");
        return 1;
    }
    d2r::RefDb db(*dbPath);

    const auto zones = d2r::loadTerrorZones(db);
    if (zones.empty()) {
        std::fprintf(stderr, "error: terrorzones table in %s is empty.\n",
                     dbPath->string().c_str());
        return 1;
    }

    const auto forecast = d2r::forecastTerrorZones(
        zones, anchor, at, slotsAhead, zonesPerSlot);

    std::printf("anchor : %s\n", formatUTC(anchor).c_str());
    std::printf("from   : %s\n", formatUTC(at).c_str());
    std::printf("slots  : %d x 30 min  (%d zones/slot, source: %zu total)\n\n",
                slotsAhead, zonesPerSlot, zones.size());

    std::unordered_map<int, const d2r::TerrorZone*> byId;
    for (const auto& z : zones) byId.emplace(z.id, &z);

    for (const auto& slotFcst : forecast) {
        std::printf("%s\n", formatUTC(slotFcst.slot).c_str());
        for (const int id : slotFcst.zoneIds) {
            const auto it = byId.find(id);
            const auto* z = it != byId.end() ? it->second : nullptr;
            std::printf("  #%-3d (act %d)  %s\n",
                        id,
                        z ? z->act : 0,
                        z ? z->name.c_str() : "?");
        }
    }
    return 0;
}
