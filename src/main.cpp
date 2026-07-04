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
        "  items    <name>              List every item on the character.\n"
        "\n"
        "Account commands (auto-discovers stash + scans every .d2s in --path):\n"
#if D2R_ENABLE_RUNEWORDS_WIP
        "  chronicle [--uniques] [--sets] [--runewords] [--full | --query STR ...]\n"
#else
        "  chronicle [--uniques] [--sets] [--full | --query STR ...]\n"
#endif
        "                               Chronicle progress + item lists.\n"
        "                               Category selectors restrict the report;\n"
        "                               with none, all available categories are\n"
        "                               shown. Without --full or --query only\n"
        "                               items you have NOT yet chronicled are\n"
        "                               listed. --full prints every item with a\n"
        "                               [X]/[ ] found marker. --query filters to\n"
        "                               items whose base/display/set name\n"
        "                               contains any of the supplied strings\n"
        "                               (case-insensitive substring); --query\n"
        "                               consumes ALL remaining arguments so it\n"
        "                               must come last. --full and --query are\n"
        "                               mutually exclusive.\n"
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

int cmdItems(const std::filesystem::path& exePath, const std::string& savePath) {
    const auto dbPath = d2r::findReferenceDb(exePath);
    if (!dbPath) {
        std::fprintf(stderr, "error: reference DB not found\n");
        return 1;
    }
    d2r::RefDb db(*dbPath);
    db.loadItemTables();

    const auto bytes = d2r::readFile(savePath);
    const auto ch = d2r::parseCharacter(bytes);
    if (!ch.itemsOffset) {
        std::fprintf(stderr, "error: no JM section in %s\n", savePath.c_str());
        return 1;
    }

    d2r::ItemParser parser(db);
    std::size_t failIdx = 0;
    std::string failMsg;
    const auto items = parser.parseItems(bytes, ch.itemsOffset, &failIdx, &failMsg);

    std::printf("file:   %s (character=%u, decoded=%zu)\n",
                savePath.c_str(), ch.itemCount, items.size());
    if (!failMsg.empty()) {
        std::printf("parse-failure: item #%zu: %s\n", failIdx, failMsg.c_str());
    }
    std::printf("%-4s %-6s %-4s %-8s %-6s %-20s %-24s %s\n",
                "#", "@bit", "code", "qual", "ilvl", "name", "affix/set/unique", "flags");
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        std::string extra;
        if (it.quality == d2r::ItemQuality::Unique) {
            const auto* row = db.lookupUnique(it.uniqueId);
            extra = "unique#" + std::to_string(it.uniqueId);
            if (row) extra += " (" + row->index + ")";
        } else if (it.quality == d2r::ItemQuality::Set) {
            const auto* row = db.lookupSetItem(it.setItemId);
            extra = "set#" + std::to_string(it.setItemId);
            if (row) extra += " (" + row->index + ")";
        } else if (!it.prefixIds.empty() || !it.suffixIds.empty()) {
            extra = "prefix=" + std::to_string(it.prefixIds.size())
                  + " suffix=" + std::to_string(it.suffixIds.size());
        }
        std::string flags;
        if (it.identified)       flags += "I";
        if (it.socketed)         flags += "S";
        if (it.ethereal)         flags += "E";
        if (it.personalized)     flags += "P";
        if (it.runeword)         flags += "R";
        if (it.hasChronicleData) flags += "C";
        std::printf("%-4zu %-6zu %-4s %-8.*s %-6u %-20.20s %-24s %s\n",
                    i, it.startBitOffset, it.code.c_str(),
                    static_cast<int>(d2r::toString(it.quality).size()),
                    d2r::toString(it.quality).data(),
                    it.itemLevel, it.itemName.c_str(), extra.c_str(),
                    flags.c_str());
    }
    return 0;
}

namespace {

// How the chronicle command formats its item lists.
//   Default -- print only items not yet chronicled ("remaining").
//   Full    -- print every collectable item with a [X]/[ ] found marker.
//   Query   -- print items whose base/display/set name substring-matches
//              any of the caller-supplied query strings; also with a
//              [X]/[ ] found marker.
enum class ChronicleMode { Default, Full, Query };

// Case-insensitive substring match: does haystack contain any needle?
// An empty needles list matches everything (defensive; Query mode is
// only entered when at least one non-empty needle is present).
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

// Format the query list as e.g. '"sword", "axe"' for section headers.
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

} // namespace

int cmdChronicle(const std::filesystem::path& exePath,
                 const std::string& stashPath,
                 const std::string& scanDir,
                 bool showUniques, bool showSets, bool showRunewords,
                 ChronicleMode mode,
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
    auto scalar = [&](const std::string& sql) {
        auto st = db.prepare(sql);
        return st.step() ? st.columnInt64(0) : 0;
    };
    const auto totalUniques = scalar(
        "SELECT COUNT(*) FROM uniqueitems t "
        "LEFT JOIN (SELECT code, quest FROM armor "
        "           UNION ALL SELECT code, quest FROM weapons "
        "           UNION ALL SELECT code, quest FROM misc) b ON b.code = t.code "
        "WHERE t.id IS NOT NULL AND t.id != '' "
        "AND CAST(t.spawnable AS INT)=1 "
        "AND (t.disablechronicle IS NULL OR t.disablechronicle != '1') "
        "AND (t.disabled IS NULL OR t.disabled != '1') "
        "AND (b.quest IS NULL OR b.quest = '')");
    const auto totalSets = scalar(
        "SELECT COUNT(*) FROM setitems t "
        "LEFT JOIN (SELECT code, quest FROM armor "
        "           UNION ALL SELECT code, quest FROM weapons "
        "           UNION ALL SELECT code, quest FROM misc) b ON b.code = t.item "
        "WHERE t.id IS NOT NULL AND t.id != '' "
        "AND (b.quest IS NULL OR b.quest = '')");

    const auto combinedUniqueCount = foundUniqueIds.size();
    const auto combinedSetCount    = foundSetIds.size();

    std::printf("\ncoverage (collectable items only):\n");
    std::printf("  uniques: %4zu / %4lld found (%5.1f%%)\n",
                combinedUniqueCount, static_cast<long long>(totalUniques),
                100.0 * combinedUniqueCount / std::max<std::int64_t>(1, totalUniques));
    std::printf("  sets:    %4zu / %4lld found (%5.1f%%)\n",
                combinedSetCount, static_cast<long long>(totalSets),
                100.0 * combinedSetCount / std::max<std::int64_t>(1, totalSets));

    // Diagnostic: items with the bit-29 blob that AREN'T in the stash tab.
    // Silent when everything reconciles; loud (with details) when it doesn't,
    // because that means the game hasn't finished syncing the item's inline
    // chronicle blob into the shared chronicle tab.
    if (!scanDir.empty()) {
        std::vector<const Bit29Item*> unreconciled;
        for (const auto& b : bit29Items) {
            const bool inChron =
                (b.quality == d2r::ItemQuality::Unique && foundUniqueIds.contains(b.id)) ||
                (b.quality == d2r::ItemQuality::Set    && foundSetIds.contains(b.id));
            if (!inChron) unreconciled.push_back(&b);
        }
        if (!unreconciled.empty()) {
            std::fprintf(stderr,
                "\nerror: %zu bit-29 chronicle item%s not represented in the shared "
                "stash chronicle tab. This is a mid-transition state: D2R has "
                "stamped an inline chronicle blob onto the item but hasn't copied "
                "the record into the account chronicle. Equip or stash the item "
                "to force the game to reconcile it, then re-run this scan.\n",
                unreconciled.size(), unreconciled.size() == 1 ? "" : "s");
            for (const auto* b : unreconciled) {
                std::fprintf(stderr, "  %s  quality=%.*s  id=%u  %s\n",
                    b->source.c_str(),
                    static_cast<int>(d2r::toString(b->quality).size()),
                    d2r::toString(b->quality).data(),
                    b->id, b->name.c_str());
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
            "       COALESCE(inm.en_us, b.name)         AS base_name "
            "FROM ";
        sql.append(table).append(" t ").append(join).append(
            " LEFT JOIN item_names inm_idx ON inm_idx.\"key\" = t.\"index\" "
            "WHERE t.id IS NOT NULL AND t.id != '' "
            // Base item must not be a quest item.
            "AND (b.quest IS NULL OR b.quest = '') ");
        if (std::string_view(table) == "uniqueitems") {
            sql +=
                "AND CAST(t.spawnable AS INT)=1 "
                "AND (t.disablechronicle IS NULL OR t.disablechronicle != '1') "
                "AND (t.disabled IS NULL OR t.disabled != '1') ";
        }
        sql += "ORDER BY base_name COLLATE NOCASE, t.\"index\" COLLATE NOCASE";

        struct Row {
            std::uint32_t id;
            std::string   display;
            std::string   base;
            bool          have;
        };
        std::vector<Row> shown;
        std::size_t      foundCount = 0;

        auto st = db.prepare(sql);
        while (st.step()) {
            Row r;
            r.id      = static_cast<std::uint32_t>(st.columnInt64(0));
            r.display = st.columnText(1);
            r.base    = st.columnText(2);
            r.have    = found.contains(r.id);

            bool include = false;
            switch (mode) {
                case ChronicleMode::Default: include = !r.have; break;
                case ChronicleMode::Full:    include = true;    break;
                case ChronicleMode::Query:
                    include = containsAnyCI(r.display, queries)
                           || containsAnyCI(r.base,    queries);
                    break;
            }
            if (!include) continue;
            if (r.have) ++foundCount;
            shown.push_back(std::move(r));
        }

        switch (mode) {
            case ChronicleMode::Default:
                std::printf("\n=== Remaining %s Items (%zu) ===\n",
                            label, shown.size());
                break;
            case ChronicleMode::Full:
                std::printf("\n=== All %s Items (%zu/%zu found) ===\n",
                            label, foundCount, shown.size());
                break;
            case ChronicleMode::Query:
                std::printf("\n=== %s Items matching %s (%zu/%zu found) ===\n",
                            label, quoteQueries(queries).c_str(),
                            foundCount, shown.size());
                break;
        }
        for (const auto& r : shown) {
            if (mode == ChronicleMode::Default) {
                std::printf("  %-24s  [#%u %s]\n",
                            r.base.empty() ? "?" : r.base.c_str(),
                            r.id, r.display.c_str());
            } else {
                std::printf("  [%c] %-24s  [#%u %s]\n",
                            r.have ? 'X' : ' ',
                            r.base.empty() ? "?" : r.base.c_str(),
                            r.id, r.display.c_str());
            }
        }
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
            "       COALESCE(inm_base.en_us, b.name)     AS base_name "
            "FROM setitems t "
            "LEFT JOIN (SELECT code, name, namestr, quest FROM armor "
            "           UNION ALL SELECT code, name, namestr, quest FROM weapons "
            "           UNION ALL SELECT code, name, namestr, quest FROM misc) b "
            "  ON b.code = t.item "
            "LEFT JOIN item_names inm_base ON inm_base.\"key\" = b.namestr "
            "LEFT JOIN item_names inm_idx  ON inm_idx.\"key\"  = t.\"index\" "
            "LEFT JOIN item_names inm_set  ON inm_set.\"key\"  = t.\"set\" "
            "WHERE t.id IS NOT NULL AND t.id != '' "
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

            bool include = false;
            switch (mode) {
                case ChronicleMode::Default: include = !r.have; break;
                case ChronicleMode::Full:    include = true;    break;
                case ChronicleMode::Query:
                    include = containsAnyCI(setName,   queries)
                           || containsAnyCI(r.display, queries)
                           || containsAnyCI(r.base,    queries);
                    break;
            }
            if (!include) continue;
            if (r.have) ++foundCount;
            ++totalShown;
            shownBySet[setName].push_back(std::move(r));
        }

        switch (mode) {
            case ChronicleMode::Default:
                std::printf("\n=== Remaining Set Items (%zu) ===\n", totalShown);
                break;
            case ChronicleMode::Full:
                std::printf("\n=== All Set Items (%zu/%zu found) ===\n",
                            foundCount, totalShown);
                break;
            case ChronicleMode::Query:
                std::printf("\n=== Set Items matching %s (%zu/%zu found) ===\n",
                            quoteQueries(queries).c_str(),
                            foundCount, totalShown);
                break;
        }
        for (const auto& [setName, items] : shownBySet) {
            std::printf("  %s\n", setName.c_str());
            for (const auto& r : items) {
                if (mode == ChronicleMode::Default) {
                    std::printf("      %-22s  [#%u %s]\n",
                                r.base.empty() ? "?" : r.base.c_str(),
                                r.id, r.display.c_str());
                } else {
                    std::printf("      [%c] %-22s  [#%u %s]\n",
                                r.have ? 'X' : ' ',
                                r.base.empty() ? "?" : r.base.c_str(),
                                r.id, r.display.c_str());
                }
            }
        }
    }

#if D2R_ENABLE_RUNEWORDS_WIP
    // Runewords are a work-in-progress: the chronicle stores a numeric itemId
    // whose meaning comes from D2R's item-runes.json string-table (not in our
    // snapshot), so we cannot say WHICH runewords are still missing -- just
    // how many. Per-runeword state is displayed as [?] in Full/Query modes.
    if (showRunewords) {
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
            bool include = false;
            switch (mode) {
                case ChronicleMode::Default: include = true; break;
                case ChronicleMode::Full:    include = true; break;
                case ChronicleMode::Query:
                    include = containsAnyCI(rw, queries);
                    break;
            }
            if (include) shown.push_back(rw);
        }

        switch (mode) {
            case ChronicleMode::Default:
                std::printf("\n=== Remaining Runewords (%zu) ===\n",
                            allRunewords.size() - chron.runewords.size());
                std::printf("(chronicle records %zu of %zu; per-runeword found/missing\n"
                            " mapping requires D2R's item-runes.json string-table extract\n"
                            " which is not currently loaded)\n",
                            chron.runewords.size(), allRunewords.size());
                break;
            case ChronicleMode::Full:
                std::printf("\n=== All Runewords (%zu total, %zu chronicled -- state unknown) ===\n",
                            allRunewords.size(), chron.runewords.size());
                break;
            case ChronicleMode::Query:
                std::printf("\n=== Runewords matching %s (%zu of %zu total) ===\n",
                            quoteQueries(queries).c_str(),
                            shown.size(), allRunewords.size());
                break;
        }
        for (const auto& rw : shown) {
            if (mode == ChronicleMode::Default) {
                std::printf("  %s\n", rw.c_str());
            } else {
                std::printf("  [?] %s\n", rw.c_str());
            }
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
            std::printf("  unique#%-4u %-30s  in %s\n",
                        id, o.name.c_str(), o.location.c_str());
        }
        ++missingA;
    }
    std::map<std::uint32_t, std::vector<OwnedItem>*> sortedS;
    for (auto& [id, list] : ownedSets) sortedS[id] = &list;
    for (auto& [id, listp] : sortedS) {
        if (setChronicled(id)) continue;
        for (const auto& o : *listp) {
            std::printf("  set#%-7u %-30s  in %s\n",
                        id, o.name.c_str(), o.location.c_str());
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
        std::printf("  unique#%-4u %-30s  found %s UTC  (mon=%u)\n",
                    id, lookupUniqueName(db, id).c_str(),
                    formatTimestamp(ep->timestampMinutes).c_str(),
                    ep->monsterId);
        ++missingB;
    }
    std::map<std::uint32_t, const d2r::ChronicleEntry*> sortedCS;
    for (const auto& e : chron.setItems) sortedCS[e.itemId] = &e;
    for (const auto& [id, ep] : sortedCS) {
        if (setOwned(id)) continue;
        std::printf("  set#%-7u %-30s  found %s UTC  (mon=%u)\n",
                    id, lookupSetName(db, id).c_str(),
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
    else if (cmd == "items")     { requirePath(); requireArgs(1);
        runCommand = [exe = std::string(argv[0]),
                      p = resolveCharacter(savePath, positional[1]).string()]{ return cmdItems(exe, p); }; }
    else if (cmd == "chronicle") { requirePath();
        // Chronicle selectors:
        //   --uniques / --sets / --runewords  category filters (any subset)
        //   --full                            print every item with [X]/[ ]
        //   --query STR STR ...               substring-filter every item
        // --full and --query are mutually exclusive. --query MUST come last:
        // every subsequent positional is consumed as a search string.
        bool showU = false, showS = false, showR = false;
        bool full = false, sawQuery = false;
        std::vector<std::string> queries;
        for (std::size_t i = 1; i < positional.size(); ++i) {
            const auto a = positional[i];
            if (sawQuery) { queries.emplace_back(a); continue; }
            if      (a == "--uniques")   showU = true;
            else if (a == "--sets")      showS = true;
#if D2R_ENABLE_RUNEWORDS_WIP
            else if (a == "--runewords") showR = true;
#endif
            else if (a == "--full")      full = true;
            else if (a == "--query")     sawQuery = true;
            else {
                std::fprintf(stderr,
                    "error: 'chronicle' does not accept '%.*s' (allowed: "
                    "--uniques, --sets"
#if D2R_ENABLE_RUNEWORDS_WIP
                    ", --runewords"
#endif
                    ", --full, --query)\n",
                    static_cast<int>(a.size()), a.data());
                return 2;
            }
        }
        if (full && sawQuery) {
            std::fprintf(stderr,
                "error: 'chronicle --full' and '--query' are mutually exclusive\n");
            return 2;
        }
        if (sawQuery && queries.empty()) {
            std::fprintf(stderr,
                "error: '--query' requires at least one search string\n");
            return 2;
        }
        ChronicleMode mode = ChronicleMode::Default;
        if (full)          mode = ChronicleMode::Full;
        else if (sawQuery) mode = ChronicleMode::Query;

        if (!showU && !showS && !showR) {
            showU = showS = true;
#if D2R_ENABLE_RUNEWORDS_WIP
            showR = true;
#endif
        }
        runCommand = [exe = std::string(argv[0]), sp = savePath.string(),
                      showU, showS, showR, mode, queries]{
            return cmdChronicle(exe, findStashFile(sp).string(), sp,
                                showU, showS, showR, mode, queries);
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
