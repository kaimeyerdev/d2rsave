// d2rsave: D2R save-file CLI.
//
// Phase 3 subcommands (no external dependencies):
//   verify   <file>              — print stored + computed checksum, name, seed.
//   rename   <file> <name>       — overwrite the 16-byte name field, recompute checksum.
//   set-seed <file> <u32>        — overwrite the map seed, recompute checksum.
//   set-difficulty <file> <lvl>  — mark difficulty (normal/nightmare/hell) active, recompute checksum.
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

#if D2R_HAVE_SQLITE && D2R_HAVE_INOTIFY
#include "d2r/BackupDb.hpp"
#include "d2r/BackupScheduler.hpp"
#include "d2r/Paths.hpp"
#include "d2r/Recovery.hpp"
#endif

#if D2R_HAVE_DASHBOARD
#include "d2r/Dashboard.hpp"
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
        "  set-difficulty <name> <lvl>  Mark <lvl> (1|2|3 or normal|nightmare|hell)\n"
        "                               as the character's active difficulty and\n"
        "                               recompute checksum. Preserves act progress\n"
        "                               on all three difficulties; only flips the\n"
        "                               'active' bit onto <lvl>.\n"
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
#if D2R_HAVE_DASHBOARD
        "Interactive mode (mutually exclusive with --watch and the modifying\n"
        "commands rename/set-seed/set-difficulty/checksum):\n"
        "  dashboard [--print [--width W] [--height H]]\n"
        "                               Launch the interactive TUI dashboard.\n"
        "                               Top row (configurable): Character (name,\n"
        "                               class, level, session XP), Uber (keys +\n"
        "                               torch + optional uber drops + Ancients +\n"
        "                               optional torch-by-class), Terror Zone\n"
        "                               (Worldstone shards), and Session Loot\n"
        "                               (new uniques / sets / runes since the\n"
        "                               session anchor). Bottom row: configurable\n"
        "                               Chronicle / Inventory / Reconcile / Backups\n"
        "                               panes. Live-refreshes on save-file changes\n"
        "                               when inotify support is compiled in;\n"
        "                               otherwise press `r`. Press `?` inside the\n"
        "                               TUI for keybindings.\n"
        "                               --print renders the current layout once as\n"
        "                               ANSI to stdout and exits (no watcher, no\n"
        "                               DB writes). Default size 200x60; override\n"
        "                               with --width / --height.\n"
        "\n"
#endif
        "Diagnostic commands:\n"
        "  db-info                      Show reference DB path and per-table row counts.\n"
#if D2R_HAVE_INOTIFY
        "\n"
        "Backup commands (uses $XDG_DATA_HOME/d2rsave/backups.sqlite):\n"
        "  backups <sub> [flags...]     summary | list | sessions | show | recover | prune\n"
        "                               Pass 'backups --help' for the full flag list.\n"
#endif
        ,
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

// Parse a difficulty specifier: "1"/"normal", "2"/"nightmare", "3"/"hell"
// (case-insensitive). Returns 0/1/2 on success, std::nullopt otherwise.
std::optional<std::uint8_t> parseDifficulty(std::string_view s) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower == "1" || lower == "normal")    return std::uint8_t{0};
    if (lower == "2" || lower == "nightmare") return std::uint8_t{1};
    if (lower == "3" || lower == "hell")      return std::uint8_t{2};
    return std::nullopt;
}

const char* difficultyLabel(std::uint8_t d) {
    switch (d) {
        case 0: return "Normal";
        case 1: return "Nightmare";
        case 2: return "Hell";
        default: return "?";
    }
}

int cmdSetDifficulty(const std::string& path, std::string_view diffStr) {
    const auto target = parseDifficulty(diffStr);
    if (!target) {
        std::fprintf(stderr,
            "error: difficulty must be one of: 1, 2, 3, normal, nightmare, hell\n");
        return 1;
    }
    auto bytes = d2r::readFile(path);
    if (!d2r::hasValidMagic(bytes)) {
        std::fprintf(stderr, "error: %s: invalid magic\n", path.c_str());
        return 1;
    }
    const auto oldActive = d2r::readActiveDifficulty(bytes);
    d2r::writeActiveDifficulty(bytes, *target);
    const auto newChecksum = d2r::recomputeAndWriteChecksum(bytes);
    d2r::writeFileAtomic(path, bytes);
    std::printf("difficulty: %s -> %s\n",
                oldActive ? difficultyLabel(*oldActive) : "(none)",
                difficultyLabel(*target));
    std::printf("checksum:   0x%08X\n", newChecksum);
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

#if D2R_HAVE_INOTIFY

// ---- backups subcommand ---------------------------------------------------
//
// Owns its own arg-parsing loop since the shape doesn't fit the top-level
// dispatcher's positional-only convention: `list --limit N --state K`,
// `show <file> [--at ISO]`, `recover <file> --at ISO [--to DIR]`, etc.
// All variants operate on the shared XDG backups.sqlite; recovery uses
// `primarySavePath` as the default destination when `--to` is omitted.

namespace {

const char* stateName(d2r::BackupDb::State s) {
    switch (s) {
        case d2r::BackupDb::State::Deleted:     return "deleted";
        case d2r::BackupDb::State::SaveAndExit: return "save_and_exit";
        case d2r::BackupDb::State::Autosave:    return "autosave";
        case d2r::BackupDb::State::Startup:     return "startup";
        case d2r::BackupDb::State::Other:       return "other";
    }
    return "?";
}

std::string formatBackupDate(std::int64_t unix) {
    if (unix <= 0) return "-";
    const std::time_t t = static_cast<std::time_t>(unix);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::int64_t parseIsoUtc(std::string_view iso) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (std::sscanf(std::string(iso).c_str(),
                    "%d-%d-%dT%d:%d:%d",
                    &y, &mo, &d, &h, &mi, &se) < 3) {
        throw std::runtime_error(
            "backups: --at expects YYYY-MM-DDTHH:MM:SS (UTC); got '" +
            std::string(iso) + "'");
    }
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
    tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = se;
    return static_cast<std::int64_t>(timegm(&tm));
}

void printBackupsUsage() {
    std::fprintf(stderr,
        "Usage: d2rsave backups <subcommand> [flags]\n"
        "\n"
        "Subcommands:\n"
        "  summary\n"
        "      One line per known file: last-save time, state, session and\n"
        "      backup counts. Uses the shared XDG backups.sqlite.\n"
        "\n"
        "  list [--filename NAME] [--limit N] [--state K]\n"
        "      Reverse-chronological rows. --state filters to one of\n"
        "      0(deleted) 1(save_and_exit) 2(autosave) 3(startup) 4(other).\n"
        "\n"
        "  sessions <filename> [--limit N]\n"
        "      Print one line per session for a .d2s file. A session ends\n"
        "      at a save_and_exit row; the current in-progress session (no\n"
        "      closing save_and_exit yet) is labelled 'in progress'.\n"
        "\n"
        "  show <filename> --at YYYY-MM-DDTHH:MM:SSZ\n"
        "      Print the row that was current at the given UTC moment.\n"
        "\n"
        "  recover <filename> --at YYYY-MM-DDTHH:MM:SSZ [--to DIR]\n"
        "                     [--no-pre-snapshot]\n"
        "      Restore that row's bytes. --to defaults to the --path save\n"
        "      dir; a pre-recovery snapshot of the destination is taken\n"
        "      unless --no-pre-snapshot is passed.\n"
        "\n"
        "  prune [--days N] [--sessions M]\n"
        "      Enforce retention now. Defaults: 30 days, 100 sessions per\n"
        "      character.\n"
        "\n"
        "  snapshot\n"
        "      Force a startup-style sweep of --path <saves-dir>. Useful\n"
        "      for seeding or refreshing the backup DB outside the TUI.\n"
        "\n"
        "  diff <filename> [--limit N] [--db PATH] [--max-diffs K]\n"
        "      Byte-diff the N (default 3) most-recent rows for <filename>,\n"
        "      labelling each differing offset by the region it lives in.\n"
        "      Per-byte rows are printed only for currently-undecoded\n"
        "      header/section regions; decoded regions are only tallied.\n"
        "\n"
        "  find-item {--character NAME | --all-characters} [--shared-stash]\n"
        "           {--name PATTERN | --code CODE}\n"
        "           [--db PATH] [--limit-rows N] [--limit-matches K]\n"
        "      Scan the N (default 500) most-recent rows of each selected\n"
        "      character (and, with --shared-stash, .d2i) for items whose\n"
        "      unique/set/base name matches PATTERN (case-insensitive\n"
        "      substring) or whose item code equals CODE. Prints every row\n"
        "      where a match exists so the user can pick one to restore.\n");
}

int cmdBackupsSummary() {
    d2r::BackupDb db(d2r::backupDbPath());
    const auto sums = db.summariseFiles();
    if (sums.empty()) {
        std::printf("(no backups yet)\n");
        return 0;
    }
    std::printf("%-40s  %-20s  %-14s  %8s  %8s\n",
                "filename", "last-save", "state", "sessions", "backups");
    for (const auto& fs : sums) {
        std::printf("%-40s  %-20s  %-14s  %8lld  %8lld\n",
                    fs.filename.c_str(),
                    formatBackupDate(fs.lastDate).c_str(),
                    stateName(fs.lastState),
                    static_cast<long long>(fs.sessionCount),
                    static_cast<long long>(fs.backupCount));
    }
    return 0;
}

int cmdBackupsList(std::vector<std::string_view> args) {
    std::string filenameFilter;
    std::int64_t limit = 50;
    int stateFilter = -1;
    for (std::size_t i = 2; i < args.size(); ++i) {
        const auto a = args[i];
        auto need = [&](const char* flag) {
            if (i + 1 >= args.size()) {
                throw std::runtime_error(std::string("backups list: ") + flag + " expects a value");
            }
            return args[++i];
        };
        if      (a == "--filename") filenameFilter = need("--filename");
        else if (a == "--limit")    limit = std::atoll(std::string(need("--limit")).c_str());
        else if (a == "--state")    stateFilter = std::atoi(std::string(need("--state")).c_str());
        else {
            throw std::runtime_error("backups list: unknown flag '" + std::string(a) + "'");
        }
    }
    d2r::BackupDb db(d2r::backupDbPath());
    if (!filenameFilter.empty()) {
        const auto hist = db.historyFor(filenameFilter, static_cast<std::size_t>(limit));
        for (const auto& r : hist) {
            if (stateFilter >= 0 && static_cast<int>(r.state) != stateFilter) continue;
            std::printf("%-20s  %-14s  %8lld bytes\n",
                        formatBackupDate(r.date).c_str(),
                        stateName(r.state),
                        static_cast<long long>(r.sizeBytes));
        }
        return 0;
    }
    // No filter: walk summaries and expand each. Simpler than adding a
    // whole-DB accessor to BackupDb for this one call site.
    for (const auto& fs : db.summariseFiles()) {
        std::printf("== %s ==\n", fs.filename.c_str());
        const auto hist = db.historyFor(fs.filename, static_cast<std::size_t>(limit));
        for (const auto& r : hist) {
            if (stateFilter >= 0 && static_cast<int>(r.state) != stateFilter) continue;
            std::printf("  %-20s  %-14s  %8lld bytes\n",
                        formatBackupDate(r.date).c_str(),
                        stateName(r.state),
                        static_cast<long long>(r.sizeBytes));
        }
    }
    return 0;
}

int cmdBackupsSessions(std::vector<std::string_view> args) {
    if (args.size() < 3) {
        std::fprintf(stderr, "error: backups sessions: expected <filename>\n");
        return 2;
    }
    const std::string filename(args[2]);
    std::int64_t limit = 100;
    for (std::size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--limit" && i + 1 < args.size()) {
            limit = std::atoll(std::string(args[++i]).c_str());
        } else {
            throw std::runtime_error("backups sessions: unknown flag '" + std::string(args[i]) + "'");
        }
    }
    d2r::BackupDb db(d2r::backupDbPath());
    // Load enough history to reconstruct sessions. Ask for many rows;
    // for a .d2s that's fine.
    const auto hist = db.historyFor(filename, 10000);
    if (hist.empty()) {
        std::printf("(no backups for %s)\n", filename.c_str());
        return 0;
    }
    // hist is date-DESC. Walk backwards (oldest -> newest) so a session's
    // "start" is naturally the first row in that session's range.
    struct Session {
        std::int64_t startDate = 0, endDate = 0;
        std::int64_t rowCount  = 0;
        bool inProgress        = false;
    };
    std::vector<Session> sessions;
    Session cur;
    cur.startDate = hist.back().date;
    for (auto it = hist.rbegin(); it != hist.rend(); ++it) {
        cur.rowCount += 1;
        cur.endDate = it->date;
        if (it->state == d2r::BackupDb::State::SaveAndExit) {
            sessions.push_back(cur);
            cur = Session{};
            // Next session (if any) begins here.
            auto next = it + 1;
            if (next != hist.rend()) cur.startDate = next->date;
        }
    }
    if (cur.rowCount > 0) {
        cur.inProgress = true;
        sessions.push_back(cur);
    }
    // Sessions are oldest-first; reverse to show newest-first, matching
    // the summary view.
    std::reverse(sessions.begin(), sessions.end());
    std::int64_t printed = 0;
    std::printf("%-20s  %-20s  %8s  %s\n",
                "start", "end", "rows", "status");
    for (const auto& s : sessions) {
        if (printed++ >= limit) break;
        std::printf("%-20s  %-20s  %8lld  %s\n",
                    formatBackupDate(s.startDate).c_str(),
                    formatBackupDate(s.endDate).c_str(),
                    static_cast<long long>(s.rowCount),
                    s.inProgress ? "in progress" : "complete");
    }
    return 0;
}

int cmdBackupsShow(std::vector<std::string_view> args) {
    if (args.size() < 3) {
        std::fprintf(stderr, "error: backups show: expected <filename>\n");
        return 2;
    }
    const std::string filename(args[2]);
    std::int64_t at = std::numeric_limits<std::int64_t>::max();
    for (std::size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--at" && i + 1 < args.size()) {
            at = parseIsoUtc(args[++i]);
        } else {
            throw std::runtime_error("backups show: unknown flag '" + std::string(args[i]) + "'");
        }
    }
    d2r::BackupDb db(d2r::backupDbPath());
    auto row = db.at(filename, at);
    if (!row) {
        std::printf("(no backup for %s at or before %s)\n",
                    filename.c_str(), formatBackupDate(at).c_str());
        return 1;
    }
    std::printf("file:  %s\n", filename.c_str());
    std::printf("state: %s\n", stateName(row->state));
    std::printf("bytes: %zu\n", row->data.size());
    if (row->checksum) {
        std::printf("checksum: 0x%08x\n", *row->checksum);
    } else {
        std::printf("checksum: (none)\n");
    }
    return 0;
}

int cmdBackupsRecover(std::vector<std::string_view>       args,
                      const std::filesystem::path&        primarySavePath) {
    if (args.size() < 3) {
        std::fprintf(stderr, "error: backups recover: expected <filename>\n");
        return 2;
    }
    const std::string filename(args[2]);
    std::optional<std::int64_t> at;
    std::filesystem::path       to = primarySavePath;
    bool                        pre = true;
    for (std::size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--at" && i + 1 < args.size()) at = parseIsoUtc(args[++i]);
        else if (args[i] == "--to" && i + 1 < args.size()) to = args[++i];
        else if (args[i] == "--no-pre-snapshot") pre = false;
        else throw std::runtime_error("backups recover: unknown flag '" + std::string(args[i]) + "'");
    }
    if (!at) {
        std::fprintf(stderr, "error: backups recover: --at YYYY-MM-DDTHH:MM:SSZ is required\n");
        return 2;
    }
    if (to.empty()) {
        std::fprintf(stderr,
            "error: backups recover: destination unknown. Pass --to <dir>\n"
            "       or the global --path <saves-dir>.\n");
        return 2;
    }
    d2r::BackupDb db(d2r::backupDbPath());
    // Scheduler needs a saves-dir; use `to` as the effective one. If it's
    // the primary saves dir that matches, in-place semantics. If it's an
    // alt dir, that's also fine -- the scheduler only reads bytes from it
    // for the pre-recovery snapshot, which is what we want.
    d2r::BackupScheduler sched(db, to);
    d2r::RecoverySpec spec;
    spec.destDir              = to;
    spec.filename             = filename;
    spec.atUnix               = *at;
    spec.preRecoveryBackup    = pre;
    spec.allowTombstoneRestore = (to == primarySavePath);

    const auto rep = d2r::recoverFile(db, sched, spec);
    if (!rep.restored && !rep.wasTombstone) {
        std::fprintf(stderr, "no backup for %s at or before %s\n",
                     filename.c_str(), formatBackupDate(*at).c_str());
        return 1;
    }
    std::printf("recovered:       %s\n", filename.c_str());
    std::printf("as-of:           %s (asked %s)\n",
                formatBackupDate(rep.recoveredDate).c_str(),
                formatBackupDate(*at).c_str());
    std::printf("destination:     %s\n", (to / filename).string().c_str());
    std::printf("bytes-written:   %lld\n", static_cast<long long>(rep.bytesWritten));
    std::printf("tombstone:       %s\n", rep.wasTombstone ? "yes" : "no");
    std::printf("pre-snapshot:    %s\n", rep.preSnapshotTaken ? "taken" : "skipped");
    return 0;
}

// ---- backups diff ----------------------------------------------------------
//
// Byte-diff N adjacent backup rows for a single .d2s or .d2i and label
// every differing offset with the region it lives in. Purpose: identify
// whether an *objective* signal for save-and-exit vs autosave exists
// inside the file itself (as opposed to the sibling .ctl / Settings.json
// write we currently key on in BackupScheduler::classifyBurst).
//
// A byte differs "in an unknown region" iff its offset falls outside
// every labeled range below. Those unknown ranges are the header bytes
// we've never wired up (see docs on .d2s coverage: 0x10-0x13, 0x16-0x17,
// 0x19-0x1A, 0x1C-0x1F, 0x24-0x97, 0x9F-0xA0, 0xAF-0xF7, 0xF9-0x12A,
// 0x13B-0x192) plus the quest/waypoint/NPC payloads whose OFFSETS we
// find but whose CONTENTS we don't decode.

struct BlobRegion {
    std::size_t begin  = 0;   // inclusive
    std::size_t end    = 0;   // exclusive
    const char* label  = "";
    bool        known  = false; // true when the parser DECODES this region
};

std::vector<BlobRegion> buildD2sRegions(std::span<const std::byte> bytes) {
    // Fixed header regions from src/character_parser.cpp + include/d2r/Save.hpp.
    std::vector<BlobRegion> regions = {
        {0x00, 0x04, "magic",           true},
        {0x04, 0x08, "version",         true},
        {0x08, 0x0C, "fileSize",        true},
        {0x0C, 0x10, "storedChecksum",  true},
        {0x14, 0x15, "status",          true},
        {0x15, 0x16, "actProgression",  true},
        {0x18, 0x19, "class",           true},
        {0x1B, 0x1C, "level(header)",   true},
        {0x20, 0x24, "timestamp",       true},
        {0x98, 0x9B, "difficulty",      true},
        {0x9B, 0x9F, "mapSeed",         true},
        {0xA1, 0xAF, "mercenary",       true},
        {0xF8, 0xF9, "expansionByte",   true},
        {0x12B,0x13B,"name",            true},
    };
    // Try to parse the character to pick up variable-section offsets.
    try {
        d2r::Character ch = d2r::parseCharacter(bytes);
        if (ch.questsOffset) {
            const auto e = ch.waypointsOffset ? ch.waypointsOffset : ch.statsOffset;
            regions.push_back({ch.questsOffset, e ? e : bytes.size(),
                               "quests(marker+payload,undecoded)", false});
        }
        if (ch.waypointsOffset) {
            const auto e = ch.npcOffset ? ch.npcOffset : ch.statsOffset;
            regions.push_back({ch.waypointsOffset, e ? e : bytes.size(),
                               "waypoints(undecoded)", false});
        }
        if (ch.npcOffset) {
            const auto e = ch.statsOffset ? ch.statsOffset : bytes.size();
            regions.push_back({ch.npcOffset, e, "npc(undecoded)", false});
        }
        if (ch.statsOffset && ch.skillsOffset) {
            regions.push_back({ch.statsOffset, ch.skillsOffset,
                               "attributes", true});
        }
        if (ch.skillsOffset && ch.itemsOffset) {
            regions.push_back({ch.skillsOffset, ch.itemsOffset,
                               "skills", true});
        }
        if (ch.itemsOffset) {
            const std::size_t e = ch.corpseJMOffset ? ch.corpseJMOffset
                                                    : bytes.size();
            regions.push_back({ch.itemsOffset, e, "items", true});
        }
        if (ch.corpseJMOffset) {
            const std::size_t e = ch.mercItemsJMOffset ? ch.mercItemsJMOffset - 2
                                                       : (ch.ironGolemItemOffset
                                                             ? ch.ironGolemItemOffset - 3
                                                             : bytes.size());
            regions.push_back({ch.corpseJMOffset, e, "corpseItems", true});
        }
        if (ch.mercItemsJMOffset) {
            const std::size_t e = ch.ironGolemItemOffset
                                    ? ch.ironGolemItemOffset - 3
                                    : bytes.size();
            regions.push_back({ch.mercItemsJMOffset - 2, e,
                               "mercItems", true});
        }
        if (ch.ironGolemItemOffset) {
            regions.push_back({ch.ironGolemItemOffset - 3, bytes.size(),
                               "ironGolem", true});
        }
    } catch (const std::exception&) {
        // Fall back to header-only labelling; unknown regions dominate.
    }
    std::sort(regions.begin(), regions.end(),
        [](const BlobRegion& a, const BlobRegion& b) {
            return a.begin < b.begin;
        });
    return regions;
}

const BlobRegion* regionAt(const std::vector<BlobRegion>& rs, std::size_t off) {
    for (const auto& r : rs) {
        if (off >= r.begin && off < r.end) return &r;
    }
    return nullptr;
}

int cmdBackupsDiff(std::vector<std::string_view> args) {
    if (args.size() < 3) {
        std::fprintf(stderr, "error: backups diff: expected <filename>\n");
        return 2;
    }
    const std::string filename(args[2]);
    std::size_t limit = 3;
    std::filesystem::path dbPath = d2r::backupDbPath();
    std::size_t maxDiffsToPrint  = 64; // per pair; -1 for unlimited
    for (std::size_t i = 3; i < args.size(); ++i) {
        const auto a = args[i];
        auto need = [&](const char* flag) {
            if (i + 1 >= args.size()) {
                throw std::runtime_error(std::string("backups diff: ") + flag + " expects a value");
            }
            return args[++i];
        };
        if      (a == "--limit") limit = static_cast<std::size_t>(std::atoll(std::string(need("--limit")).c_str()));
        else if (a == "--db")    dbPath = std::filesystem::path(std::string(need("--db")));
        else if (a == "--max-diffs") maxDiffsToPrint = static_cast<std::size_t>(std::atoll(std::string(need("--max-diffs")).c_str()));
        else {
            throw std::runtime_error("backups diff: unknown flag '" + std::string(a) + "'");
        }
    }
    if (limit < 2) {
        std::fprintf(stderr, "error: backups diff: --limit must be >= 2\n");
        return 2;
    }

    d2r::BackupDb db(dbPath);
    const auto hist = db.historyFor(filename, limit);
    if (hist.size() < 2) {
        std::printf("(need at least 2 backups for %s; have %zu)\n",
                    filename.c_str(), hist.size());
        return 0;
    }
    // Fetch blobs. hist is date-DESC; walk chronologically ascending so
    // "prev -> next" reads naturally.
    struct Loaded {
        std::int64_t                 date  = 0;
        d2r::BackupDb::State         state = d2r::BackupDb::State::Autosave;
        std::vector<std::byte>       data;
    };
    std::vector<Loaded> rows;
    rows.reserve(hist.size());
    for (auto it = hist.rbegin(); it != hist.rend(); ++it) {
        auto r = db.at(filename, it->date);
        if (!r) continue;
        rows.push_back(Loaded{it->date, it->state, std::move(r->data)});
    }
    if (rows.size() < 2) {
        std::printf("(couldn't load enough blob rows to diff)\n");
        return 0;
    }

    std::printf("file: %s   loaded %zu rows (oldest first)\n",
                filename.c_str(), rows.size());
    for (const auto& r : rows) {
        std::printf("  %-20s  %-14s  %zu bytes\n",
                    formatBackupDate(r.date).c_str(),
                    stateName(r.state), r.data.size());
    }

    // Region maps: build one per row (offsets can shift when the file
    // grows/shrinks). For diffs we index the *earlier* row's regions;
    // this keeps offsets stable within the reported pair.
    for (std::size_t i = 0; i + 1 < rows.size(); ++i) {
        const auto& a = rows[i];
        const auto& b = rows[i + 1];
        const auto regs = buildD2sRegions(a.data);

        std::printf("\n---- %s (%s) -> %s (%s) ----\n",
                    formatBackupDate(a.date).c_str(), stateName(a.state),
                    formatBackupDate(b.date).c_str(), stateName(b.state));
        std::printf("     sizes: %zu -> %zu   (%s%zd)\n",
                    a.data.size(), b.data.size(),
                    b.data.size() >= a.data.size() ? "+" : "",
                    static_cast<std::ptrdiff_t>(b.data.size()) -
                        static_cast<std::ptrdiff_t>(a.data.size()));

        // Aggregate diffs by region label so we don't drown in per-byte
        // rows for the items bitstream (which shifts wildly on every save).
        std::map<std::string, std::size_t> perRegionKnown;
        std::map<std::string, std::size_t> perRegionUnknown;
        std::size_t totalDiffs = 0;
        std::size_t unknownDiffs = 0;
        std::size_t printed = 0;

        const std::size_t common = std::min(a.data.size(), b.data.size());
        for (std::size_t off = 0; off < common; ++off) {
            if (a.data[off] == b.data[off]) continue;
            ++totalDiffs;
            const auto* reg = regionAt(regs, off);
            const bool known = reg && reg->known;
            const std::string label = reg ? reg->label : "UNKNOWN";
            if (known) perRegionKnown[label] += 1;
            else       { perRegionUnknown[label] += 1; ++unknownDiffs; }

            // Print per-byte rows for unknown regions (that's where the
            // interesting signal would hide). For known/decoded regions
            // we only tally; per-byte churn in the items bitstream would
            // otherwise flood the output.
            if (!known && printed < maxDiffsToPrint) {
                std::printf("  0x%04zx  %02x -> %02x   [%s]\n",
                            off,
                            static_cast<unsigned>(std::to_integer<std::uint8_t>(a.data[off])),
                            static_cast<unsigned>(std::to_integer<std::uint8_t>(b.data[off])),
                            label.c_str());
                ++printed;
            }
        }
        if (b.data.size() != a.data.size()) {
            std::printf("  (tail-length delta of %zu bytes not shown)\n",
                        (b.data.size() > a.data.size())
                            ? b.data.size() - a.data.size()
                            : a.data.size() - b.data.size());
        }

        std::printf("summary: %zu total diffs (%zu in unknown regions, %zu in decoded regions)\n",
                    totalDiffs, unknownDiffs, totalDiffs - unknownDiffs);
        if (!perRegionUnknown.empty()) {
            std::printf("  unknown-region breakdown:\n");
            for (const auto& [k, v] : perRegionUnknown) {
                std::printf("    %-40s  %zu\n", k.c_str(), v);
            }
        }
        if (!perRegionKnown.empty()) {
            std::printf("  decoded-region breakdown:\n");
            for (const auto& [k, v] : perRegionKnown) {
                std::printf("    %-40s  %zu\n", k.c_str(), v);
            }
        }
    }
    return 0;
}

// ---- backups find-item ----------------------------------------------------
//
// Scan historical .d2s and (optionally) .d2i blobs across N recent rows
// per file for items matching --name <substring> or --code <exact>.
// Prints every row+location where a match exists so the user can pick a
// row to `backups recover`. Intended for the "I lost a unique -- when
// did I last have it?" workflow.

std::string toLowerAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

bool endsWithIgnoreCase(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    const auto off = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[off + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

std::string characterBasenameStem(std::string_view filename) {
    if (endsWithIgnoreCase(filename, ".d2s")) {
        return std::string(filename.substr(0, filename.size() - 4));
    }
    return std::string(filename);
}

// Resolve an item's display name. Prefers the quality-specific name
// (e.g. "Earthshaker" for unique mau8 rather than the base "Legendary
// Mallet"), falls back to the base item name from armor/weapon/misc.
std::string resolveItemDisplayName(const d2r::RefDb& refDb, const d2r::Item& it) {
    if (it.quality == d2r::ItemQuality::Unique) {
        if (const auto* r = refDb.lookupUnique(static_cast<std::uint16_t>(it.uniqueId))) {
            return r->index;
        }
    } else if (it.quality == d2r::ItemQuality::Set) {
        if (const auto* r = refDb.lookupSetItem(static_cast<std::uint16_t>(it.setItemId))) {
            return r->index;
        }
    }
    // Base name resolved by ItemParser (may be empty if the code is
    // unknown to the reference DB, which is unusual).
    return it.itemName;
}

int cmdBackupsFindItem(std::vector<std::string_view>       args,
                       const std::filesystem::path&        exePath,
                       std::string_view                    referenceDbOverride) {
    std::string charName;
    bool        allCharacters = false;
    bool        includeStash  = false;
    std::string namePattern;
    std::string codePattern;
    std::filesystem::path dbPath = d2r::backupDbPath();
    std::size_t rowLimit = 500;   // per file
    std::size_t printLimit = 200; // matching rows per file
    for (std::size_t i = 2; i < args.size(); ++i) {
        const auto a = args[i];
        auto need = [&](const char* flag) -> std::string_view {
            if (i + 1 >= args.size()) {
                throw std::runtime_error(std::string("backups find-item: ") + flag + " expects a value");
            }
            return args[++i];
        };
        if      (a == "--character")       charName = std::string(need("--character"));
        else if (a == "--all-characters")  allCharacters = true;
        else if (a == "--shared-stash")    includeStash = true;
        else if (a == "--name")            namePattern = std::string(need("--name"));
        else if (a == "--code")            codePattern = std::string(need("--code"));
        else if (a == "--db")              dbPath = std::filesystem::path(std::string(need("--db")));
        else if (a == "--limit-rows")      rowLimit = static_cast<std::size_t>(std::atoll(std::string(need("--limit-rows")).c_str()));
        else if (a == "--limit-matches")   printLimit = static_cast<std::size_t>(std::atoll(std::string(need("--limit-matches")).c_str()));
        else {
            throw std::runtime_error("backups find-item: unknown flag '" + std::string(a) + "'");
        }
    }
    // Argument validation. Exactly one character-scope flag required;
    // exactly one item-selector required.
    if (charName.empty() == !allCharacters) {
        std::fprintf(stderr,
            "error: backups find-item: pass exactly one of --character NAME or --all-characters\n");
        return 2;
    }
    if (namePattern.empty() == codePattern.empty()) {
        std::fprintf(stderr,
            "error: backups find-item: pass exactly one of --name PATTERN or --code CODE\n");
        return 2;
    }

    const auto refDbPath = d2r::findReferenceDb(exePath, referenceDbOverride);
    if (!refDbPath) {
        std::fprintf(stderr,
            "error: backups find-item: reference DB not found. Build the\n"
            "       'reference_db' target or set D2R_REFERENCE_DB / pass\n"
            "       --reference-db.\n");
        return 1;
    }
    d2r::RefDb refDb(*refDbPath);
    // ItemParser + SharedStashParser both need the armor/weapon/misc/
    // uniqueitems/setitems tables populated; without this every parse
    // silently produces an empty item list.
    refDb.loadItemTables();

    const std::string wantNameLower = toLowerAscii(namePattern);
    const std::string wantCodeLower = toLowerAscii(codePattern);
    auto itemMatches = [&](const d2r::Item& it) -> bool {
        if (!wantCodeLower.empty()) {
            return toLowerAscii(it.code) == wantCodeLower;
        }
        // Match against display name AND base name -- users often type
        // "Earthshaker" (unique name) or "Legendary Mallet" (base).
        const auto disp = toLowerAscii(resolveItemDisplayName(refDb, it));
        if (!disp.empty() && disp.find(wantNameLower) != std::string::npos) return true;
        const auto base = toLowerAscii(it.itemName);
        if (!base.empty() && base.find(wantNameLower) != std::string::npos) return true;
        return false;
    };

    d2r::BackupDb db(dbPath);
    const auto sums = db.summariseFiles();
    if (sums.empty()) {
        std::printf("(no backups on record)\n");
        return 0;
    }

    // Build the ordered file list. We always search in filename order
    // for determinism.
    std::vector<std::string> targets;
    const std::string wantStemLower = toLowerAscii(charName);
    for (const auto& fs : sums) {
        if (endsWithIgnoreCase(fs.filename, ".d2s")) {
            const auto stem = toLowerAscii(characterBasenameStem(fs.filename));
            if (allCharacters || stem == wantStemLower) {
                targets.push_back(fs.filename);
            }
        } else if (includeStash && endsWithIgnoreCase(fs.filename, ".d2i")) {
            targets.push_back(fs.filename);
        }
    }
    if (targets.empty()) {
        std::fprintf(stderr,
            "error: backups find-item: no matching files in %s\n",
            dbPath.string().c_str());
        return 1;
    }

    std::printf("search: %s%s   scanning %zu file%s (up to %zu rows each)\n",
                wantCodeLower.empty() ? "name~" : "code=",
                wantCodeLower.empty() ? namePattern.c_str() : codePattern.c_str(),
                targets.size(), targets.size() == 1 ? "" : "s",
                rowLimit);

    d2r::ItemParser itemParser(refDb);
    d2r::SharedStashParser stashParser(refDb);

    // Per-file totals for the trailing summary line.
    struct Totals {
        std::string  filename;
        std::size_t  rowsScanned  = 0;
        std::size_t  rowsMatched  = 0;
        std::int64_t firstMatch   = 0;
        std::int64_t lastMatch    = 0;
    };
    std::vector<Totals> totals;

    for (const auto& fname : targets) {
        Totals t; t.filename = fname;
        const auto hist = db.historyFor(fname, rowLimit);
        t.rowsScanned = hist.size();

        std::printf("\n== %s (%zu rows) ==\n", fname.c_str(), hist.size());
        std::size_t printed = 0;

        // Walk NEWEST FIRST so the top of the section is always the
        // most recent snapshot containing the item -- that's the row
        // the user usually wants to restore.
        for (const auto& hr : hist) {
            if (hr.state == d2r::BackupDb::State::Deleted) continue;
            if (hr.sizeBytes <= 0) continue;
            auto row = db.at(fname, hr.date);
            if (!row || row->data.empty()) continue;

            // Collect matches for this row. `matches` uses a small
            // vector to preserve on-file order (character inv, then
            // merc, then corpse, then golem, or stash-tab order).
            struct Hit {
                std::string location;
                std::string displayName;
                std::string code;
                d2r::ItemQuality quality = d2r::ItemQuality::None;
                std::uint32_t    fingerprint = 0;
            };
            std::vector<Hit> matches;

            auto emit = [&](const std::vector<d2r::Item>& items,
                            std::string_view loc) {
                for (const auto& it : items) {
                    if (itemMatches(it)) {
                        matches.push_back(Hit{
                            std::string(loc),
                            resolveItemDisplayName(refDb, it),
                            it.code,
                            it.quality,
                            it.fingerprint,
                        });
                    }
                    for (const auto& s : it.socketedItems) {
                        if (itemMatches(s)) {
                            matches.push_back(Hit{
                                std::string(loc) + " (socketed)",
                                resolveItemDisplayName(refDb, s),
                                s.code,
                                s.quality,
                                s.fingerprint,
                            });
                        }
                    }
                }
            };

            try {
                if (endsWithIgnoreCase(fname, ".d2s")) {
                    const auto ch = d2r::parseCharacter(row->data);
                    if (ch.itemsOffset) {
                        emit(itemParser.parseItems(row->data, ch.itemsOffset), fname);
                    }
                    if (ch.mercItemsJMOffset) {
                        emit(itemParser.parseItems(row->data, ch.mercItemsJMOffset),
                             std::string(fname) + " (merc)");
                    }
                    if (ch.corpseJMOffset && ch.corpseItemCount == 1) {
                        emit(itemParser.parseItems(row->data, ch.corpseJMOffset + 16),
                             std::string(fname) + " (corpse)");
                    }
                    if (ch.hasIronGolem && ch.ironGolemItemOffset) {
                        std::vector<d2r::Item> golem{
                            itemParser.parseSingleItem(row->data, ch.ironGolemItemOffset)};
                        emit(golem, std::string(fname) + " (iron golem)");
                    }
                } else {
                    // Shared stash.
                    const auto ss = stashParser.parse(row->data);
                    for (std::size_t ti = 0; ti < ss.tabs.size(); ++ti) {
                        emit(ss.tabs[ti].items,
                             fname + " (tab " + std::to_string(ti + 1) + ")");
                    }
                }
            } catch (const std::exception&) {
                // Partial parse: any hits we already collected still count.
            }

            if (matches.empty()) continue;

            t.rowsMatched += 1;
            if (t.firstMatch == 0 || hr.date < t.firstMatch) t.firstMatch = hr.date;
            if (hr.date > t.lastMatch) t.lastMatch = hr.date;

            if (printed < printLimit) {
                for (const auto& m : matches) {
                    // "unique / mau8 / Earthshaker  fp=0x12345678"
                    std::printf("  %-20s  %-14s  %-32s  %-8s  %s  fp=0x%08x\n",
                                formatBackupDate(hr.date).c_str(),
                                stateName(hr.state),
                                m.location.c_str(),
                                m.code.c_str(),
                                m.displayName.c_str(),
                                m.fingerprint);
                }
                ++printed;
            } else if (printed == printLimit) {
                std::printf("  ... (further matches suppressed; pass --limit-matches N to see more)\n");
                ++printed;
            }
        }

        if (t.rowsMatched == 0) {
            std::printf("  (no matches)\n");
        }
        totals.push_back(std::move(t));
    }

    std::printf("\nsummary\n");
    for (const auto& t : totals) {
        if (t.rowsMatched == 0) {
            std::printf("  %-40s   0 matching rows (scanned %zu)\n",
                        t.filename.c_str(), t.rowsScanned);
        } else {
            std::printf("  %-40s  %2zu matching rows   first %s  last %s\n",
                        t.filename.c_str(), t.rowsMatched,
                        formatBackupDate(t.firstMatch).c_str(),
                        formatBackupDate(t.lastMatch).c_str());
        }
    }
    return 0;
}

int cmdBackupsPrune(std::vector<std::string_view> args) {
    int days = 30, sessions = 100;
    for (std::size_t i = 2; i < args.size(); ++i) {
        if      (args[i] == "--days"     && i + 1 < args.size()) days = std::atoi(std::string(args[++i]).c_str());
        else if (args[i] == "--sessions" && i + 1 < args.size()) sessions = std::atoi(std::string(args[++i]).c_str());
        else throw std::runtime_error("backups prune: unknown flag '" + std::string(args[i]) + "'");
    }
    d2r::BackupDb db(d2r::backupDbPath());
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    const auto n   = db.enforceRetention(days, sessions, now);
    std::printf("pruned %lld rows (days=%d sessions=%d)\n",
                static_cast<long long>(n), days, sessions);
    return 0;
}

int cmdBackupsSnapshot(const std::filesystem::path& savePath) {
    if (savePath.empty()) {
        std::fprintf(stderr,
            "error: backups snapshot: requires --path <saves-dir>\n");
        return 2;
    }
    d2r::BackupDb db(d2r::backupDbPath());
    d2r::BackupScheduler sched(db, savePath);
    sched.takeStartupSnapshot();
    const auto sums = db.summariseFiles();
    std::printf("snapshot: %zu file%s in %s\n",
                sums.size(), sums.size() == 1 ? "" : "s",
                savePath.string().c_str());
    return 0;
}

int cmdBackups(std::vector<std::string_view>       args,
               const std::filesystem::path&        primarySavePath,
               const std::filesystem::path&        exePath,
               std::string_view                    referenceDbOverride) {
    if (args.size() < 2) {
        printBackupsUsage();
        return 2;
    }
    try {
        const auto sub = args[1];
        if (sub == "summary")   return cmdBackupsSummary();
        if (sub == "list")      return cmdBackupsList(args);
        if (sub == "sessions")  return cmdBackupsSessions(args);
        if (sub == "show")      return cmdBackupsShow(args);
        if (sub == "diff")      return cmdBackupsDiff(args);
        if (sub == "find-item") return cmdBackupsFindItem(args, exePath, referenceDbOverride);
        if (sub == "recover")   return cmdBackupsRecover(args, primarySavePath);
        if (sub == "prune")     return cmdBackupsPrune(args);
        if (sub == "snapshot")  return cmdBackupsSnapshot(primarySavePath);
        if (sub == "-h" || sub == "--help") { printBackupsUsage(); return 0; }
        std::fprintf(stderr, "error: unknown backups subcommand '%.*s'\n\n",
                     static_cast<int>(sub.size()), sub.data());
        printBackupsUsage();
        return 2;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
}

} // namespace

#endif // D2R_HAVE_INOTIFY

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
    else if (cmd == "set-difficulty") { requirePath(); requireArgs(2); watchCompatible = false;
        runCommand = [p = resolveCharacter(savePath, positional[1]).string(),
                      d = std::string(positional[2])]{ return cmdSetDifficulty(p, d); }; }
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
#if D2R_HAVE_INOTIFY
    else if (cmd == "backups") {
        // Subcommand-style; owns its own arg parsing. --watch is nonsense
        // here (backups is idempotent + queries), so opt out.
        watchCompatible = false;
        std::vector<std::string_view> subArgs(positional.begin(), positional.end());
        auto sp = savePath;  // may be empty; recover checks
        runCommand = [subArgs = std::move(subArgs), sp,
                      exe = std::string(argv[0]),
                      r   = std::string(referenceDbOverride)]{
            return cmdBackups(subArgs, sp, exe, r);
        };
    }
#endif
#if D2R_HAVE_DASHBOARD
    else if (cmd == "dashboard") { requirePath();
        // Verb-scoped flags: --print (headless render), --width N,
        // --height N. Anything else is an error.
        d2r::DashboardOptions dashOpts;
        for (std::size_t i = 1; i < positional.size(); ++i) {
            const auto a = positional[i];
            auto need = [&](const char* flag) -> std::string {
                if (i + 1 >= positional.size()) {
                    std::fprintf(stderr,
                        "error: dashboard: %s expects a value\n", flag);
                    std::exit(2);
                }
                return std::string(positional[++i]);
            };
            if      (a == "--print")   dashOpts.printOnce   = true;
            else if (a == "--width")   dashOpts.printWidth  = std::atoi(need("--width").c_str());
            else if (a == "--height")  dashOpts.printHeight = std::atoi(need("--height").c_str());
            else {
                std::fprintf(stderr,
                    "error: dashboard: unknown argument '%.*s'\n",
                    static_cast<int>(a.size()), a.data());
                std::exit(2);
            }
        }
        // The dashboard runs its own inotify watcher inside `runDashboard`,
        // so the outer --watch machinery is redundant. Emit a note and
        // fall through to the non-watch code path instead of erroring or
        // double-watching.
        if (watchRequested) {
            std::fprintf(stderr,
                "note: 'dashboard' already refreshes on save-file changes; "
                "ignoring --watch\n");
            watchRequested = false;
        }
        runCommand = [exe = std::string(argv[0]),
                      sp  = savePath,
                      r   = std::string(referenceDbOverride),
                      opts = dashOpts]{
            return d2r::runDashboard(sp, r, exe, opts);
        };
    }
#else
    else if (cmd == "dashboard") {
        std::fprintf(stderr,
            "error: 'dashboard' was compiled out of this build.\n"
            "       Install the ftxui vcpkg port and re-configure with\n"
            "       -DD2R_USE_VCPKG=ON.\n");
        return 2;
    }
#endif
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
            "error: '%.*s' is not compatible with --watch (either it modifies\n"
            "       save files, or it already runs its own watch loop).\n",
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

            if (trig->kind == d2r::DirectoryWatcher::Trigger::Kind::Executable) {
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