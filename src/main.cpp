// d2rsave: D2R save-file CLI.
//
// Phase 3 subcommands (no external dependencies):
//   verify   <file>              — print stored + computed checksum, name, seed.
//   rename   <file> <name>       — overwrite the 16-byte name field, recompute checksum.
//   set-seed <file> <u32>        — overwrite the map seed, recompute checksum.
//   checksum <file>              — recompute + write checksum only.
//
// Later phases will add: list-items, chronicle, dump.

#include "d2r/Save.hpp"
#include "d2r/CharacterParser.hpp"

#include <charconv>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <thread>
#include <functional>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if D2R_HAVE_SQLITE
#include "d2r/ItemParser.hpp"
#include "d2r/RefDb.hpp"
#include "d2r/SharedStashParser.hpp"
#include "d2r/SunderCharms.hpp"
#endif

#if D2R_HAVE_INOTIFY
#include "d2r/Watcher.hpp"
#include <unistd.h>
#endif

namespace {

int usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [--path <dir>] [--reference-db <path>] [--watch] <command> <args...>\n"
        "\n"
        "Global flags:\n"
        "  --path <dir>              Directory containing D2R save files (.d2s, .d2i).\n"
        "                            Required for every command except 'db-info'.\n"
        "                            Character names below resolve to <dir>/<name>.d2s.\n"
        "  --reference-db <path>     Override the reference SQLite DB location.\n"
#if D2R_HAVE_INOTIFY
        "  --watch                   Re-run the command whenever a file in --path\n"
        "                            changes. Available for read-only commands only.\n"
        "                            Ctrl-C to exit.\n"
#else
        "  --watch                   (unavailable: this build has no inotify support)\n"
#endif
        "\n"
        "Character commands (<name> is the file stem, e.g. Kai for Kai.d2s):\n"
        "  verify   <name>              Print header info, name, seed, and checksum status.\n"
        "  rename   <name> <newname>    Set character name (<=15 chars) and recompute checksum.\n"
        "  set-seed <name> <u32>        Set the map seed and recompute checksum.\n"
        "  checksum <name>              Recompute and write the checksum.\n"
        "  dump     <name>              Parse the whole file and print all decoded fields.\n"
        "\n"
        "Account commands (auto-discovers stash + scans every .d2s in --path):\n"
        "  items   [--character NAME]... [--shared-stash]\n"
        "          [--inferior] [--normal] [--superior] [--magic]\n"
        "          [--set] [--rare] [--unique] [--craft]\n"
        "          [--tier-normal] [--tier-exceptional] [--tier-elite] [--tier-misc]\n"
        "          [STR STR ...]\n"
        "                               List items across the selected scope.\n"
        "                               Scope: --character (repeatable) and\n"
        "                               --shared-stash pick sources; absence of\n"
        "                               both means every character + every\n"
        "                               stash tab. Quality and tier flags each\n"
        "                               OR together within their group and AND\n"
        "                               across groups; with none of either,\n"
        "                               that filter is disabled. Tier applies\n"
        "                               to armor/weapons (normal/exceptional/\n"
        "                               elite) and to misc-table bases\n"
        "                               (--tier-misc: amulets, rings, jewels,\n"
        "                               charms). Items whose base is in none\n"
        "                               of those tables are excluded whenever\n"
        "                               any --tier-* flag is set.\n"
        "                               Trailing positionals are case-\n"
        "                               insensitive substring queries matched\n"
        "                               against each item's name and base type.\n"
        "                               Rows sorted alphabetically by name;\n"
        "                               duplicates preserved.\n"
#if D2R_ENABLE_RUNEWORDS_WIP
        "  chronicle [--uniques] [--sets] [--runewords]\n"
#else
        "  chronicle [--uniques] [--sets]\n"
#endif
        "            [--remaining] [--discovered]\n"
        "            [--tier-normal] [--tier-exceptional] [--tier-elite] [--tier-misc]\n"
        "            [--query STR ...]\n"
        "                               Chronicle progress + item lists.\n"
        "                               Category selectors restrict the report;\n"
        "                               with none, all available categories are\n"
        "                               shown. --remaining shows items NOT yet\n"
        "                               chronicled; --discovered shows those\n"
        "                               already chronicled. Neither, or both,\n"
        "                               shows every item with a [X]/[ ] marker.\n"
        "                               --tier-* restricts by base-item tier:\n"
        "                               normal/exceptional/elite for armor and\n"
        "                               weapons, and misc for amulets, rings,\n"
        "                               jewels, and charms. Runewords have no\n"
        "                               base code and are suppressed whenever\n"
        "                               a tier filter is active). --query STR ...\n"
        "                               is a case-insensitive substring filter\n"
        "                               over item name / base type / set name;\n"
        "                               it consumes ALL remaining arguments so\n"
        "                               it must come last.\n"
#if !D2R_ENABLE_RUNEWORDS_WIP
        "                               Runewords are compiled out (build with\n"
        "                               -DD2R_ENABLE_RUNEWORDS_WIP=ON to enable).\n"
#endif
        "  reconcile                    Diff owned uniques/sets against the chronicle.\n"
        "\n"
        "Diagnostic commands:\n"
        "  db-info                      Show reference DB path and per-table row counts.\n",
        prog);
    return 2;
}

bool parseU32(std::string_view s, std::uint32_t& out) {
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.remove_prefix(2);
        base = 16;
    }
    const auto* first = s.data();
    const auto* last  = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, out, base);
    return ec == std::errc{} && ptr == last;
}

// Uniform per-item display line used by every command that prints items
// (items, chronicle, reconcile). Three logical fields; each may be empty:
//   name     -- proper item name (e.g. "Steel Shade"), or the base type
//               for items without a distinct name.
//   type     -- base item type (e.g. "Armet"). Omitted when equal to name
//               or empty (e.g. runewords).
//   location -- where the item currently lives (e.g. "Kai.d2s",
//               "stash tab 3"). Empty when unknown or not applicable.
//
// Format: 'Name  [Type]  @ Location', columnar-ish so operators can scan
// by eye. See TODO.md for planned embellishments (id, quality, ilvl,
// colour, verbosity flag, ...).
std::string formatItem(std::string_view name,
                       std::string_view type,
                       std::string_view location = {}) {
    std::string_view primary = name.empty() ? type : name;
    std::string line;
    line.reserve(96);
    line += primary.empty() ? "?" : primary;
    if (line.size() < 32) line.resize(32, ' ');
    const bool showType = !type.empty() && type != primary;
    if (showType) {
        line += "  [";
        line += type;
        line += ']';
    }
    if (!location.empty()) {
        if (line.size() < 60) line.resize(60, ' ');
        line += "  @ ";
        line += location;
    }
    return line;
}

// Case-insensitive substring: does haystack contain any needle?
// An empty needles list matches everything (defensive; callers usually
// gate this call so they only invoke it when there's a real filter).
bool containsAnyCI(std::string_view haystack,
                   const std::vector<std::string>& needles) {
    if (needles.empty()) return true;
    auto lc = [](unsigned char c) { return std::tolower(c); };
    for (const auto& n : needles) {
        if (n.empty()) continue;
        auto it = std::search(haystack.begin(), haystack.end(),
                              n.begin(), n.end(),
                              [&](char a, char b) { return lc(a) == lc(b); });
        if (it != haystack.end()) return true;
    }
    return false;
}

// Format a query list as e.g. '"sword", "axe"' for section headers.
std::string quoteQueries(const std::vector<std::string>& queries) {
    std::string out;
    for (std::size_t i = 0; i < queries.size(); ++i) {
        if (i) out += ", ";
        out += '"';
        out += queries[i];
        out += '"';
    }
    return out;
}

#if D2R_HAVE_SQLITE
// Resolve a 3-char base item code (from armor/weapons/misc) to its display
// name. Returns the code itself if the lookup misses, so output degrades
// gracefully rather than showing a blank column.
std::string lookupBaseName(d2r::RefDb& db, std::string_view code) {
    if (code.empty()) return {};
    auto st = db.prepare(
        "SELECT COALESCE(inm.en_us, b.name) "
        "FROM (SELECT code, name, namestr FROM armor "
        "      UNION ALL SELECT code, name, namestr FROM weapons "
        "      UNION ALL SELECT code, name, namestr FROM misc) b "
        "LEFT JOIN item_names inm ON inm.\"key\" = b.namestr "
        "WHERE b.code = ? LIMIT 1");
    st.bind(1, code);
    if (st.step()) return st.columnText(0);
    return std::string(code);
}
#endif

// Bitmask filter over ItemQuality. Absent (mask==0) means "pass everything";
// otherwise a bit at position q means "pass items of quality q".
struct QualityFilter {
    std::uint32_t mask = 0;
    bool active() const noexcept { return mask != 0; }
    void enable(d2r::ItemQuality q) noexcept {
        mask |= 1u << static_cast<int>(q);
    }
    bool allows(d2r::ItemQuality q) const noexcept {
        return !active() || ((mask >> static_cast<int>(q)) & 1u);
    }
};

// Recognise a --<quality> flag ("--unique", "--set", "--normal", ...) and
// enable the corresponding bit. Returns true iff `a` was a quality flag.
bool tryConsumeQualityFlag(std::string_view a, QualityFilter& qf) {
    if      (a == "--inferior") { qf.enable(d2r::ItemQuality::Inferior); return true; }
    else if (a == "--normal")   { qf.enable(d2r::ItemQuality::Normal);   return true; }
    else if (a == "--superior") { qf.enable(d2r::ItemQuality::Superior); return true; }
    else if (a == "--magic")    { qf.enable(d2r::ItemQuality::Magic);    return true; }
    else if (a == "--set")      { qf.enable(d2r::ItemQuality::Set);      return true; }
    else if (a == "--rare")     { qf.enable(d2r::ItemQuality::Rare);     return true; }
    else if (a == "--unique")   { qf.enable(d2r::ItemQuality::Unique);   return true; }
    else if (a == "--craft")    { qf.enable(d2r::ItemQuality::Craft);    return true; }
    return false;
}

// Tier of the item's *base type* (independent of quality). Armor and
// weapons come in three tiers (normal / exceptional / elite); items whose
// base lives in misc.txt (amulets, rings, jewels, charms) have no tier
// concept and are reported under Misc. Items with a base that isn't in
// any of the three tables (should not happen for real chronicle rows)
// stay as None and are excluded whenever any tier filter is active.
enum class ItemTier : std::uint8_t {
    None = 0, Normal = 1, Exceptional = 2, Elite = 3, Misc = 4
};

// Bitmask filter over ItemTier. Same shape as QualityFilter: mask==0
// disables the filter, otherwise a bit at position tier passes items of
// that tier.
struct TierFilter {
    std::uint32_t mask = 0;
    bool active() const noexcept { return mask != 0; }
    void enable(ItemTier t) noexcept { mask |= 1u << static_cast<int>(t); }
    bool allows(ItemTier t) const noexcept {
        return !active() || ((mask >> static_cast<int>(t)) & 1u);
    }
};

bool tryConsumeTierFlag(std::string_view a, TierFilter& tf) {
    if      (a == "--tier-normal")      { tf.enable(ItemTier::Normal);      return true; }
    else if (a == "--tier-exceptional") { tf.enable(ItemTier::Exceptional); return true; }
    else if (a == "--tier-elite")       { tf.enable(ItemTier::Elite);       return true; }
    else if (a == "--tier-misc")        { tf.enable(ItemTier::Misc);        return true; }
    return false;
}

// Which sources the `items` command should include. Empty characters + no
// stash flag = every source (implicit "all"). Otherwise restrict to the
// listed characters and/or the shared stash.
struct ItemScope {
    bool                            anyExplicit  = false;
    std::unordered_set<std::string> characters;   // filename stems, e.g. "Kai"
    bool                            includeStash = false;
    bool allowsCharacter(std::string_view stem) const {
        if (!anyExplicit) return true;
        return characters.contains(std::string(stem));
    }
    bool allowsStash() const { return !anyExplicit || includeStash; }
};

int cmdVerify(const std::string& path) {
    auto bytes = d2r::readFile(path);
    const bool magicOk = d2r::hasValidMagic(bytes);
    const auto stored = d2r::readStoredChecksum(bytes);
    const auto computed = d2r::computeChecksum(bytes);
    const auto name = d2r::readCharacterName(bytes);
    const auto seed = d2r::readMapSeed(bytes);

    std::printf("file:     %s (%zu bytes)\n", path.c_str(), bytes.size());
    std::printf("magic:    %s\n", magicOk ? "OK (0xAA55AA55)" : "INVALID");
    std::printf("name:     \"%s\"\n", name.c_str());
    std::printf("seed:     %u (0x%08X)\n", seed, seed);
    std::printf("checksum: stored=0x%08X  computed=0x%08X  %s\n",
                stored, computed,
                stored == computed ? "OK" : "MISMATCH");
    return (magicOk && stored == computed) ? 0 : 1;
}

int cmdRename(const std::string& path, std::string_view newName) {
    auto bytes = d2r::readFile(path);
    if (!d2r::hasValidMagic(bytes)) {
        std::fprintf(stderr, "error: %s: invalid magic\n", path.c_str());
        return 1;
    }
    const auto oldName = d2r::readCharacterName(bytes);
    if (!d2r::writeCharacterName(bytes, newName)) {
        std::fprintf(stderr, "error: name must be 1..15 chars, no control chars\n");
        return 1;
    }
    const auto newChecksum = d2r::recomputeAndWriteChecksum(bytes);
    d2r::writeFileAtomic(path, bytes);
    std::printf("name:     \"%s\" -> \"%.*s\"\n",
                oldName.c_str(),
                static_cast<int>(newName.size()), newName.data());
    std::printf("checksum: 0x%08X\n", newChecksum);
    return 0;
}

int cmdSetSeed(const std::string& path, std::string_view seedStr) {
    std::uint32_t seed = 0;
    if (!parseU32(seedStr, seed)) {
        std::fprintf(stderr, "error: seed must be a decimal or 0x-hex u32\n");
        return 1;
    }
    auto bytes = d2r::readFile(path);
    if (!d2r::hasValidMagic(bytes)) {
        std::fprintf(stderr, "error: %s: invalid magic\n", path.c_str());
        return 1;
    }
    const auto oldSeed = d2r::readMapSeed(bytes);
    d2r::writeMapSeed(bytes, seed);
    const auto newChecksum = d2r::recomputeAndWriteChecksum(bytes);
    d2r::writeFileAtomic(path, bytes);
    std::printf("seed:     %u -> %u\n", oldSeed, seed);
    std::printf("checksum: 0x%08X\n", newChecksum);
    return 0;
}

int cmdChecksum(const std::string& path) {
    auto bytes = d2r::readFile(path);
    if (!d2r::hasValidMagic(bytes)) {
        std::fprintf(stderr, "error: %s: invalid magic\n", path.c_str());
        return 1;
    }
    const auto oldChecksum = d2r::readStoredChecksum(bytes);
    const auto newChecksum = d2r::recomputeAndWriteChecksum(bytes);
    if (oldChecksum != newChecksum) {
        d2r::writeFileAtomic(path, bytes);
    }
    std::printf("checksum: 0x%08X -> 0x%08X%s\n",
                oldChecksum, newChecksum,
                oldChecksum == newChecksum ? " (unchanged)" : "");
    return 0;
}

int cmdDump(const std::string& path) {
    const auto bytes = d2r::readFile(path);
    const auto c = d2r::parseCharacter(bytes);

    std::printf("file:      %s (%zu bytes)\n", path.c_str(), bytes.size());
    std::printf("version:   %u\n", c.version);
    std::printf("checksum:  stored=0x%08X computed=0x%08X %s\n",
                c.storedChecksum, c.computedChecksum,
                c.storedChecksum == c.computedChecksum ? "OK" : "MISMATCH");
    std::printf("name:      \"%s\"\n", c.name.c_str());
    const auto cls = d2r::toString(c.characterClass);
    std::printf("class:     %.*s (byte=%u)\n",
                static_cast<int>(cls.size()), cls.data(),
                static_cast<unsigned>(c.characterClass));
    std::printf("level:     %u  (header) / %u (attributes)\n", c.level, c.attributes.level);
    std::printf("flags:     hardcore=%d died=%d LoD=%d RotW=%d actProgression=%u\n",
                c.hardcore, c.died, c.lordOfDestruction, c.reignOfTheWarlock,
                c.actProgression);

    const std::time_t ts = c.timestamp;
    char timebuf[32] = {};
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", std::gmtime(&ts));
    std::printf("timestamp: %u (%s UTC)\n", c.timestamp, timebuf);
    std::printf("map seed:  %u (0x%08X)\n", c.mapId, c.mapId);

    static constexpr const char* kDiff[] = {"Normal", "Nightmare", "Hell"};
    for (std::size_t i = 0; i < 3; ++i) {
        std::printf("  %-10s active=%d act=%u\n", kDiff[i],
                    c.locations[i].active, c.locations[i].act);
    }
    std::printf("mercenary: alive=%d id=0x%08X name=%u type=%u xp=%u\n",
                c.mercenary.alive, c.mercenary.controlSeed,
                c.mercenary.nameId, c.mercenary.typeId, c.mercenary.experience);

    const auto& a = c.attributes;
    std::printf("attrs:     STR=%u DEX=%u VIT=%u ENG=%u statPts=%u skillPts=%u\n",
                a.strength, a.dexterity, a.vitality, a.energy,
                a.statPointsLeft, a.skillPointsLeft);
    std::printf("           HP=%llu/%llu MP=%llu/%llu STAM=%llu/%llu\n",
                (unsigned long long)a.hp,      (unsigned long long)a.maxHP,
                (unsigned long long)a.mana,    (unsigned long long)a.maxMana,
                (unsigned long long)a.stamina, (unsigned long long)a.maxStamina);
    std::printf("           gold=%u  stash=%u  exp=%u\n",
                a.gold, a.goldInStash, a.experience);

    std::printf("sections:  quests=0x%zX waypoints=0x%zX npc=0x%zX stats=0x%zX skills=0x%zX items=0x%zX\n",
                c.questsOffset, c.waypointsOffset, c.npcOffset,
                c.statsOffset, c.skillsOffset, c.itemsOffset);

    if (!c.skills.empty()) {
        std::printf("skills:   ");
        int printed = 0;
        for (const auto& s : c.skills) {
            if (s.level != 0) {
                std::printf(" [%u]=%u", s.slot, s.level);
                ++printed;
            }
        }
        if (printed == 0) std::printf(" (all zero)");
        std::printf("\n");
    }

    std::printf("items:     character=%u  corpse=%u  mercenary=%u  iron-golem=%d\n",
                c.itemCount, c.corpseItemCount, c.mercItemCount, c.hasIronGolem);
    return 0;
}
#if D2R_HAVE_SQLITE
int cmdDbInfo(const std::filesystem::path& exePath, std::string_view override) {
    const auto dbPath = d2r::findReferenceDb(exePath, override);
    if (!dbPath) {
        std::fprintf(stderr,
            "error: reference DB not found. Build the 'reference_db' target or\n"
            "       set D2R_REFERENCE_DB / pass --reference-db.\n");
        return 1;
    }
    std::printf("reference-db: %s\n", dbPath->string().c_str());
    d2r::RefDb db(*dbPath);
    for (const char* t : {"armor","weapons","misc","uniqueitems","setitems","sets",
                          "itemstatcost","properties","magicprefix","magicsuffix",
                          "rareprefix","raresuffix","runes","gems","hireling"}) {
        std::printf("  %-14s %6lld\n", t,
                    static_cast<long long>(db.countRows(t)));
    }
    return 0;
}

// Enumerate every item across the requested scope (characters +
// shared-stash tabs, honouring ItemScope), apply quality and substring
// filters, sort alphabetically by primary display name, and print via
// formatItem. Every row carries its own location so the output is
// grep-friendly and cross-source comparisons are trivial.
int cmdItems(const std::filesystem::path& exePath,
             const std::string& stashPath,
             const std::string& scanDir,
             const ItemScope& scope,
             const QualityFilter& qf,
             const TierFilter& tf,
             const std::vector<std::string>& queries) {
    const auto dbPath = d2r::findReferenceDb(exePath);
    if (!dbPath) {
        std::fprintf(stderr, "error: reference DB not found\n");
        return 1;
    }
    d2r::RefDb db(*dbPath);
    db.loadItemTables();

    // Precompute the tier of every base item code. Armor/weapons have three
    // tiers (normal / exceptional / elite); misc-table bases (amulets,
    // rings, jewels, charms) map to ItemTier::Misc. When a tier filter is
    // active, only tiers in the filter set are kept, so
    // `unique exceptional` still hides charms but `unique misc` or a
    // multi-select like `unique normal misc` includes them.
    std::unordered_map<std::string, ItemTier> tierByCode;
    {
        auto st = db.prepare(
            "SELECT code, normcode, ubercode, ultracode FROM armor "
            "UNION ALL "
            "SELECT code, normcode, ubercode, ultracode FROM weapons");
        while (st.step()) {
            const auto code = st.columnText(0);
            const auto norm = st.columnText(1);
            const auto uber = st.columnText(2);
            const auto ultra = st.columnText(3);
            if (code.empty()) continue;
            ItemTier t = ItemTier::None;
            if      (code == norm)  t = ItemTier::Normal;
            else if (code == uber)  t = ItemTier::Exceptional;
            else if (code == ultra) t = ItemTier::Elite;
            tierByCode.emplace(code, t);
        }
        auto stMisc = db.prepare("SELECT code FROM misc");
        while (stMisc.step()) {
            const auto code = stMisc.columnText(0);
            if (!code.empty()) tierByCode.emplace(code, ItemTier::Misc);
        }
    }
    auto tierFor = [&](std::string_view code) -> ItemTier {
        if (code.empty()) return ItemTier::None;
        auto it = tierByCode.find(std::string(code));
        return it == tierByCode.end() ? ItemTier::None : it->second;
    };

    struct Row {
        std::string       name;
        std::string       type;
        std::string       location;
        d2r::ItemQuality  quality = d2r::ItemQuality::None;
        std::string       sortKey;  // lowercased name for stable ordering
    };
    std::vector<Row> rows;

    auto pushIfMatches = [&](const d2r::Item& it, const std::string& location) {
        if (!qf.allows(it.quality))     return;
        if (!tf.allows(tierFor(it.code))) return;
        // Primary display name: unique/set items expose a real name; other
        // qualities take the base item name as their primary label.
        auto type = lookupBaseName(db, it.code);
        const bool named = it.quality == d2r::ItemQuality::Unique
                        || it.quality == d2r::ItemQuality::Set;
        std::string name = named && !it.itemName.empty() ? it.itemName : type;
        if (!queries.empty()) {
            const bool hit = containsAnyCI(name, queries)
                          || containsAnyCI(type, queries);
            if (!hit) return;
        }
        Row r;
        r.name     = std::move(name);
        r.type     = std::move(type);
        r.location = location;
        r.quality  = it.quality;
        r.sortKey.reserve(r.name.size());
        for (unsigned char c : r.name) r.sortKey.push_back(std::tolower(c));
        rows.push_back(std::move(r));
    };

    // Shared-stash tabs (numbered from 1 for user output).
    if (scope.allowsStash() && !stashPath.empty()) {
        try {
            const auto bytes = d2r::readFile(stashPath);
            d2r::SharedStashParser sp(db);
            const auto stash = sp.parse(bytes);
            for (std::size_t i = 0; i < stash.tabs.size(); ++i) {
                const std::string loc = "stash tab " + std::to_string(i + 1);
                for (const auto& it : stash.tabs[i].items) {
                    pushIfMatches(it, loc);
                    for (const auto& s : it.socketedItems) pushIfMatches(s, loc);
                }
            }
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "  skip stash %s: %s\n",
                         stashPath.c_str(), ex.what());
        }
    }

    // Character .d2s files (+ merc / corpse / iron-golem).
    if (!scanDir.empty()) {
        for (const auto& entry : std::filesystem::directory_iterator(scanDir)) {
            if (entry.path().extension() != ".d2s") continue;
            const auto stem = entry.path().stem().string();
            if (!scope.allowsCharacter(stem)) continue;
            try {
                const auto bytes = d2r::readFile(entry.path());
                const auto ch    = d2r::parseCharacter(bytes);
                if (ch.itemsOffset == 0) continue;
                d2r::ItemParser p(db);
                const auto file = entry.path().filename().string();
                auto record = [&](const std::vector<d2r::Item>& items,
                                  const std::string& loc) {
                    for (const auto& it : items) {
                        pushIfMatches(it, loc);
                        for (const auto& s : it.socketedItems) pushIfMatches(s, loc);
                    }
                };
                record(p.parseItems(bytes, ch.itemsOffset), file);
                if (ch.mercItemsJMOffset) {
                    record(p.parseItems(bytes, ch.mercItemsJMOffset),
                           file + " (merc)");
                }
                if (ch.corpseJMOffset && ch.corpseItemCount == 1) {
                    record(p.parseItems(bytes, ch.corpseJMOffset + 16),
                           file + " (corpse)");
                }
                if (ch.hasIronGolem && ch.ironGolemItemOffset) {
                    try {
                        std::vector<d2r::Item> golem{
                            p.parseSingleItem(bytes, ch.ironGolemItemOffset)};
                        record(golem, file + " (iron golem)");
                    } catch (const std::exception&) {
                        // Iron-golem parse issues are tolerated -- match reconcile.
                    }
                }
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "  skip %s: %s\n",
                             entry.path().filename().c_str(), ex.what());
            }
        }
    }

    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.sortKey < b.sortKey; });

    // Header echoes the active filters so a scrollback trail is
    // self-explanatory. Scope, quality, and query clauses each appear only
    // when the user actually asked for them.
    auto qualityNames = [&]() -> std::string {
        if (!qf.active()) return {};
        static constexpr std::pair<d2r::ItemQuality, const char*> kNames[] = {
            {d2r::ItemQuality::Inferior, "inferior"},
            {d2r::ItemQuality::Normal,   "normal"},
            {d2r::ItemQuality::Superior, "superior"},
            {d2r::ItemQuality::Magic,    "magic"},
            {d2r::ItemQuality::Set,      "set"},
            {d2r::ItemQuality::Rare,     "rare"},
            {d2r::ItemQuality::Unique,   "unique"},
            {d2r::ItemQuality::Craft,    "craft"},
        };
        std::string out;
        for (const auto& [q, name] : kNames) {
            if (!qf.allows(q)) continue;
            if (!out.empty()) out += ", ";
            out += name;
        }
        return out;
    };
    auto scopeNames = [&]() -> std::string {
        if (!scope.anyExplicit) return {};
        // Deterministic ordering: characters alphabetical, shared stash last.
        std::vector<std::string> sorted(scope.characters.begin(),
                                        scope.characters.end());
        std::sort(sorted.begin(), sorted.end());
        std::string out;
        for (const auto& c : sorted) {
            if (!out.empty()) out += ", ";
            out += c;
        }
        if (scope.includeStash) {
            if (!out.empty()) out += ", ";
            out += "shared stash";
        }
        return out;
    };
    auto tierNames = [&]() -> std::string {
        if (!tf.active()) return {};
        static constexpr std::pair<ItemTier, const char*> kNames[] = {
            {ItemTier::Normal,      "normal"},
            {ItemTier::Exceptional, "exceptional"},
            {ItemTier::Elite,       "elite"},
            {ItemTier::Misc,        "misc"},
        };
        std::string out;
        for (const auto& [t, name] : kNames) {
            if (!tf.allows(t)) continue;
            if (!out.empty()) out += ", ";
            out += name;
        }
        return out;
    };

    std::string header = "items";
    if (const auto s = scopeNames();   !s.empty()) header += " on " + s;
    if (!queries.empty())                          header += " matching " + quoteQueries(queries);
    std::string parens;
    if (const auto t = tierNames();    !t.empty()) parens = "tier: " + t;
    if (const auto q = qualityNames(); !q.empty()) {
        if (!parens.empty()) parens += "; ";
        parens += q;
    }
    if (!parens.empty()) header += " (" + parens + ")";
    std::printf("== %s -- %zu shown ==\n", header.c_str(), rows.size());
    for (const auto& r : rows) {
        std::printf("  %s\n", formatItem(r.name, r.type, r.location).c_str());
    }
    return 0;
}

namespace {

// The chronicle command shows two axes of items:
//   showRemaining  -- items not yet chronicled ("undiscovered", per D2R).
//   showDiscovered -- items already in the chronicle.
// Neither flag set -> both true (default is "show the entire list").
// A query is an additional case-insensitive substring filter that matches
// on the item's display name AND its base type (for sets, also the set
// name). A tier filter restricts by base-item tier (normal/exceptional/
// elite); runewords and misc bases have no tier so those categories are
// suppressed when any --tier-* is active.

} // namespace

int cmdChronicle(const std::filesystem::path& exePath,
                 const std::string& stashPath,
                 const std::string& scanDir,
                 bool showUniques, bool showSets, bool showRunewords,
                 bool showRemaining, bool showDiscovered,
                 const TierFilter& tf,
                 const std::vector<std::string>& queries) {
    const auto dbPath = d2r::findReferenceDb(exePath);
    if (!dbPath) { std::fprintf(stderr, "error: reference DB not found\n"); return 1; }
    d2r::RefDb db(*dbPath);
    db.loadItemTables();

    // 1) Load the shared-stash chronicle tab. No stdout status here -- the
    // interesting summary lives in the "coverage" block below.
    const auto stashBytes = d2r::readFile(stashPath);
    d2r::SharedStashParser stashParser(db);
    const auto chron = stashParser.parseChronicleOnly(stashBytes);
    (void) showRunewords; // referenced below only when the WIP macro is set

    // Collect the found IDs, keyed by quality.
    std::unordered_set<std::uint32_t> foundUniqueIds, foundSetIds;
    for (const auto& e : chron.uniques)  foundUniqueIds.insert(e.itemId);
    for (const auto& e : chron.setItems) foundSetIds.insert(e.itemId);

    // 2) Optionally scan every .d2s in scanDir for items with bit-29 chronicle blobs.
    struct Bit29Item {
        std::string  source;
        d2r::ItemQuality quality;
        std::uint32_t id;
        std::string  name;
        std::string  code;
    };
    std::vector<Bit29Item> bit29Items;
    std::unordered_set<std::uint32_t> bit29UniqueIds, bit29SetIds;
    if (!scanDir.empty()) {
        auto record = [&](const std::vector<d2r::Item>& items, const std::string& source) {
            for (const auto& it : items) {
                if (!it.hasChronicleData) continue;
                Bit29Item b;
                b.source  = source;
                b.quality = it.quality;
                b.code    = it.code;
                if (it.quality == d2r::ItemQuality::Unique) {
                    b.id = it.uniqueId;
                    bit29UniqueIds.insert(it.uniqueId);
                    const auto* row = db.lookupUnique(static_cast<std::uint16_t>(it.uniqueId));
                    b.name = row ? row->index : it.itemName;
                } else if (it.quality == d2r::ItemQuality::Set) {
                    b.id = it.setItemId;
                    bit29SetIds.insert(it.setItemId);
                    const auto* row = db.lookupSetItem(static_cast<std::uint16_t>(it.setItemId));
                    b.name = row ? row->index : it.itemName;
                } else {
                    b.id = 0;
                    b.name = it.itemName.empty() ? it.code : it.itemName;
                }
                bit29Items.push_back(std::move(b));
            }
        };
        for (const auto& entry : std::filesystem::directory_iterator(scanDir)) {
            if (entry.path().extension() != ".d2s") continue;
            try {
                const auto bytes = d2r::readFile(entry.path());
                const auto ch    = d2r::parseCharacter(bytes);
                if (ch.itemsOffset == 0) continue;
                d2r::ItemParser p(db);
                const auto file = entry.path().filename().string();
                record(p.parseItems(bytes, ch.itemsOffset), file);
                if (ch.mercItemsJMOffset) {
                    record(p.parseItems(bytes, ch.mercItemsJMOffset), file + " (merc)");
                }
                if (ch.corpseJMOffset && ch.corpseItemCount == 1) {
                    record(p.parseItems(bytes, ch.corpseJMOffset + 16), file + " (corpse)");
                }
                if (ch.hasIronGolem && ch.ironGolemItemOffset) {
                    try {
                        std::vector<d2r::Item> g{p.parseSingleItem(bytes, ch.ironGolemItemOffset)};
                        record(g, file + " (iron golem)");
                    } catch (const std::exception&) {}
                }
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "  skip %s: %s\n",
                             entry.path().filename().c_str(), ex.what());
            }
        }
        // The bit-29 count is a diagnostic; it only appears when there's a
        // mismatch further below (see "not represented in shared stash").
    }

    // 3) Cross-check against the collectable catalog. Filter out section-header
    //    rows (NULL id), items excluded from the chronicle by D2 mod tables
    //    (disablechronicle=1, spawnable=0, disabled=1), and items whose base
    //    is a quest item (Khalim's Flail, Horadric Staff pieces, Hell Forge
    //    Hammer, etc. — the base armor/weapons/misc row has a non-null
    //    `quest` column).
    //
    // We build the full set of collectable IDs (rather than a scalar
    // count) so the coverage "found" number reflects only chronicled
    // items that are actually visible in the section listings below.
    // Without this, entries excluded from the chronicle by mod-txt flags
    // (e.g. Sunder Charm unique bases with disablechronicle=1 recorded
    // via the bit-29 blob, or the Warlord's Glory set items which
    // Blizzard marked disablechronicle=1) inflate the "found" or
    // "total" counts relative to what the section listings show.
    //
    // Both uniqueitems and setitems use the same three mod-txt flags
    // (spawnable / disablechronicle / disabled) so the queries here are
    // structurally identical apart from the base-code column name.
    auto loadCollectable = [&](const char* table, const char* codeCol) {
        std::unordered_set<std::uint32_t> out;
        std::string sql =
            "SELECT t.id FROM ";
        sql += table;
        sql += " t "
            "LEFT JOIN (SELECT code, quest FROM armor "
            "           UNION ALL SELECT code, quest FROM weapons "
            "           UNION ALL SELECT code, quest FROM misc) b ON b.code = t.\"";
        sql += codeCol;
        sql += "\" "
            "WHERE t.id IS NOT NULL AND t.id != '' "
            "AND CAST(t.spawnable AS INT)=1 "
            "AND (t.disablechronicle IS NULL OR t.disablechronicle != '1') "
            "AND (t.disabled IS NULL OR t.disabled != '1') "
            "AND (b.quest IS NULL OR b.quest = '')";
        auto st = db.prepare(sql);
        while (st.step()) {
            out.insert(static_cast<std::uint32_t>(st.columnInt64(0)));
        }
        return out;
    };
    const auto collectableUniqueIds = loadCollectable("uniqueitems", "code");
    const auto collectableSetIds    = loadCollectable("setitems",    "item");
    const auto totalUniques = static_cast<std::int64_t>(collectableUniqueIds.size());
    const auto totalSets    = static_cast<std::int64_t>(collectableSetIds.size());

    std::size_t combinedUniqueCount = 0;
    for (auto id : foundUniqueIds) {
        if (collectableUniqueIds.contains(id)) ++combinedUniqueCount;
    }
    std::size_t combinedSetCount = 0;
    for (auto id : foundSetIds) {
        if (collectableSetIds.contains(id)) ++combinedSetCount;
    }

    std::printf("\ncoverage (collectable items only):\n");
    std::printf("  uniques: %4zu / %4lld found (%5.1f%%)\n",
                combinedUniqueCount, static_cast<long long>(totalUniques),
                100.0 * combinedUniqueCount / std::max<std::int64_t>(1, totalUniques));
    std::printf("  sets:    %4zu / %4lld found (%5.1f%%)\n",
                combinedSetCount, static_cast<long long>(totalSets),
                100.0 * combinedSetCount / std::max<std::int64_t>(1, totalSets));

    // Notification: items whose bit-29 chronicle blob is stamped but which
    // haven't propagated into the shared-stash chronicle tab yet. This is a
    // NORMAL state -- typically an item you just picked up (often
    // unidentified) still sitting in inventory. Once it's identified,
    // equipped, or stashed, D2R copies the record into the chronicle tab.
    // Printed to stdout so it appears in the normal report flow.
    if (!scanDir.empty()) {
        std::vector<const Bit29Item*> pending;
        for (const auto& b : bit29Items) {
            const bool inChron =
                (b.quality == d2r::ItemQuality::Unique && foundUniqueIds.contains(b.id)) ||
                (b.quality == d2r::ItemQuality::Set    && foundSetIds.contains(b.id));
            if (!inChron) pending.push_back(&b);
        }
        if (!pending.empty()) {
            std::printf("\n== items pending chronicle sync (%zu) ==\n",
                        pending.size());
            std::printf("(picked-up items still in inventory; typically "
                        "unidentified and will\n reconcile once identified, "
                        "equipped, or stashed)\n");
            for (const auto* b : pending) {
                std::printf("  %s\n",
                    formatItem(b->name, lookupBaseName(db, b->code),
                               b->source).c_str());
            }
        }
    }

    // 4) List every not-yet-found unique and set, sorted alphabetically by the
    //    base item type (what the in-game chronicle displays for undiscovered
    //    items). Each item's base type comes from armor/weapons/misc.name,
    //    overlaid with item_names.en_us where an entry exists. Duplicates are
    //    expected when several items share a base (e.g. four amulets all
    //    show as "Amulet").
    //
    // Note: uniqueitems.txt keys its base-item ref as `code`, setitems.txt
    // uses `item`. We accept the column name as a parameter.
    //
    // Quest items (Khalim's Flail, Horadric Staff pieces, etc.) never appear
    // in the in-game chronicle. They're identified by a non-null base-item
    // `quest` column (the associated quest number, e.g. 10 for Horadric,
    // 17 for Khalim, 25 for Hell Forge). Filter them out via a subselect
    // that unions the three base tables and exposes the quest flag.
    const char* kBaseJoin =
        " LEFT JOIN (SELECT code, name, namestr, quest FROM armor "
        "            UNION ALL SELECT code, name, namestr, quest FROM weapons "
        "            UNION ALL SELECT code, name, namestr, quest FROM misc) b "
        "  ON b.code = t.{code_col} "
        " LEFT JOIN item_names inm ON inm.\"key\" = b.namestr ";

    // Precompute base-item tier for filtering. Armor + weapons map to one
    // of the three tiers (normal / exceptional / elite); misc bases
    // (amulets, rings, jewels, charms) map to Misc so `--tier-misc`
    // can surface them. Runewords still have no base code available and
    // remain suppressed whenever any tier filter is active.
    std::unordered_map<std::string, ItemTier> tierByCode;
    if (tf.active()) {
        auto st = db.prepare(
            "SELECT code, normcode, ubercode, ultracode FROM armor "
            "UNION ALL "
            "SELECT code, normcode, ubercode, ultracode FROM weapons");
        while (st.step()) {
            const auto code = st.columnText(0);
            const auto norm = st.columnText(1);
            const auto uber = st.columnText(2);
            const auto ultra = st.columnText(3);
            if (code.empty()) continue;
            ItemTier t = ItemTier::None;
            if      (code == norm)  t = ItemTier::Normal;
            else if (code == uber)  t = ItemTier::Exceptional;
            else if (code == ultra) t = ItemTier::Elite;
            tierByCode.emplace(code, t);
        }
        auto stMisc = db.prepare("SELECT code FROM misc");
        while (stMisc.step()) {
            const auto code = stMisc.columnText(0);
            if (!code.empty()) tierByCode.emplace(code, ItemTier::Misc);
        }
    }
    auto tierAllows = [&](std::string_view baseCode) {
        if (!tf.active()) return true;
        if (baseCode.empty()) return false;
        auto it = tierByCode.find(std::string(baseCode));
        if (it == tierByCode.end()) return false;
        return tf.allows(it->second);
    };

    // Mode word (Remaining/Discovered/All) plus a formatted query clause
    // shared by all three section headers.
    const bool showBoth  = showRemaining && showDiscovered;
    const char* modeWord = showBoth ? "All"
                                    : (showRemaining ? "Remaining" : "Discovered");
    auto queryClause = queries.empty() ? std::string()
                                       : " matching " + quoteQueries(queries);
    auto tierClause = [&]() -> std::string {
        if (!tf.active()) return {};
        static constexpr std::pair<ItemTier, const char*> kNames[] = {
            {ItemTier::Normal,      "normal"},
            {ItemTier::Exceptional, "exceptional"},
            {ItemTier::Elite,       "elite"},
            {ItemTier::Misc,        "misc"},
        };
        std::string out = "; tier: ";
        bool first = true;
        for (const auto& [t, name] : kNames) {
            if (!tf.allows(t)) continue;
            if (!first) out += ", ";
            first = false;
            out += name;
        }
        return out;
    };

    auto dumpItems = [&](const char* label, const char* table,
                         const char* code_col,
                         const std::unordered_set<std::uint32_t>& found) {
        std::string join = kBaseJoin;
        const auto placeholder = std::string("{code_col}");
        if (auto pos = join.find(placeholder); pos != std::string::npos) {
            join.replace(pos, placeholder.size(), code_col);
        }
        std::string sql =
            "SELECT t.id, "
            "       COALESCE(inm_idx.en_us, t.\"index\") AS display_name, "
            "       COALESCE(inm.en_us, b.name)         AS base_name, "
            "       t.\"";
        sql.append(code_col).append("\" AS base_code FROM ");
        sql.append(table).append(" t ").append(join).append(
            " LEFT JOIN item_names inm_idx ON inm_idx.\"key\" = t.\"index\" "
            "WHERE t.id IS NOT NULL AND t.id != '' "
            // Same three-flag mod-txt filter that the coverage
            // catalog and the set-item listing use. Both the
            // uniqueitems and setitems tables carry these columns,
            // so this listing works for either table.
            "AND CAST(t.spawnable AS INT)=1 "
            "AND (t.disablechronicle IS NULL OR t.disablechronicle != '1') "
            "AND (t.disabled IS NULL OR t.disabled != '1') "
            // Base item must not be a quest item.
            "AND (b.quest IS NULL OR b.quest = '') ");
        sql += "ORDER BY base_name COLLATE NOCASE, t.\"index\" COLLATE NOCASE";

        struct Row {
            std::uint32_t id;
            std::string   display;
            std::string   base;
            bool          have;
        };
        std::vector<Row> shown;
        std::size_t      foundCount = 0;
        std::size_t      totalCollectable = 0;

        auto st = db.prepare(sql);
        while (st.step()) {
            Row r;
            r.id      = static_cast<std::uint32_t>(st.columnInt64(0));
            r.display = st.columnText(1);
            r.base    = st.columnText(2);
            r.have    = found.contains(r.id);
            const auto baseCode = st.columnText(3);

            if (!tierAllows(baseCode))                       continue;
            if (r.have  && !showDiscovered)                  continue;
            if (!r.have && !showRemaining)                   continue;
            if (!queries.empty()) {
                const bool hit = containsAnyCI(r.display, queries)
                              || containsAnyCI(r.base,    queries);
                if (!hit) continue;
            }
            ++totalCollectable;
            if (r.have) ++foundCount;
            shown.push_back(std::move(r));
        }

        // Header. When both remaining and discovered are shown, we print
        // "F/T found" (matches the pre-change --full output); otherwise a
        // single count of the surviving rows.
        std::printf("\n=== %s %s Items%s (", modeWord, label, queryClause.c_str());
        if (showBoth) std::printf("%zu/%zu found", foundCount, shown.size());
        else          std::printf("%zu", shown.size());
        std::printf("%s) ===\n", tierClause().c_str());
        for (const auto& r : shown) {
            const auto line = formatItem(r.display, r.base);
            if (showBoth) std::printf("  [%c] %s\n", r.have ? 'X' : ' ', line.c_str());
            else          std::printf("  %s\n", line.c_str());
        }
        (void) totalCollectable;
    };

    if (showUniques) {
        dumpItems("Unique", "uniqueitems", "code", foundUniqueIds);
    }

    // Set items are grouped by parent set in every mode. In Default only
    // sets with a missing item show up; in Full every set shows; in Query
    // only sets with at least one row matching the query are printed. The
    // query matches on set_name / base_name / display_name.
    if (showSets) {
        auto st = db.prepare(
            "SELECT COALESCE(inm_set.en_us, t.\"set\")   AS set_name, "
            "       t.id, "
            "       COALESCE(inm_idx.en_us, t.\"index\") AS display_name, "
            "       COALESCE(inm_base.en_us, b.name)     AS base_name, "
            "       t.item                               AS base_code "
            "FROM setitems t "
            "LEFT JOIN (SELECT code, name, namestr, quest FROM armor "
            "           UNION ALL SELECT code, name, namestr, quest FROM weapons "
            "           UNION ALL SELECT code, name, namestr, quest FROM misc) b "
            "  ON b.code = t.item "
            "LEFT JOIN item_names inm_base ON inm_base.\"key\" = b.namestr "
            "LEFT JOIN item_names inm_idx  ON inm_idx.\"key\"  = t.\"index\" "
            "LEFT JOIN item_names inm_set  ON inm_set.\"key\"  = t.\"set\" "
            "WHERE t.id IS NOT NULL AND t.id != '' "
            "AND CAST(t.spawnable AS INT)=1 "
            "AND (t.disablechronicle IS NULL OR t.disablechronicle != '1') "
            "AND (t.disabled IS NULL OR t.disabled != '1') "
            "AND (b.quest IS NULL OR b.quest = '') "
            "ORDER BY set_name COLLATE NOCASE, base_name COLLATE NOCASE, "
            "         display_name COLLATE NOCASE");

        struct Row {
            std::string   base;
            std::uint32_t id;
            std::string   display;
            bool          have;
        };
        std::map<std::string, std::vector<Row>> shownBySet;
        std::size_t foundCount = 0;
        std::size_t totalShown = 0;

        while (st.step()) {
            const auto setName = st.columnText(0);

            Row r;
            r.id      = static_cast<std::uint32_t>(st.columnInt64(1));
            r.display = st.columnText(2);
            r.base    = st.columnText(3);
            r.have    = foundSetIds.contains(r.id);
            const auto baseCode = st.columnText(4);

            if (!tierAllows(baseCode))                       continue;
            if (r.have  && !showDiscovered)                  continue;
            if (!r.have && !showRemaining)                   continue;
            if (!queries.empty()) {
                const bool hit = containsAnyCI(setName,   queries)
                              || containsAnyCI(r.display, queries)
                              || containsAnyCI(r.base,    queries);
                if (!hit) continue;
            }
            if (r.have) ++foundCount;
            ++totalShown;
            shownBySet[setName].push_back(std::move(r));
        }

        std::printf("\n=== %s Set Items%s (", modeWord, queryClause.c_str());
        if (showBoth) std::printf("%zu/%zu found", foundCount, totalShown);
        else          std::printf("%zu", totalShown);
        std::printf("%s) ===\n", tierClause().c_str());
        for (const auto& [setName, items] : shownBySet) {
            std::printf("  %s\n", setName.c_str());
            for (const auto& r : items) {
                const auto line = formatItem(r.display, r.base);
                if (showBoth) std::printf("      [%c] %s\n",
                                          r.have ? 'X' : ' ', line.c_str());
                else          std::printf("      %s\n", line.c_str());
            }
        }
    }

#if D2R_ENABLE_RUNEWORDS_WIP
    // Runewords are a work-in-progress: the chronicle stores a numeric itemId
    // whose meaning comes from D2R's item-runes.json string-table (not in our
    // snapshot), so we cannot map individual entries to per-runeword state.
    // As a consequence:
    //   - --tier-* filters suppress the section entirely (no base to check).
    //   - --remaining/--discovered alone can't be honoured; we skip the
    //     section unless the caller wants both (the default).
    if (showRunewords && !tf.active() && showRemaining && showDiscovered) {
        auto st = db.prepare(
            "SELECT COALESCE(inm.en_us, rune_name) AS display_name "
            "FROM runes "
            "LEFT JOIN item_names inm ON inm.\"key\" = runes.rune_name "
            "WHERE complete = '1' AND rune_name IS NOT NULL AND rune_name != '' "
            "ORDER BY display_name COLLATE NOCASE");

        std::vector<std::string> allRunewords;
        while (st.step()) allRunewords.push_back(st.columnText(0));

        std::vector<std::string> shown;
        for (const auto& rw : allRunewords) {
            if (!queries.empty() && !containsAnyCI(rw, queries)) continue;
            shown.push_back(rw);
        }

        std::printf("\n=== All Runewords%s (%zu of %zu total; %zu chronicled) ===\n",
                    queryClause.c_str(),
                    shown.size(), allRunewords.size(), chron.runewords.size());
        std::printf("(per-runeword found/missing mapping requires D2R's\n"
                    " item-runes.json string-table extract which is not\n"
                    " currently loaded; per-item state shown as [?])\n");
        for (const auto& rw : shown) {
            std::printf("  [?] %s\n", formatItem(rw, "").c_str());
        }
    }
#else
    (void) showRunewords;
#endif
    return 0;
}

// ---- reconcile --------------------------------------------------------------

namespace {

struct OwnedItem {
    std::uint32_t id       = 0;
    std::string   name;      // resolved from uniqueitems.index / setitems.index
    std::string   code;      // 3-char item code
    std::string   location;  // human-readable source ("Kai.d2s", "stash tab 3", ...)
};

// Walk one item list, recording every unique or set item into the matching map.
void collectOwned(const std::vector<d2r::Item>& items,
                  const std::string& source,
                  const d2r::RefDb& db,
                  std::unordered_map<std::uint32_t, std::vector<OwnedItem>>& uniques,
                  std::unordered_map<std::uint32_t, std::vector<OwnedItem>>& sets) {
    auto record = [&](const d2r::Item& it) {
        if (it.quality == d2r::ItemQuality::Unique) {
            OwnedItem o{ it.uniqueId, it.itemName, it.code, source };
            if (o.name.empty()) {
                if (const auto* r = db.lookupUnique(static_cast<std::uint16_t>(it.uniqueId))) {
                    o.name = r->index;
                }
            }
            uniques[it.uniqueId].push_back(std::move(o));
        } else if (it.quality == d2r::ItemQuality::Set) {
            OwnedItem o{ it.setItemId, it.itemName, it.code, source };
            if (o.name.empty()) {
                if (const auto* r = db.lookupSetItem(static_cast<std::uint16_t>(it.setItemId))) {
                    o.name = r->index;
                }
            }
            sets[it.setItemId].push_back(std::move(o));
        }
    };
    for (const auto& it : items) {
        record(it);
        for (const auto& s : it.socketedItems) record(s);
    }
}

std::string lookupUniqueName(const d2r::RefDb& db, std::uint32_t id) {
    if (const auto* r = db.lookupUnique(static_cast<std::uint16_t>(id))) return r->index;
    return "unique#" + std::to_string(id);
}
std::string lookupSetName(const d2r::RefDb& db, std::uint32_t id) {
    if (const auto* r = db.lookupSetItem(static_cast<std::uint16_t>(id))) return r->index;
    return "set#" + std::to_string(id);
}

} // namespace

int cmdReconcile(const std::filesystem::path& exePath,
                 const std::string& stashPath,
                 const std::string& scanDir) {
    const auto dbPath = d2r::findReferenceDb(exePath);
    if (!dbPath) { std::fprintf(stderr, "error: reference DB not found\n"); return 1; }
    d2r::RefDb db(*dbPath);
    db.loadItemTables();

    // Owned items across stash tabs + every scanned character.
    std::unordered_map<std::uint32_t, std::vector<OwnedItem>> ownedUniques;
    std::unordered_map<std::uint32_t, std::vector<OwnedItem>> ownedSets;

    // 1) Shared stash: parse storage tabs (0-5) and the chronicle tab.
    d2r::SharedStash stash;
    d2r::ChronicleTab chron;
    {
        const auto bytes = d2r::readFile(stashPath);
        d2r::SharedStashParser sp(db);
        stash = sp.parse(bytes);
        if (stash.chronicle) chron = *stash.chronicle;
    }
    for (std::size_t i = 0; i < stash.tabs.size(); ++i) {
        const std::string src = "stash tab " + std::to_string(i + 1);
        collectOwned(stash.tabs[i].items, src, db, ownedUniques, ownedSets);
    }

    // 2) Every .d2s in scanDir (character items — mercenary/corpse/golem TBD).
    std::size_t chars = 0;
    if (!scanDir.empty()) {
        for (const auto& entry : std::filesystem::directory_iterator(scanDir)) {
            if (entry.path().extension() != ".d2s") continue;
            ++chars;
            try {
                const auto bytes = d2r::readFile(entry.path());
                const auto ch    = d2r::parseCharacter(bytes);
                if (ch.itemsOffset == 0) continue;
                d2r::ItemParser p(db);
                const auto file = entry.path().filename().string();
                collectOwned(p.parseItems(bytes, ch.itemsOffset),
                             file, db, ownedUniques, ownedSets);
                if (ch.mercItemsJMOffset) {
                    collectOwned(p.parseItems(bytes, ch.mercItemsJMOffset),
                                 file + " (merc)", db, ownedUniques, ownedSets);
                }
                if (ch.corpseJMOffset && ch.corpseItemCount == 1) {
                    // Java: parseItems(buffer, deadBodyIndex + 16, ...);
                    // the actual corpse item list JM sits 16 bytes past the flag.
                    collectOwned(p.parseItems(bytes, ch.corpseJMOffset + 16),
                                 file + " (corpse)", db, ownedUniques, ownedSets);
                }
                if (ch.hasIronGolem && ch.ironGolemItemOffset) {
                    try {
                        std::vector<d2r::Item> golem{
                            p.parseSingleItem(bytes, ch.ironGolemItemOffset)};
                        collectOwned(golem, file + " (iron golem)",
                                     db, ownedUniques, ownedSets);
                    } catch (const std::exception&) { /* tolerate golem parse issues */ }
                }
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "  skip %s: %s\n",
                             entry.path().filename().c_str(), ex.what());
            }
        }
    }

    // Chronicle IDs (both sources: stash tab 7 + bit-29 blobs in scanned items).
    std::unordered_set<std::uint32_t> chronUnique, chronSet;
    for (const auto& e : chron.uniques)  chronUnique.insert(e.itemId);
    for (const auto& e : chron.setItems) chronSet.insert(e.itemId);

    // Header summary.
    std::printf("stash:  %s (%zu storage tabs, chronicle %s)\n",
                stashPath.c_str(), stash.tabs.size(),
                stash.chronicle ? "present" : "missing");
    std::printf("scan:   %s (%zu character files)\n",
                scanDir.empty() ? "(none)" : scanDir.c_str(), chars);
    std::printf("owned:  %zu distinct unique IDs, %zu distinct set IDs\n",
                ownedUniques.size(), ownedSets.size());
    std::printf("chron:  %zu unique IDs, %zu set IDs\n",
                chronUnique.size(), chronSet.size());

    // ---- Discrepancy A: owned but MISSING from chronicle -------------------
    std::printf("\n== owned but NOT in chronicle ==\n");
    std::printf("(items you have that never made it into your chronicle "
                "— e.g. picked up before a chronicle reset)\n");

    // Sunder-charm rule: owning a Renewed charm is satisfied by its Latent
    // pair in the chronicle (and vice versa). See include/d2r/SunderCharms.hpp.
    auto uniqueChronicled = [&](std::uint32_t id) {
        if (chronUnique.contains(id)) return true;
        const auto pair = d2r::sunderPairedId(id);
        return pair != 0 && chronUnique.contains(pair);
    };
    auto setChronicled = [&](std::uint32_t id) {
        return chronSet.contains(id);
    };
    auto uniqueOwned = [&](std::uint32_t id) {
        if (ownedUniques.contains(id)) return true;
        const auto pair = d2r::sunderPairedId(id);
        return pair != 0 && ownedUniques.contains(pair);
    };
    auto setOwned = [&](std::uint32_t id) {
        return ownedSets.contains(id);
    };

    std::size_t missingA = 0;
    std::map<std::uint32_t, std::vector<OwnedItem>*> sortedU;
    for (auto& [id, list] : ownedUniques) sortedU[id] = &list;
    for (auto& [id, listp] : sortedU) {
        if (uniqueChronicled(id)) continue;
        for (const auto& o : *listp) {
            std::printf("  %s\n",
                formatItem(o.name, lookupBaseName(db, o.code), o.location).c_str());
        }
        ++missingA;
    }
    std::map<std::uint32_t, std::vector<OwnedItem>*> sortedS;
    for (auto& [id, list] : ownedSets) sortedS[id] = &list;
    for (auto& [id, listp] : sortedS) {
        if (setChronicled(id)) continue;
        for (const auto& o : *listp) {
            std::printf("  %s\n",
                formatItem(o.name, lookupBaseName(db, o.code), o.location).c_str());
        }
        ++missingA;
    }
    if (missingA == 0) std::printf("  (none)\n");

    // ---- Discrepancy B: in chronicle but not currently owned ---------------
    std::printf("\n== in chronicle but NOT currently owned ==\n");
    std::printf("(items your account has seen but that no longer live in any "
                "of the scanned .d2s/.d2i files — e.g. sold or dropped)\n");

    auto formatTimestamp = [](std::uint32_t minutes) {
        const std::time_t secs = static_cast<std::time_t>(minutes) * 60;
        char buf[32] = {};
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::gmtime(&secs));
        return std::string(buf);
    };

    std::size_t missingB = 0;
    std::map<std::uint32_t, const d2r::ChronicleEntry*> sortedCU;
    for (const auto& e : chron.uniques)  sortedCU[e.itemId] = &e;
    for (const auto& [id, ep] : sortedCU) {
        if (uniqueOwned(id)) continue;
        // Item type is unknown here (the chronicle entry has only an id);
        // resolving it would require joining uniqueitems -> armor/weapons/misc.
        // Left blank for now -- captured under item-display embellishments in
        // TODO.md.
        std::printf("  %s  (chronicled %s UTC, mon=%u)\n",
                    formatItem(lookupUniqueName(db, id), "").c_str(),
                    formatTimestamp(ep->timestampMinutes).c_str(),
                    ep->monsterId);
        ++missingB;
    }
    std::map<std::uint32_t, const d2r::ChronicleEntry*> sortedCS;
    for (const auto& e : chron.setItems) sortedCS[e.itemId] = &e;
    for (const auto& [id, ep] : sortedCS) {
        if (setOwned(id)) continue;
        std::printf("  %s  (chronicled %s UTC, mon=%u)\n",
                    formatItem(lookupSetName(db, id), "").c_str(),
                    formatTimestamp(ep->timestampMinutes).c_str(),
                    ep->monsterId);
        ++missingB;
    }
    if (missingB == 0) std::printf("  (none)\n");

    std::printf("\nsummary: %zu owned-not-in-chronicle, %zu chronicled-not-owned\n",
                missingA, missingB);
    return 0;
}
#endif


} // namespace

namespace {

// Resolve a bare character name against a save-directory. Accepts a full
// filename (with or without .d2s) or a plain name; the returned path is
// canonicalised for readable error messages.
std::filesystem::path resolveCharacter(const std::filesystem::path& base,
                                       std::string_view name) {
    std::filesystem::path p{std::string(name)};
    if (p.extension() != ".d2s") p += ".d2s";
    if (!p.is_absolute() && !p.has_parent_path()) {
        p = base / p;
    }
    return p;
}

// Locate the shared-stash file inside a save directory. Prefers the RotW
// "Modern*" filenames, then falls back to the classic ones. Throws if none.
std::filesystem::path findStashFile(const std::filesystem::path& base) {
    static constexpr const char* kCandidates[] = {
        "ModernSharedStashSoftCoreV2.d2i",
        "ModernSharedStashHardCoreV2.d2i",
        "SharedStashSoftCoreV2.d2i",
        "SharedStashHardCoreV2.d2i",
    };
    for (const char* name : kCandidates) {
        auto p = base / name;
        std::error_code ec;
        if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) {
            return p;
        }
    }
    throw std::runtime_error(
        "no shared-stash .d2i file (ModernSharedStash* or SharedStash*) found in " +
        base.string());
}

} // namespace

int main(int argc, char** argv) {
    // Pull global flags out of argv, leaving `positional` for command + args.
    std::vector<std::string_view> positional;
    std::filesystem::path savePath;
    std::string_view referenceDbOverride;
    bool watchRequested = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--path" && i + 1 < argc) {
            savePath = argv[++i];
        } else if (a == "--reference-db" && i + 1 < argc) {
            referenceDbOverride = argv[++i];
        } else if (a == "--watch") {
            watchRequested = true;
        } else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            positional.push_back(a);
        }
    }
    if (positional.empty()) return usage(argv[0]);
    const auto cmd = positional[0];

    auto requirePath = [&]() {
        if (savePath.empty()) {
            std::fprintf(stderr,
                "error: '%.*s' requires --path <save-dir>\n",
                static_cast<int>(cmd.size()), cmd.data());
            std::exit(2);
        }
        std::error_code ec;
        if (!std::filesystem::is_directory(savePath, ec)) {
            std::fprintf(stderr,
                "error: --path %s is not a directory\n", savePath.c_str());
            std::exit(2);
        }
    };
    auto requireArgs = [&](std::size_t need) {
        if (positional.size() - 1 != need) {
            std::fprintf(stderr,
                "error: '%.*s' expects %zu argument%s (got %zu)\n",
                static_cast<int>(cmd.size()), cmd.data(),
                need, need == 1 ? "" : "s", positional.size() - 1);
            std::exit(2);
        }
    };

    // Build a re-runnable callable for the subcommand so --watch can invoke
    // it repeatedly. If the command isn't watch-compatible we bail early.
    std::function<int()> runCommand;
    bool watchCompatible = true;

    if (cmd == "verify")   { requirePath(); requireArgs(1);
        runCommand = [p = resolveCharacter(savePath, positional[1]).string()]{ return cmdVerify(p); }; }
    else if (cmd == "checksum") { requirePath(); requireArgs(1); watchCompatible = false;
        runCommand = [p = resolveCharacter(savePath, positional[1]).string()]{ return cmdChecksum(p); }; }
    else if (cmd == "rename")   { requirePath(); requireArgs(2); watchCompatible = false;
        runCommand = [p = resolveCharacter(savePath, positional[1]).string(),
                      n = std::string(positional[2])]{ return cmdRename(p, n); }; }
    else if (cmd == "set-seed") { requirePath(); requireArgs(2); watchCompatible = false;
        runCommand = [p = resolveCharacter(savePath, positional[1]).string(),
                      s = std::string(positional[2])]{ return cmdSetSeed(p, s); }; }
    else if (cmd == "dump")     { requirePath(); requireArgs(1);
        runCommand = [p = resolveCharacter(savePath, positional[1]).string()]{ return cmdDump(p); }; }
#if D2R_HAVE_SQLITE
    else if (cmd == "items")     { requirePath();
        // items selectors:
        //   --character NAME (repeatable) or --shared-stash: restrict scope.
        //     Absence of both = every source (all .d2s + all stash tabs).
        //   --<quality> flags (--unique, --set, --normal, --magic, --rare,
        //     --craft, --superior, --inferior): restrict by item quality.
        //     Absence = all qualities.
        //   Trailing positionals: case-insensitive substring queries. Each
        //     candidate item matches if any query string appears in its
        //     name OR its base type.
        ItemScope scope;
        QualityFilter qf;
        TierFilter tf;
        std::vector<std::string> queries;
        for (std::size_t i = 1; i < positional.size(); ++i) {
            const auto a = positional[i];
            if (a == "--character") {
                if (i + 1 >= positional.size()) {
                    std::fprintf(stderr,
                        "error: '--character' requires a character name\n");
                    return 2;
                }
                const auto name = positional[++i];
                scope.characters.emplace(name);
                scope.anyExplicit = true;
            }
            else if (a == "--shared-stash") {
                scope.includeStash = true;
                scope.anyExplicit  = true;
            }
            else if (tryConsumeQualityFlag(a, qf)) {
                // Enabled inside the helper.
            }
            else if (tryConsumeTierFlag(a, tf)) {
                // Enabled inside the helper.
            }
            else if (!a.empty() && a[0] == '-') {
                std::fprintf(stderr,
                    "error: 'items' does not accept flag '%.*s' (allowed: "
                    "--character NAME, --shared-stash, --inferior, --normal, "
                    "--superior, --magic, --set, --rare, --unique, --craft, "
                    "--tier-normal, --tier-exceptional, --tier-elite, --tier-misc)\n",
                    static_cast<int>(a.size()), a.data());
                return 2;
            }
            else {
                queries.emplace_back(a);
            }
        }
        runCommand = [exe   = std::string(argv[0]),
                      sp    = savePath.string(),
                      scope = std::move(scope),
                      qf, tf,
                      queries = std::move(queries)]{
            std::string stashPath;
            try { stashPath = findStashFile(sp).string(); }
            catch (const std::exception&) {
                // No stash file is fine when the user has scoped away from it
                // via --character (or when they simply have a bare save dir).
            }
            return cmdItems(exe, stashPath, sp, scope, qf, tf, queries);
        };
    }
    else if (cmd == "chronicle") { requirePath();
        // Chronicle selectors:
        //   --uniques / --sets / --runewords  category filters (any subset)
        //   --remaining                       show items NOT yet chronicled
        //   --discovered                      show items already chronicled
        //   --tier-normal / -exceptional / -elite  filter by base-item tier
        //   --query STR STR ...               substring filter (last flag;
        //                                     consumes every remaining arg)
        // Neither --remaining nor --discovered => show both (the entire list).
        bool showU = false, showS = false, showR = false;
        bool remaining = false, discovered = false, sawQuery = false;
        TierFilter tf;
        std::vector<std::string> queries;
        for (std::size_t i = 1; i < positional.size(); ++i) {
            const auto a = positional[i];
            if (sawQuery) { queries.emplace_back(a); continue; }
            if      (a == "--uniques")     showU = true;
            else if (a == "--sets")        showS = true;
#if D2R_ENABLE_RUNEWORDS_WIP
            else if (a == "--runewords")   showR = true;
#endif
            else if (a == "--remaining")   remaining = true;
            else if (a == "--discovered")  discovered = true;
            else if (tryConsumeTierFlag(a, tf)) {
                // Enabled inside the helper.
            }
            else if (a == "--query")       sawQuery = true;
            else {
                std::fprintf(stderr,
                    "error: 'chronicle' does not accept '%.*s' (allowed: "
                    "--uniques, --sets"
#if D2R_ENABLE_RUNEWORDS_WIP
                    ", --runewords"
#endif
                    ", --remaining, --discovered, "
                    "--tier-normal, --tier-exceptional, --tier-elite, --tier-misc, --query)\n",
                    static_cast<int>(a.size()), a.data());
                return 2;
            }
        }
        if (sawQuery && queries.empty()) {
            std::fprintf(stderr,
                "error: '--query' requires at least one search string\n");
            return 2;
        }
        // Absence of both --remaining and --discovered means "show the
        // entire list" -- same output as passing both explicitly.
        if (!remaining && !discovered) { remaining = discovered = true; }

        if (!showU && !showS && !showR) {
            showU = showS = true;
#if D2R_ENABLE_RUNEWORDS_WIP
            showR = true;
#endif
        }
        runCommand = [exe = std::string(argv[0]), sp = savePath.string(),
                      showU, showS, showR, remaining, discovered, tf, queries]{
            return cmdChronicle(exe, findStashFile(sp).string(), sp,
                                showU, showS, showR,
                                remaining, discovered, tf, queries);
        };
    }
    else if (cmd == "reconcile") { requirePath(); requireArgs(0);
        runCommand = [exe = std::string(argv[0]), sp = savePath.string()]{
            return cmdReconcile(exe, findStashFile(sp).string(), sp);
        }; }
    else if (cmd == "db-info") {
        watchCompatible = false;
        runCommand = [exe = std::string(argv[0]), r = std::string(referenceDbOverride)]{
            return cmdDbInfo(exe, r);
        };
    }
#endif
    else {
        std::fprintf(stderr, "error: unknown command '%.*s'\n\n",
                     static_cast<int>(cmd.size()), cmd.data());
        return usage(argv[0]);
    }

    // Non-watch path: run once and exit.
    if (!watchRequested) {
        try { return runCommand(); }
        catch (const std::exception& ex) {
            std::fprintf(stderr, "error: %s\n", ex.what());
            return 1;
        }
    }

#if !D2R_HAVE_INOTIFY
    std::fprintf(stderr,
        "error: --watch is unavailable on this build (no inotify support was\n"
        "       detected at CMake configure time).\n");
    return 2;
#else
    if (!watchCompatible) {
        std::fprintf(stderr,
            "error: '%.*s' modifies save files; combining it with --watch\n"
            "       would trigger the watcher on its own write and loop.\n",
            static_cast<int>(cmd.size()), cmd.data());
        return 2;
    }

    // --watch loop. SIGINT interrupts the blocking read() inside the watcher
    // so the whole thing exits gracefully on Ctrl-C.
    struct sigaction sa{};
    sa.sa_handler = [](int){};   // Empty handler: just wake up read() with EINTR.
    sa.sa_flags   = 0;           // Deliberately no SA_RESTART.
    ::sigaction(SIGINT,  &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    // Resolve the running executable so we can also detect self-relinks and
    // hot-reload by execv'ing the fresh binary back into place.
    std::filesystem::path exePath;
    {
        char link[4096];
        const ssize_t n = ::readlink("/proc/self/exe", link, sizeof(link) - 1);
        if (n > 0) { link[n] = '\0'; exePath = link; }
    }

    std::fprintf(stderr,
        "[watch] %s in %s (Ctrl-C to exit%s)\n",
        std::string(cmd).c_str(), savePath.c_str(),
        exePath.empty() ? "" : "; will re-exec on self-rebuild");
    try {
        (void) runCommand();  // Initial run.

        d2r::DirectoryWatcher watcher(savePath);
        if (!exePath.empty()) {
            (void) watcher.alsoWatchExecutable(exePath);
        }
        while (auto trig = watcher.waitForChange()) {
            char stamp[32] = {};
            const std::time_t now = std::time(nullptr);
            std::strftime(stamp, sizeof(stamp), "%H:%M:%S", std::localtime(&now));

            if (*trig == d2r::DirectoryWatcher::Trigger::Executable) {
                std::fprintf(stderr,
                    "\n[watch] %s -- binary changed at %s, re-executing\n",
                    stamp, exePath.c_str());
                // Give the linker a moment in case cmake is still mid-link.
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
                (void) ::execv(exePath.c_str(), argv);
                // execv returned -> the new binary wasn't ready. Report and
                // fall back to the normal re-run so we don't lose the loop.
                std::fprintf(stderr,
                    "[watch] execv %s failed (%s); continuing with the old "
                    "binary\n", exePath.c_str(), std::strerror(errno));
            }

            std::fprintf(stderr, "\n[watch] %s -- change detected, re-running '%.*s'\n"
                                 "----------------------------------------------------\n",
                         stamp, static_cast<int>(cmd.size()), cmd.data());
            try { (void) runCommand(); }
            catch (const std::exception& ex) {
                std::fprintf(stderr, "[watch] error: %s\n", ex.what());
            }
        }
        std::fprintf(stderr, "\n[watch] exiting\n");
        return 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
#endif
}