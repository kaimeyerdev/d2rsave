// Snapshot builder for the TUI dashboard. Walks every .d2s in `saveDir`
// plus the shared stash, aggregating quest-item counts, chronicle rows,
// and a flat inventory list. UI-free.
//
// The parsing patterns here mirror `cmdItems` / `cmdChronicle` in
// src/main.cpp; when those grow features (e.g. bit-29 pending sync
// display), consider consolidating shared helpers into a small
// snapshot-support library.

#include "d2r/DashboardModel.hpp"

#include "d2r/CharacterParser.hpp"
#include "d2r/ItemParser.hpp"
#include "d2r/RefDb.hpp"
#include "d2r/Save.hpp"
#include "d2r/SharedStash.hpp"
#include "d2r/SharedStashParser.hpp"
#include "d2r/SunderCharms.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace d2r {

namespace {

// D2R experience table. Values are "exp to REACH level N" with level 1 = 0.
// Sourced from Maxroll's D2R experience reference table (public D2R data,
// verified against community-published `experience.txt` snapshots). These
// diverge materially from classic D2 above ~level 30 -- keep this table in
// sync with the game's current curve when Blizzard revises it. A CASC-
// driven extract in `d2r_refdb_gen` is the long-term home for these values;
// see TODO.md.
constexpr std::uint64_t kExpToReachLevel[100] = {
    /*  0 */ 0ULL,
    /*  1 */ 0ULL,
    /*  2 */ 500ULL,
    /*  3 */ 1500ULL,
    /*  4 */ 3750ULL,
    /*  5 */ 7875ULL,
    /*  6 */ 14175ULL,
    /*  7 */ 22680ULL,
    /*  8 */ 32886ULL,
    /*  9 */ 44396ULL,
    /* 10 */ 57715ULL,
    /* 11 */ 72144ULL,
    /* 12 */ 90180ULL,
    /* 13 */ 112275ULL,
    /* 14 */ 140906ULL,
    /* 15 */ 176132ULL,
    /* 16 */ 220165ULL,
    /* 17 */ 275207ULL,
    /* 18 */ 344008ULL,
    /* 19 */ 430010ULL,
    /* 20 */ 537513ULL,
    /* 21 */ 671891ULL,
    /* 22 */ 839864ULL,
    /* 23 */ 1049830ULL,
    /* 24 */ 1312287ULL,
    /* 25 */ 1640359ULL,
    /* 26 */ 2050449ULL,
    /* 27 */ 2563061ULL,
    /* 28 */ 3203826ULL,
    /* 29 */ 3902260ULL,
    /* 30 */ 4663553ULL,
    /* 31 */ 5493363ULL,
    /* 32 */ 6397855ULL,
    /* 33 */ 7383752ULL,
    /* 34 */ 8458379ULL,
    /* 35 */ 9629723ULL,
    /* 36 */ 10906488ULL,
    /* 37 */ 12298162ULL,
    /* 38 */ 13815086ULL,
    /* 39 */ 15468534ULL,
    /* 40 */ 17270791ULL,
    /* 41 */ 19235252ULL,
    /* 42 */ 21376515ULL,
    /* 43 */ 23710491ULL,
    /* 44 */ 26254525ULL,
    /* 45 */ 29027522ULL,
    /* 46 */ 32050088ULL,
    /* 47 */ 35344686ULL,
    /* 48 */ 38935798ULL,
    /* 49 */ 42850109ULL,
    /* 50 */ 47116709ULL,
    /* 51 */ 51767302ULL,
    /* 52 */ 56836449ULL,
    /* 53 */ 62361819ULL,
    /* 54 */ 68384473ULL,
    /* 55 */ 74949165ULL,
    /* 56 */ 82104680ULL,
    /* 57 */ 89904191ULL,
    /* 58 */ 98405658ULL,
    /* 59 */ 107672256ULL,
    /* 60 */ 117772849ULL,
    /* 61 */ 128782495ULL,
    /* 62 */ 140783010ULL,
    /* 63 */ 153863570ULL,
    /* 64 */ 168121381ULL,
    /* 65 */ 183662396ULL,
    /* 66 */ 200602101ULL,
    /* 67 */ 219066380ULL,
    /* 68 */ 239192444ULL,
    /* 69 */ 261129853ULL,
    /* 70 */ 285041630ULL,
    /* 71 */ 311105466ULL,
    /* 72 */ 339515048ULL,
    /* 73 */ 370481492ULL,
    /* 74 */ 404234916ULL,
    /* 75 */ 441026148ULL,
    /* 76 */ 481128591ULL,
    /* 77 */ 524840254ULL,
    /* 78 */ 572485967ULL,
    /* 79 */ 624419793ULL,
    /* 80 */ 681027665ULL,
    /* 81 */ 742730244ULL,
    /* 82 */ 809986056ULL,
    /* 83 */ 883294891ULL,
    /* 84 */ 963201521ULL,
    /* 85 */ 1050299747ULL,
    /* 86 */ 1145236814ULL,
    /* 87 */ 1248718217ULL,
    /* 88 */ 1361512946ULL,
    /* 89 */ 1484459201ULL,
    /* 90 */ 1618470619ULL,
    /* 91 */ 1764543065ULL,
    /* 92 */ 1923762030ULL,
    /* 93 */ 2097310703ULL,
    /* 94 */ 2286478756ULL,
    /* 95 */ 2492671933ULL,
    /* 96 */ 2717422497ULL,
    /* 97 */ 2962400612ULL,
    /* 98 */ 3229426756ULL,
    /* 99 */ 3520485254ULL,
};

// Base name lookup: 3-char code -> "en_us" display name via item_names,
// with fallback to armor/weapons/misc `name`.
std::string lookupBaseName(RefDb& db, std::string_view code) {
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

// Small text-to-int helper for reference-DB columns (all stored as
// TEXT). Empty / non-numeric -> 0.
int parseInt(std::string_view s) noexcept {
    if (s.empty()) return 0;
    try { return std::stoi(std::string(s)); }
    catch (const std::exception&) { return 0; }
}

// Return the tier of every base code (armor/weapons three-tier, misc = Misc)
// alongside a family map: base code -> Normal-tier sibling code, a
// type map: base code -> `type` slug (e.g. "swor", "tors"), and a
// level map: base code -> `level` (qlvl). The Uniques "By Tier" view
// keys off the family, groups families under the Normal-tier's item-
// class type, and orders families within each group by their Normal-
// tier base level (weakest -> strongest). Misc codes only appear in
// the tier map with tier=Misc; their level is captured for future use.
struct TierMaps {
    std::unordered_map<std::string, ChronicleTier> tier;
    std::unordered_map<std::string, std::string>   family;
    std::unordered_map<std::string, std::string>   type;
    std::unordered_map<std::string, int>           level;
};
TierMaps loadTierMap(RefDb& db) {
    TierMaps out;
    auto st = db.prepare(
        "SELECT code, normcode, ubercode, ultracode, type, level FROM armor "
        "UNION ALL "
        "SELECT code, normcode, ubercode, ultracode, type, level FROM weapons");
    while (st.step()) {
        const auto code = st.columnText(0);
        if (code.empty()) continue;
        const auto norm  = st.columnText(1);
        const auto uber  = st.columnText(2);
        const auto ultra = st.columnText(3);
        const auto ty    = st.columnText(4);
        const auto lvl   = parseInt(st.columnText(5));
        ChronicleTier t = ChronicleTier::None;
        if      (code == norm)  t = ChronicleTier::Normal;
        else if (code == uber)  t = ChronicleTier::Exceptional;
        else if (code == ultra) t = ChronicleTier::Elite;
        out.tier.emplace(code, t);
        if (!norm.empty()) out.family.emplace(code, norm);
        if (!ty.empty())   out.type.emplace(code, ty);
        out.level.emplace(code, lvl);
    }
    auto stMisc = db.prepare("SELECT code, level FROM misc");
    while (stMisc.step()) {
        const auto code = stMisc.columnText(0);
        if (code.empty()) continue;
        out.tier.emplace(code, ChronicleTier::Misc);
        out.level.emplace(code, parseInt(stMisc.columnText(1)));
    }
    return out;
}

// Same three-flag filter cmdChronicle uses: spawnable + not disabled +
// not a quest-item base. Returns the set of collectable IDs.
std::unordered_set<std::uint32_t> loadCollectable(RefDb& db,
                                                   std::string_view table,
                                                   std::string_view codeCol) {
    std::unordered_set<std::uint32_t> out;
    std::string sql =
        "SELECT t.id FROM ";
    sql.append(table);
    sql +=
        " t LEFT JOIN (SELECT code, quest FROM armor "
        "              UNION ALL SELECT code, quest FROM weapons "
        "              UNION ALL SELECT code, quest FROM misc) b ON b.code = t.\"";
    sql.append(codeCol);
    sql +=
        "\" WHERE t.id IS NOT NULL AND t.id != '' "
        "AND CAST(t.spawnable AS INT)=1 "
        "AND (t.disablechronicle IS NULL OR t.disablechronicle != '1') "
        "AND (t.disabled IS NULL OR t.disabled != '1') "
        "AND (b.quest IS NULL OR b.quest = '')";
    auto st = db.prepare(sql);
    while (st.step()) {
        out.insert(static_cast<std::uint32_t>(st.columnInt64(0)));
    }
    return out;
}

// Bump the correct field on the quest counters based on the item's
// base code (and, for the Hellfire Torch, uniqueId). Stackable items
// (keys, statues, shards) contribute `Item.stacks` per stack; unique
// charms and other non-stackable items contribute 1 each.
void countQuestItem(const Item& it,
                    HellfireTorchQuest& hf,
                    ColossalAncientsQuest& ca,
                    TerrorZones& tz) {
    const auto& code = it.code;
    const std::uint32_t qty = it.stacks > 0 ? it.stacks : 1u;

    if      (code == "pk1") hf.keysTerror      += qty;
    else if (code == "pk2") hf.keysHate        += qty;
    else if (code == "pk3") hf.keysDestruction += qty;
    else if (code == "dhn") hf.diablosHorn     += qty;
    else if (code == "mbr") hf.mephistosBrain  += qty;
    else if (code == "bey") hf.baalsEye        += qty;
    else if (code == "ua1") ca.talicAnguish       += qty;
    else if (code == "ua2") ca.korlicPain         += qty;   // ua2 = Korlic (not Madawc)
    else if (code == "ua3") ca.madawcIre          += qty;
    else if (code == "ua4") ca.bulKathosNightmare += qty;
    else if (code == "ua5") ca.woruskEnd          += qty;
    else if (code == "cjw") ca.colossalJewels     += qty;
    else if (code == "xa1") tz.shardWestern   += qty;
    else if (code == "xa2") tz.shardEastern   += qty;
    else if (code == "xa3") tz.shardSouthern  += qty;
    else if (code == "xa4") tz.shardDeep      += qty;
    else if (code == "xa5") tz.shardNorthern  += qty;

    // Hellfire Torch = large charm (cm2) with uniqueId 400. Non-stackable.
    if (code == "cm2" && it.quality == ItemQuality::Unique && it.uniqueId == 400) {
        ++hf.torchesHellfire;
    }
}

// Choose the primary display name for an item (unique/set overlay > base).
std::string primaryName(RefDb& db, const Item& it) {
    const bool named = it.quality == ItemQuality::Unique
                    || it.quality == ItemQuality::Set;
    if (named && !it.itemName.empty()) return it.itemName;
    return lookupBaseName(db, it.code);
}

// Highest active-difficulty index (0=Normal, 1=Nightmare, 2=Hell). If
// none are marked active, defaults to 0 with act=1.
void computeDifficulty(const Character& c,
                       std::uint8_t& outDiff,
                       std::uint8_t& outAct) {
    outDiff = 0;
    outAct  = 1;
    for (std::uint8_t i = 0; i < 3; ++i) {
        if (c.locations[i].active) {
            outDiff = i;
            outAct  = c.locations[i].act ? c.locations[i].act : 1;
        }
    }
}

// ---------------------------------------------------------------------------
// Per-file parse helpers used by the incremental snapshot pipeline. Each
// takes raw bytes and produces a self-contained DashboardFileCache entry;
// the aggregator merges entries into the final snapshot. Kept in the
// anonymous namespace so `primaryName` / `lookupBaseName` / `countQuestItem`
// / `computeDifficulty` are reachable.
// ---------------------------------------------------------------------------

DashboardFileCache::D2sEntry parseD2sIntoEntry(
    RefDb&                                        db,
    std::span<const std::byte>                    bytes,
    std::string_view                              filename,
    const std::unordered_set<std::uint32_t>&      collectableUniqueIds,
    const std::unordered_set<std::uint32_t>&      collectableSetIds) {
    DashboardFileCache::D2sEntry out;
    out.character = parseCharacter(bytes);
    if (out.character.itemsOffset == 0) return out;

    ItemParser p(db);
    const std::string filestr(filename);
    auto record = [&](const std::vector<Item>& items,
                      const std::string&       loc,
                      const std::string&       subLocOverride) {
        for (const auto& it : items) {
            countQuestItem(it, out.hellfire, out.colossal, out.terror);
            const std::string subLoc =
                subLocOverride.empty() ? characterItemSubLoc(it) : subLocOverride;
            InventoryItem inv;
            inv.name        = primaryName(db, it);
            inv.baseName    = lookupBaseName(db, it.code);
            inv.code        = it.code;
            inv.location    = loc;
            inv.subLocation = subLoc;
            inv.quality     = it.quality;
            inv.fingerprint = it.fingerprint;
            inv.stacks       = it.stacks;
            inv.hasStackSlot = it.hasStackSlot;
            inv.identified  = it.identified;
            out.items.push_back(std::move(inv));

            if (it.quality == ItemQuality::Unique) {
                if (collectableUniqueIds.contains(it.uniqueId)) {
                    out.chronUniqueLocs.emplace_back(it.uniqueId, loc);
                }
                out.ownedUniqueLocs.emplace_back(it.uniqueId, loc);
            } else if (it.quality == ItemQuality::Set) {
                if (collectableSetIds.contains(it.setItemId)) {
                    out.chronSetLocs.emplace_back(it.setItemId, loc);
                }
                out.ownedSetLocs.emplace_back(it.setItemId, loc);
            }

            for (const auto& s : it.socketedItems) {
                countQuestItem(s, out.hellfire, out.colossal, out.terror);
                InventoryItem sInv;
                sInv.name        = primaryName(db, s);
                sInv.baseName    = lookupBaseName(db, s.code);
                sInv.code        = s.code;
                sInv.location    = loc;
                sInv.subLocation = subLoc;   // inherit parent's bucket
                sInv.quality     = s.quality;
                sInv.fingerprint = s.fingerprint;
                sInv.stacks       = s.stacks;
                sInv.hasStackSlot = s.hasStackSlot;
                sInv.identified  = s.identified;
                sInv.socketed    = true;
                out.items.push_back(std::move(sInv));
                // Socketed uniques/sets (Rainbow Facets, Defender's Fire)
                // must count for reconcile ownership too.
                if (s.quality == ItemQuality::Unique) {
                    out.ownedUniqueLocs.emplace_back(s.uniqueId, loc);
                } else if (s.quality == ItemQuality::Set) {
                    out.ownedSetLocs.emplace_back(s.setItemId, loc);
                }
            }
        }
    };
    try {
        record(p.parseItems(bytes, out.character.itemsOffset), filestr, "");
        if (out.character.mercItemsJMOffset) {
            record(p.parseItems(bytes, out.character.mercItemsJMOffset),
                   filestr + " (merc)", "Merc");
        }
        if (out.character.corpseJMOffset && out.character.corpseItemCount == 1) {
            record(p.parseItems(bytes, out.character.corpseJMOffset + 16),
                   filestr + " (corpse)", "Corpse");
        }
        if (out.character.hasIronGolem && out.character.ironGolemItemOffset) {
            try {
                std::vector<Item> golem{
                    p.parseSingleItem(bytes, out.character.ironGolemItemOffset)};
                record(golem, filestr + " (iron golem)", "Iron Golem");
            } catch (const std::exception&) {}
        }
    } catch (const std::exception&) {
        // Partial parse: keep whatever succeeded.
    }
    return out;
}

DashboardFileCache::D2iEntry parseD2iIntoEntry(
    RefDb&                        db,
    std::span<const std::byte>    bytes) {
    DashboardFileCache::D2iEntry out;
    SharedStashParser sp(db);
    // Single full parse yields both chronicle + storage tabs; the older
    // code did two passes which doubled the .d2i cost. The chronicle
    // block is embedded in the same file so `parse()` reads everything
    // in one traversal; `parseChronicleOnly` just skips the tab items.
    out.chron    = sp.parseChronicleOnly(bytes);
    for (const auto& e : out.chron.uniques)  out.foundUniqueIds.insert(e.itemId);
    for (const auto& e : out.chron.setItems) out.foundSetIds.insert(e.itemId);

    const auto full = sp.parse(bytes);
    for (std::size_t i = 0; i < full.tabs.size(); ++i) {
        const std::string loc = "stash tab " + std::to_string(i + 1);
        for (const auto& it : full.tabs[i].items) {
            countQuestItem(it, out.hellfire, out.colossal, out.terror);
            InventoryItem inv;
            inv.name        = primaryName(db, it);
            inv.baseName    = lookupBaseName(db, it.code);
            inv.code        = it.code;
            inv.location    = loc;
            inv.quality     = it.quality;
            inv.fingerprint = it.fingerprint;
            inv.stacks       = it.stacks;
            inv.hasStackSlot = it.hasStackSlot;
            inv.identified  = it.identified;
            out.items.push_back(std::move(inv));
            if (it.quality == ItemQuality::Unique) {
                out.ownedUniqueLocs.emplace_back(it.uniqueId, loc);
            } else if (it.quality == ItemQuality::Set) {
                out.ownedSetLocs.emplace_back(it.setItemId, loc);
            }
            for (const auto& s : it.socketedItems) {
                countQuestItem(s, out.hellfire, out.colossal, out.terror);
                InventoryItem sInv;
                sInv.name        = primaryName(db, s);
                sInv.baseName    = lookupBaseName(db, s.code);
                sInv.code        = s.code;
                sInv.location    = loc;
                sInv.quality     = s.quality;
                sInv.fingerprint = s.fingerprint;
                sInv.stacks       = s.stacks;
                sInv.hasStackSlot = s.hasStackSlot;
                sInv.identified  = s.identified;
                sInv.socketed    = true;
                out.items.push_back(std::move(sInv));
                if (s.quality == ItemQuality::Unique) {
                    out.ownedUniqueLocs.emplace_back(s.uniqueId, loc);
                } else if (s.quality == ItemQuality::Set) {
                    out.ownedSetLocs.emplace_back(s.setItemId, loc);
                }
            }
        }
    }
    return out;
}

// Merge quest counters (all are sums of stack sizes / instance counts).
void mergeQuestCounts(HellfireTorchQuest&       dst,
                      const HellfireTorchQuest& src) {
    dst.keysTerror      += src.keysTerror;
    dst.keysHate        += src.keysHate;
    dst.keysDestruction += src.keysDestruction;
    dst.torchesHellfire += src.torchesHellfire;
    dst.diablosHorn     += src.diablosHorn;
    dst.mephistosBrain  += src.mephistosBrain;
    dst.baalsEye        += src.baalsEye;
}
void mergeQuestCounts(ColossalAncientsQuest&       dst,
                      const ColossalAncientsQuest& src) {
    dst.talicAnguish       += src.talicAnguish;
    dst.madawcIre          += src.madawcIre;
    dst.korlicPain         += src.korlicPain;
    dst.bulKathosNightmare += src.bulKathosNightmare;
    dst.woruskEnd          += src.woruskEnd;
    dst.colossalJewels     += src.colossalJewels;
}
void mergeQuestCounts(TerrorZones& dst, const TerrorZones& src) {
    dst.shardWestern  += src.shardWestern;
    dst.shardEastern  += src.shardEastern;
    dst.shardSouthern += src.shardSouthern;
    dst.shardDeep     += src.shardDeep;
    dst.shardNorthern += src.shardNorthern;
}

// Stat + parse-if-changed. Returns true when the cache entry was newly
// populated (missing or stale); false when the on-disk file matched the
// cached (mtime, size) and no work was needed. Missing files remove the
// entry from the cache and return true (mutation happened).
bool refreshD2sIfNeeded(
    RefDb&                                        db,
    const std::filesystem::path&                  path,
    const std::string&                            basename,
    const std::unordered_set<std::uint32_t>&      collectableUniqueIds,
    const std::unordered_set<std::uint32_t>&      collectableSetIds,
    DashboardFileCache&                           cache) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) ||
        !std::filesystem::is_regular_file(path, ec)) {
        // File is gone; drop any cached entry.
        return cache.d2s.erase(basename) > 0;
    }
    const auto mtime = std::filesystem::last_write_time(path, ec);
    const auto size  = std::filesystem::file_size(path, ec);
    if (ec) return false;   // stat failed; leave cache untouched.
    if (auto it = cache.d2s.find(basename); it != cache.d2s.end()) {
        if (it->second.mtime == mtime && it->second.size == size) {
            return false;   // cache still valid
        }
    }
    try {
        const auto bytes = readFile(path.string());
        auto entry = parseD2sIntoEntry(db, bytes, basename,
                                        collectableUniqueIds, collectableSetIds);
        entry.mtime = mtime;
        entry.size  = size;
        cache.d2s.insert_or_assign(basename, std::move(entry));
        return true;
    } catch (const std::exception&) {
        // Parse failed -- keep any prior entry (may still be valid) so
        // the pane stays populated with the last known-good state.
        return false;
    }
}

bool refreshD2iIfNeeded(
    RefDb&                          db,
    const std::filesystem::path&    path,
    DashboardFileCache&             cache) {
    std::error_code ec;
    if (path.empty() ||
        !std::filesystem::exists(path, ec) ||
        !std::filesystem::is_regular_file(path, ec)) {
        const bool had = cache.stash.has_value();
        cache.stash.reset();
        return had;
    }
    const auto mtime = std::filesystem::last_write_time(path, ec);
    const auto size  = std::filesystem::file_size(path, ec);
    if (ec) return false;
    if (cache.stash && cache.stash->mtime == mtime && cache.stash->size == size) {
        return false;
    }
    try {
        const auto bytes = readFile(path.string());
        auto entry = parseD2iIntoEntry(db, bytes);
        entry.mtime = mtime;
        entry.size  = size;
        cache.stash = std::move(entry);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Ensure the process-lifetime RefDb tables are loaded on the cache.
void ensureCacheTables(RefDb& db, DashboardFileCache& cache) {
    if (!cache.tierByCode || !cache.familyByCode ||
        !cache.typeByCode || !cache.levelByCode) {
        auto maps = loadTierMap(db);
        cache.tierByCode   = std::move(maps.tier);
        cache.familyByCode = std::move(maps.family);
        cache.typeByCode   = std::move(maps.type);
        cache.levelByCode  = std::move(maps.level);
    }
    if (!cache.collectableUniqueIds) cache.collectableUniqueIds = loadCollectable(db, "uniqueitems", "code");
    if (!cache.collectableSetIds)    cache.collectableSetIds    = loadCollectable(db, "setitems",    "item");
}

} // namespace

std::uint64_t experienceToReachLevel(std::uint32_t level) noexcept {
    if (level >= 100) return kExpToReachLevel[99];
    return kExpToReachLevel[level];
}

// Definition matches the declaration in DashboardModel.hpp. Lives at
// d2r:: scope (not the anonymous namespace above) so tests can reach
// it. The aggregator sites inside the anonymous namespace still call
// it unqualified via enclosing-namespace lookup.
std::string characterItemSubLoc(const Item& it) noexcept {
    if (it.location == 1)  return "Equipped";
    if (it.location == 2)  return "Belt";
    if (it.container == 5) return "Stash";
    if (it.container == 4) return "Cube";
    return "Inventory";
}

// ---------------------------------------------------------------------------
// Incremental snapshot pipeline.
//
// `refreshDashboardCacheFromDirectory` handles cold starts + manual [r]
// refreshes: full walk, stat each file, re-parse only when mtime/size
// changed, drop entries for files that vanished.
//
// `refreshDashboardCacheFromChanges` is the inotify hot path: touch only
// the named basenames (which the watcher just told us changed). Skips
// non-tracked filenames silently so the caller can forward the raw
// watcher payload without pre-filtering.
//
// `aggregateDashboardSnapshot` walks the cache in deterministic order,
// concatenates per-file items into snap.inventory, sums quest counters,
// merges first-wins ownership maps, runs the chronicle + reconcile SQL
// passes, and populates ActivePlayer from the newest-timestamped .d2s.
//
// The public one-shot `buildSnapshot(db, saveDir, stashPath)` is now a
// thin wrapper: create a throwaway cache, refresh from directory,
// aggregate. Preserves existing callers (tests, non-inotify builds).
// ---------------------------------------------------------------------------

void refreshDashboardCacheFromDirectory(
    RefDb&                                        db,
    const std::filesystem::path&                  saveDir,
    const std::filesystem::path&                  stashPath,
    DashboardFileCache&                           cache) {
    ensureCacheTables(db, cache);
    // Stash (single entry).
    refreshD2iIfNeeded(db, stashPath, cache);
    // Characters.
    std::unordered_set<std::string> presentD2s;
    if (!saveDir.empty()) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(saveDir, ec)) {
            if (ec) break;
            if (entry.path().extension() != ".d2s") continue;
            auto name = entry.path().filename().string();
            refreshD2sIfNeeded(db, entry.path(), name,
                                *cache.collectableUniqueIds,
                                *cache.collectableSetIds, cache);
            presentD2s.insert(std::move(name));
        }
    }
    // Prune vanished files.
    for (auto it = cache.d2s.begin(); it != cache.d2s.end();) {
        if (!presentD2s.contains(it->first)) it = cache.d2s.erase(it);
        else                                 ++it;
    }
}

void refreshDashboardCacheFromChanges(
    RefDb&                                        db,
    const std::filesystem::path&                  saveDir,
    const std::filesystem::path&                  stashPath,
    DashboardFileCache&                           cache,
    std::span<const std::string>                  changedBasenames) {
    ensureCacheTables(db, cache);
    const std::string stashName = stashPath.empty() ? std::string{}
                                                    : stashPath.filename().string();
    auto endsIn = [](std::string_view s, std::string_view suf) {
        if (s.size() < suf.size()) return false;
        const auto off = s.size() - suf.size();
        for (std::size_t i = 0; i < suf.size(); ++i) {
            const char a = static_cast<char>(
                std::tolower(static_cast<unsigned char>(s[off + i])));
            const char b = static_cast<char>(
                std::tolower(static_cast<unsigned char>(suf[i])));
            if (a != b) return false;
        }
        return true;
    };
    for (const auto& name : changedBasenames) {
        if (!stashName.empty() && name == stashName) {
            refreshD2iIfNeeded(db, stashPath, cache);
            continue;
        }
        if (endsIn(name, ".d2s")) {
            refreshD2sIfNeeded(db, saveDir / name, name,
                                *cache.collectableUniqueIds,
                                *cache.collectableSetIds, cache);
        }
        // Anything else (e.g. .ctl, .ma0, Settings.json) is irrelevant to
        // the snapshot -- silently ignored.
    }
}

DashboardSnapshot aggregateDashboardSnapshot(RefDb&              db,
                                             DashboardFileCache& cache) {
    ensureCacheTables(db, cache);

    DashboardSnapshot snap;
    snap.refreshedAtEpoch = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    // Aggregation-time work. First-wins semantics preserved by iterating
    // .d2s entries in sorted basename order (directory_iterator was
    // already non-deterministic in the old code; sorting here makes the
    // observable snapshot stable regardless of filesystem order).
    std::unordered_set<std::uint32_t> foundUniqueIds, foundSetIds;
    ChronicleTab                      chron;
    std::unordered_map<std::uint32_t, std::string> uniqueLocations, setLocations;
    std::unordered_map<std::uint32_t, std::string> ownedUniqueLocs, ownedSetLocs;

    // Stash first: matches the old code's stash-then-characters
    // ordering in snap.inventory and lets stash ownership seed the
    // reconcile maps before characters overlay.
    if (cache.stash) {
        const auto& s = *cache.stash;
        snap.inventory.insert(snap.inventory.end(),
                               s.items.begin(), s.items.end());
        mergeQuestCounts(snap.hellfireTorch,   s.hellfire);
        mergeQuestCounts(snap.colossalAncients, s.colossal);
        mergeQuestCounts(snap.terrorZones,     s.terror);
        for (auto id : s.foundUniqueIds) foundUniqueIds.insert(id);
        for (auto id : s.foundSetIds)    foundSetIds.insert(id);
        chron = s.chron;
        for (const auto& [id, loc] : s.ownedUniqueLocs) ownedUniqueLocs.try_emplace(id, loc);
        for (const auto& [id, loc] : s.ownedSetLocs)    ownedSetLocs.try_emplace(id, loc);
    }

    // Characters: sorted by basename for deterministic first-wins.
    std::vector<const std::string*> orderedD2s;
    orderedD2s.reserve(cache.d2s.size());
    for (const auto& kv : cache.d2s) orderedD2s.push_back(&kv.first);
    std::sort(orderedD2s.begin(), orderedD2s.end(),
              [](const std::string* a, const std::string* b) { return *a < *b; });

    Character bestChar;
    bool      haveBest = false;
    for (const std::string* namePtr : orderedD2s) {
        const auto& name = *namePtr;
        const auto& e    = cache.d2s.at(name);
        if (!haveBest || e.character.timestamp > bestChar.timestamp) {
            bestChar = e.character;
            snap.activePlayer.file = name;
            haveBest = true;
        }
        snap.inventory.insert(snap.inventory.end(),
                               e.items.begin(), e.items.end());
        mergeQuestCounts(snap.hellfireTorch,   e.hellfire);
        mergeQuestCounts(snap.colossalAncients, e.colossal);
        mergeQuestCounts(snap.terrorZones,     e.terror);
        for (const auto& [id, loc] : e.chronUniqueLocs) uniqueLocations.try_emplace(id, loc);
        for (const auto& [id, loc] : e.chronSetLocs)    setLocations.try_emplace(id, loc);
        for (const auto& [id, loc] : e.ownedUniqueLocs) ownedUniqueLocs.try_emplace(id, loc);
        for (const auto& [id, loc] : e.ownedSetLocs)    ownedSetLocs.try_emplace(id, loc);
    }

    if (haveBest) {
        snap.hasActivePlayer = true;
        auto& ap = snap.activePlayer;
        ap.name           = bestChar.name;
        ap.characterClass = bestChar.characterClass;
        ap.level          = bestChar.attributes.level ? bestChar.attributes.level
                                                      : bestChar.level;
        ap.experience     = bestChar.attributes.experience;
        ap.timestamp      = bestChar.timestamp;
        ap.hardcore       = bestChar.hardcore;
        ap.died           = bestChar.died;
        ap.mapSeed        = bestChar.mapId;
        computeDifficulty(bestChar, ap.difficulty, ap.act);

        // Progress within the current level. `expForLevel` is derived
        // from the compiled-in D2 experience table (see kExpToReachLevel).
        // If the character's raw exp exceeds table[level+1], we don't
        // clamp: the caller shows a "table-stale" indicator so the user
        // knows the percentage may be inaccurate. This matters at levels
        // 97-99 where D2R has revised the curve away from classic D2.
        const std::uint32_t lvl = ap.level;
        const std::uint64_t curFloor  = experienceToReachLevel(lvl);
        const std::uint64_t nextFloor = experienceToReachLevel(lvl + 1);
        if (nextFloor > curFloor) {
            ap.expForLevel = nextFloor - curFloor;
            ap.expInLevel  = ap.experience > curFloor
                             ? ap.experience - curFloor : 0;
            ap.expPercent  = 100.0 * static_cast<double>(ap.expInLevel)
                                    / static_cast<double>(ap.expForLevel);
        } else {
            ap.expForLevel = 0;
            ap.expInLevel  = 0;
            ap.expPercent  = 100.0;
        }
    }

    // Build the chronicle table.
    const auto& tierByCode = *cache.tierByCode;
    auto tierOf = [&](std::string_view code) -> ChronicleTier {
        if (code.empty()) return ChronicleTier::None;
        auto it = tierByCode.find(std::string(code));
        return it == tierByCode.end() ? ChronicleTier::None : it->second;
    };
    const auto& familyByCode = *cache.familyByCode;
    auto familyOf = [&](std::string_view code) -> std::string {
        if (code.empty()) return {};
        auto it = familyByCode.find(std::string(code));
        return it == familyByCode.end() ? std::string{} : it->second;
    };
    const auto& typeByCode = *cache.typeByCode;
    auto typeOf = [&](std::string_view code) -> std::string {
        if (code.empty()) return {};
        auto it = typeByCode.find(std::string(code));
        return it == typeByCode.end() ? std::string{} : it->second;
    };
    const auto& levelByCode = *cache.levelByCode;
    auto levelOf = [&](std::string_view code) -> int {
        if (code.empty()) return 0;
        auto it = levelByCode.find(std::string(code));
        return it == levelByCode.end() ? 0 : it->second;
    };

    // Chronicle rows treat sunder-charm sibling ids as equivalent (owning
    // or discovering a Renewed variant satisfies its Latent pair and vice
    // versa; see include/d2r/SunderCharms.hpp).
    auto discoveredUnique = [&](std::uint32_t id) {
        if (foundUniqueIds.contains(id)) return true;
        const auto pair = sunderPairedId(id);
        return pair != 0 && foundUniqueIds.contains(pair);
    };
    auto ownedUniqueId = [&](std::uint32_t id) {
        if (ownedUniqueLocs.contains(id)) return true;
        const auto pair = sunderPairedId(id);
        return pair != 0 && ownedUniqueLocs.contains(pair);
    };
    auto ownedUniqueLocation = [&](std::uint32_t id) -> std::string {
        if (auto it = ownedUniqueLocs.find(id); it != ownedUniqueLocs.end())
            return it->second;
        const auto pair = sunderPairedId(id);
        if (pair != 0) {
            if (auto it = ownedUniqueLocs.find(pair); it != ownedUniqueLocs.end())
                return it->second + " (paired)";
        }
        return {};
    };

    // Uniques.
    {
        // Rainbow Facet variants (uniqueids 392..399) all share the
        // display name "Rainbow Facet" but differ by element proc +
        // trigger. Bake a distinguishing suffix into displayName so
        // every renderer (chronicle, reconcile, search) can tell them
        // apart without new plumbing. Derived from the game data
        // (data/sql/08_uniqueitems.sql:485-492) which is fixed at
        // 8 combinations of {Lightning,Cold,Fire,Poison} x {Death,
        // Level-Up}.
        auto rainbowFacetSuffix = [](std::uint32_t id) -> const char* {
            switch (id) {
                case 392: return "Lightning (Death)";
                case 393: return "Cold (Death)";
                case 394: return "Fire (Death)";
                case 395: return "Poison (Death)";
                case 396: return "Lightning (Level-Up)";
                case 397: return "Cold (Level-Up)";
                case 398: return "Fire (Level-Up)";
                case 399: return "Poison (Level-Up)";
                default:  return nullptr;
            }
        };

        auto st = db.prepare(
            "SELECT t.id, "
            "       COALESCE(inm_idx.en_us, t.\"index\") AS display_name, "
            "       COALESCE(inm.en_us, b.name)         AS base_name, "
            "       t.code AS base_code "
            "FROM uniqueitems t "
            "LEFT JOIN (SELECT code, name, namestr, quest FROM armor "
            "           UNION ALL SELECT code, name, namestr, quest FROM weapons "
            "           UNION ALL SELECT code, name, namestr, quest FROM misc) b "
            "  ON b.code = t.code "
            "LEFT JOIN item_names inm     ON inm.\"key\"     = b.namestr "
            "LEFT JOIN item_names inm_idx ON inm_idx.\"key\" = t.\"index\" "
            "WHERE t.id IS NOT NULL AND t.id != '' "
            "AND CAST(t.spawnable AS INT)=1 "
            "AND (t.disablechronicle IS NULL OR t.disablechronicle != '1') "
            "AND (t.disabled IS NULL OR t.disabled != '1') "
            "AND (b.quest IS NULL OR b.quest = '') "
            "ORDER BY base_name COLLATE NOCASE, t.\"index\" COLLATE NOCASE");
        while (st.step()) {
            ChronicleRow r;
            r.kind        = ChronicleKind::Unique;
            r.id          = static_cast<std::uint32_t>(st.columnInt64(0));
            r.displayName = st.columnText(1);
            if (const char* suf = rainbowFacetSuffix(r.id)) {
                r.displayName += " \xE2\x80\x94 ";   // em dash
                r.displayName += suf;
            }
            r.baseName    = st.columnText(2);
            const auto baseCode = st.columnText(3);
            r.baseCode    = baseCode;
            r.tier        = tierOf(baseCode);
            r.familyKey   = familyOf(baseCode);
            // Group families by the item-class type of their Normal-
            // tier sibling so every member of the Crystal Sword /
            // Dimensional Blade / Phase Blade family reports as
            // "swor" (Swords). Falls back to the row's own type when
            // the family key is missing.
            r.familyType  = typeOf(!r.familyKey.empty() ? r.familyKey : baseCode);
            r.familyLevel = levelOf(!r.familyKey.empty() ? r.familyKey : baseCode);
            r.discovered  = discoveredUnique(r.id);
            if (auto it = uniqueLocations.find(r.id); it != uniqueLocations.end()) {
                r.location = it->second;
            }
            snap.chronicle.push_back(std::move(r));
        }
    }

    // Sets.
    {
        auto st = db.prepare(
            "SELECT t.id, "
            "       COALESCE(inm_idx.en_us, t.\"index\") AS display_name, "
            "       COALESCE(inm.en_us, b.name)         AS base_name, "
            "       t.item AS base_code, "
            "       COALESCE(inm_set.en_us, t.\"set\") AS set_name "
            "FROM setitems t "
            "LEFT JOIN (SELECT code, name, namestr, quest FROM armor "
            "           UNION ALL SELECT code, name, namestr, quest FROM weapons "
            "           UNION ALL SELECT code, name, namestr, quest FROM misc) b "
            "  ON b.code = t.item "
            "LEFT JOIN item_names inm     ON inm.\"key\"     = b.namestr "
            "LEFT JOIN item_names inm_idx ON inm_idx.\"key\" = t.\"index\" "
            "LEFT JOIN item_names inm_set ON inm_set.\"key\" = t.\"set\" "
            "WHERE t.id IS NOT NULL AND t.id != '' "
            "AND CAST(t.spawnable AS INT)=1 "
            "AND (t.disablechronicle IS NULL OR t.disablechronicle != '1') "
            "AND (t.disabled IS NULL OR t.disabled != '1') "
            "AND (b.quest IS NULL OR b.quest = '') "
            "ORDER BY set_name COLLATE NOCASE, base_name COLLATE NOCASE, "
            "         t.\"index\" COLLATE NOCASE");
        while (st.step()) {
            ChronicleRow r;
            r.kind        = ChronicleKind::Set;
            r.id          = static_cast<std::uint32_t>(st.columnInt64(0));
            r.displayName = st.columnText(1);
            r.baseName    = st.columnText(2);
            r.tier        = tierOf(st.columnText(3));
            r.setName     = st.columnText(4);
            r.discovered  = foundSetIds.contains(r.id);
            if (auto it = setLocations.find(r.id); it != setLocations.end()) {
                r.location = it->second;
            }
            snap.chronicle.push_back(std::move(r));
        }
    }

    // Reconcile: emit one row per unique/set id where owned != discovered.
    // Sunder-charm sibling ids share their state so a Renewed variant
    // satisfies the chronicle's Latent entry and vice versa. Names come
    // from the chronicle table when possible; otherwise from a direct
    // RefDb catalog lookup, then finally a synthesised placeholder.
    {
        std::unordered_map<std::uint32_t, const ChronicleRow*> uniqueRow, setRow;
        for (const auto& r : snap.chronicle) {
            if (r.kind == ChronicleKind::Unique) uniqueRow.emplace(r.id, &r);
            else if (r.kind == ChronicleKind::Set) setRow.emplace(r.id, &r);
        }

        auto lookupName = [&](ChronicleKind kind, std::uint32_t id) {
            const auto& m = kind == ChronicleKind::Unique ? uniqueRow : setRow;
            if (auto it = m.find(id); it != m.end()) {
                return std::pair<std::string, std::string>{
                    it->second->displayName, it->second->baseName};
            }
            if (kind == ChronicleKind::Unique) {
                if (const auto* row = db.lookupUnique(
                        static_cast<std::uint16_t>(id))) {
                    return std::pair<std::string, std::string>{
                        row->index, lookupBaseName(db, row->code)};
                }
                return std::pair<std::string, std::string>{
                    "Unique #" + std::to_string(id), std::string{}};
            }
            if (const auto* row = db.lookupSetItem(
                    static_cast<std::uint16_t>(id))) {
                return std::pair<std::string, std::string>{
                    row->index, lookupBaseName(db, row->code)};
            }
            return std::pair<std::string, std::string>{
                "Set #" + std::to_string(id), std::string{}};
        };

        // Collect the union of ids that appear on either side, then diff.
        // Sunder-charm pairing folds one variant into the other so we
        // don't emit both halves of a pair as separate rows.
        auto sunderCanonical = [](std::uint32_t id) {
            const auto pair = sunderPairedId(id);
            // Fold to the smaller id for deterministic canonicalisation.
            return pair != 0 && pair < id ? pair : id;
        };

        // Uniques.
        std::unordered_map<std::uint32_t, std::uint32_t> uniqueTimestamps;
        for (const auto& e : chron.uniques) {
            uniqueTimestamps[sunderCanonical(e.itemId)] = e.timestampMinutes;
        }

        std::unordered_set<std::uint32_t> uniqueIds;
        for (const auto& [id, _] : ownedUniqueLocs) uniqueIds.insert(sunderCanonical(id));
        for (const auto& e : chron.uniques)         uniqueIds.insert(sunderCanonical(e.itemId));

        for (auto id : uniqueIds) {
            const bool o = ownedUniqueId(id);
            const bool d = discoveredUnique(id);
            if (o == d) continue;   // no discrepancy
            auto [name, base] = lookupName(ChronicleKind::Unique, id);
            ReconcileEntry entry;
            entry.kind        = ChronicleKind::Unique;
            entry.id          = id;
            entry.displayName = std::move(name);
            entry.baseName    = std::move(base);
            entry.location    = ownedUniqueLocation(id);
            entry.owned       = o;
            entry.discovered  = d;
            if (auto it = uniqueTimestamps.find(id); it != uniqueTimestamps.end()) {
                entry.chronicledAtMinutes = it->second;
            }
            snap.reconcile.push_back(std::move(entry));
        }

        // Sets (no sunder-charm concept applies; straightforward diff).
        std::unordered_map<std::uint32_t, std::uint32_t> setTimestamps;
        for (const auto& e : chron.setItems) setTimestamps[e.itemId] = e.timestampMinutes;

        std::unordered_set<std::uint32_t> setIds;
        for (const auto& [id, _] : ownedSetLocs) setIds.insert(id);
        for (const auto& e : chron.setItems)    setIds.insert(e.itemId);

        for (auto id : setIds) {
            const bool o = ownedSetLocs.contains(id);
            const bool d = foundSetIds.contains(id);
            if (o == d) continue;
            auto [name, base] = lookupName(ChronicleKind::Set, id);
            ReconcileEntry entry;
            entry.kind        = ChronicleKind::Set;
            entry.id          = id;
            entry.displayName = std::move(name);
            entry.baseName    = std::move(base);
            if (auto it = ownedSetLocs.find(id); it != ownedSetLocs.end()) {
                entry.location = it->second;
            }
            entry.owned      = o;
            entry.discovered = d;
            if (auto it = setTimestamps.find(id); it != setTimestamps.end()) {
                entry.chronicledAtMinutes = it->second;
            }
            snap.reconcile.push_back(std::move(entry));
        }
    }

    return snap;
}

DashboardSnapshot buildSnapshot(RefDb&                       db,
                                const std::filesystem::path& saveDir,
                                const std::filesystem::path& stashPath) {
    // Compat wrapper: throwaway cache, full scan, aggregate. Preserves
    // existing tests + non-inotify builds. The dashboard hot path uses
    // the persistent-cache API above (refresh* + aggregate*) directly.
    DashboardFileCache tmp;
    refreshDashboardCacheFromDirectory(db, saveDir, stashPath, tmp);
    return aggregateDashboardSnapshot(db, tmp);
}

// ---------------------------------------------------------------------------
// Session anchor: override the character-side portion of an existing
// snapshot with data parsed from raw .d2s bytes. Non-character data
// (shared stash, chronicle, quests) is left in place, so the anchor
// still reflects a coherent world state; only the .d2s owner's stats
// and character-side items are rewound to the backup moment.
// ---------------------------------------------------------------------------
bool overrideActivePlayerFromBytes(DashboardSnapshot&         snap,
                                   RefDb&                     db,
                                   std::span<const std::byte> characterBytes,
                                   std::string_view           filename) {
    Character ch;
    try {
        ch = parseCharacter(characterBytes);
    } catch (const std::exception&) {
        return false;
    }

    // Wipe existing character-side items in the snapshot's inventory.
    // The location prefix is the filename (e.g. "Kai.d2s"), optionally
    // followed by " (merc)", " (corpse)", or " (iron golem)"; that
    // matches everything we're about to re-emit from the backup bytes.
    const std::string filenameStr(filename);
    snap.inventory.erase(
        std::remove_if(snap.inventory.begin(), snap.inventory.end(),
            [&](const InventoryItem& inv) {
                return inv.location.size() >= filenameStr.size() &&
                       inv.location.compare(0, filenameStr.size(),
                                             filenameStr) == 0;
            }),
        snap.inventory.end());

    // Repopulate character-side items from the parsed bytes.
    if (ch.itemsOffset != 0) {
        ItemParser p(db);
        auto push = [&](const std::vector<Item>& items,
                        const std::string& loc,
                        const std::string& subLocOverride) {
            for (const auto& it : items) {
                const std::string subLoc = subLocOverride.empty()
                    ? characterItemSubLoc(it) : subLocOverride;
                InventoryItem inv;
                inv.name        = primaryName(db, it);
                inv.baseName    = lookupBaseName(db, it.code);
                inv.code        = it.code;
                inv.location    = loc;
                inv.subLocation = subLoc;
                inv.quality     = it.quality;
                inv.fingerprint = it.fingerprint;
                inv.stacks       = it.stacks;
                inv.hasStackSlot = it.hasStackSlot;
                inv.identified  = it.identified;
                snap.inventory.push_back(std::move(inv));
                for (const auto& s : it.socketedItems) {
                    InventoryItem sInv;
                    sInv.name        = primaryName(db, s);
                    sInv.baseName    = lookupBaseName(db, s.code);
                    sInv.code        = s.code;
                    sInv.location    = loc;
                    sInv.subLocation = subLoc;   // inherit parent's bucket
                    sInv.quality     = s.quality;
                    sInv.fingerprint = s.fingerprint;
                    sInv.stacks       = s.stacks;
                    sInv.hasStackSlot = s.hasStackSlot;
                    sInv.identified  = s.identified;
                    sInv.socketed    = true;
                    snap.inventory.push_back(std::move(sInv));
                }
            }
        };
        try {
            push(p.parseItems(characterBytes, ch.itemsOffset), filenameStr, "");
            if (ch.mercItemsJMOffset) {
                push(p.parseItems(characterBytes, ch.mercItemsJMOffset),
                     filenameStr + " (merc)", "Merc");
            }
            if (ch.corpseJMOffset && ch.corpseItemCount == 1) {
                push(p.parseItems(characterBytes, ch.corpseJMOffset + 16),
                     filenameStr + " (corpse)", "Corpse");
            }
            if (ch.hasIronGolem && ch.ironGolemItemOffset) {
                try {
                    std::vector<Item> golem{
                        p.parseSingleItem(characterBytes, ch.ironGolemItemOffset)};
                    push(golem, filenameStr + " (iron golem)", "Iron Golem");
                } catch (const std::exception&) {}
            }
        } catch (const std::exception&) {
            // Partial item parse: keep whatever pushed successfully.
        }
    }

    // Overwrite the ActivePlayer summary. `expInLevel`/`expForLevel`
    // are recomputed here so callers get a consistent view without
    // needing to know the D2R experience table.
    snap.hasActivePlayer = true;
    auto& ap = snap.activePlayer;
    ap.file           = filenameStr;
    ap.name           = ch.name;
    ap.characterClass = ch.characterClass;
    ap.level          = ch.attributes.level ? ch.attributes.level : ch.level;
    ap.experience     = ch.attributes.experience;
    ap.timestamp      = ch.timestamp;
    ap.hardcore       = ch.hardcore;
    ap.died           = ch.died;
    ap.mapSeed        = ch.mapId;
    computeDifficulty(ch, ap.difficulty, ap.act);

    const std::uint32_t lvl       = ap.level;
    const std::uint64_t curFloor  = experienceToReachLevel(lvl);
    const std::uint64_t nextFloor = experienceToReachLevel(lvl + 1);
    if (nextFloor > curFloor) {
        ap.expForLevel = nextFloor - curFloor;
        ap.expInLevel  = ap.experience > curFloor
                         ? ap.experience - curFloor : 0;
        ap.expPercent  = 100.0 * static_cast<double>(ap.expInLevel)
                                / static_cast<double>(ap.expForLevel);
    } else {
        ap.expForLevel = 0;
        ap.expInLevel  = 0;
        ap.expPercent  = 100.0;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Session anchor (part 2): swap the shared-stash items in a snapshot for
// items parsed from a specific .d2i byte buffer, so a Session anchor can
// represent the stash as it was at a past moment in time (e.g. when the
// backup that seeded the character-side override was taken).
// ---------------------------------------------------------------------------
bool overrideSharedStashFromBytes(DashboardSnapshot&         snap,
                                  RefDb&                     db,
                                  std::span<const std::byte> stashBytes) {
    static const std::string kStashPrefix = "stash tab ";

    SharedStash parsed;
    try {
        SharedStashParser sp(db);
        parsed = sp.parse(stashBytes);
    } catch (const std::exception&) {
        return false;
    }

    // Drop every existing stash-side inventory item; the parsed bytes
    // are the authoritative replacement.
    snap.inventory.erase(
        std::remove_if(snap.inventory.begin(), snap.inventory.end(),
            [&](const InventoryItem& inv) {
                return inv.location.size() >= kStashPrefix.size() &&
                       inv.location.compare(0, kStashPrefix.size(),
                                             kStashPrefix) == 0;
            }),
        snap.inventory.end());

    for (std::size_t i = 0; i < parsed.tabs.size(); ++i) {
        const std::string loc = kStashPrefix + std::to_string(i + 1);
        for (const auto& it : parsed.tabs[i].items) {
            InventoryItem inv;
            inv.name        = primaryName(db, it);
            inv.baseName    = lookupBaseName(db, it.code);
            inv.code        = it.code;
            inv.location    = loc;
            inv.quality     = it.quality;
            inv.fingerprint = it.fingerprint;
            inv.stacks       = it.stacks;
            inv.hasStackSlot = it.hasStackSlot;
            inv.identified  = it.identified;
            snap.inventory.push_back(std::move(inv));
            for (const auto& s : it.socketedItems) {
                InventoryItem sInv;
                sInv.name        = primaryName(db, s);
                sInv.baseName    = lookupBaseName(db, s.code);
                sInv.code        = s.code;
                sInv.location    = loc;
                sInv.quality     = s.quality;
                sInv.fingerprint = s.fingerprint;
                sInv.stacks       = s.stacks;
                sInv.hasStackSlot = s.hasStackSlot;
                sInv.identified  = s.identified;
                sInv.socketed    = true;
                snap.inventory.push_back(std::move(sInv));
            }
        }
    }
    return true;
}

void clearSharedStashInSnapshot(DashboardSnapshot& snap) {
    static const std::string kStashPrefix = "stash tab ";
    snap.inventory.erase(
        std::remove_if(snap.inventory.begin(), snap.inventory.end(),
            [&](const InventoryItem& inv) {
                return inv.location.size() >= kStashPrefix.size() &&
                       inv.location.compare(0, kStashPrefix.size(),
                                             kStashPrefix) == 0;
            }),
        snap.inventory.end());
}

// Reduce a fully-populated DashboardSnapshot to the small
// SessionState the pane actually reads. Kept as a pure function so
// the ftxui layer can call it after applying the character + stash
// byte overrides to a working snapshot.
SessionState makeSessionStateFromSnapshot(const DashboardSnapshot& snap) {
    SessionState out;
    out.hasActivePlayer = snap.hasActivePlayer;
    if (snap.hasActivePlayer) {
        out.playerName   = snap.activePlayer.name;
        out.playerClass  = snap.activePlayer.characterClass;
        out.level        = snap.activePlayer.level;
        out.expInLevel   = snap.activePlayer.expInLevel;
    }
    // Pre-computed lookup set: identified Unique/Set items with a
    // non-zero fingerprint. Renderer's diff loop just probes contains().
    out.itemKeys.reserve(snap.inventory.size() / 8 + 16);
    for (const auto& it : snap.inventory) {
        if (!it.identified) continue;
        if (it.quality != ItemQuality::Unique && it.quality != ItemQuality::Set)
            continue;
        if (it.fingerprint == 0) continue;
        out.itemKeys.insert({it.fingerprint, it.quality});
    }

    // Rune counts per base code -- summed via `effectiveStackCount`
    // so a material-tab pile of 99 Amn Runes contributes 99, a loose
    // rune on a character contributes 1, and an empty material-tab
    // slot (r33 with stacks==0, hasStackSlot==true) contributes 0.
    // Session Loot pane subtracts these from the current snapshot's
    // per-code counts to surface positive deltas.
    // Filter: code begins with 'r' and is exactly 3 chars ("r01".."r33").
    for (const auto& it : snap.inventory) {
        if (it.code.size() == 3 && it.code[0] == 'r'
            && it.code[1] >= '0' && it.code[1] <= '9'
            && it.code[2] >= '0' && it.code[2] <= '9') {
            out.runeStacks[it.code] += effectiveStackCount(it);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Run grouping.
// ---------------------------------------------------------------------------
// `historyRows` comes from BackupDb::historyFor which returns rows
// newest-first (date DESC). We walk oldest-first so a Run accumulates
// its Autosave dates before landing its closing SaveAndExit -- the
// same shape the renderer wants (autosaves appear before the run-end
// row when the run is expanded).
//
// Rows with `date < sessionStart` are skipped: they belong to a
// previous session and don't count. `sessionStart == 0` disables the
// clip, giving the caller the file's full run history (used by the
// Backups pane).
//
// A run's `startEpoch` is either the previous SaveAndExit row's date
// or `sessionStart`, whichever is later. So a run that straddles the
// session boundary has its left bound clipped -- matching the user's
// definition "run start is either the previous run's end or the
// session start" (per the runs-vs-sessions plan).
//
// Trailing Autosave / Other rows (newer than the newest SaveAndExit)
// emit as one in-progress Run with `endEpoch = 0` and
// `inProgress = true`. A file with no SaveAndExit at all in the
// window emits one such in-progress Run holding every retained row.
//
// Deleted / Startup rows are non-run signals and are ignored by the
// grouping. Callers that want to render tombstones or dashboard-
// startup markers should handle those separately, above the Run
// list.
std::vector<Run> groupRunsForFile(
    std::string_view                       characterFile,
    std::span<const BackupDb::HistoryRow>  historyRows,
    std::int64_t                           sessionStart) {
    std::vector<Run> out;
    if (historyRows.empty()) return out;

    // Left bound for the next run being built. Starts at sessionStart
    // and advances to the most recent SaveAndExit date as we walk.
    std::int64_t nextStart = sessionStart;

    // Accumulator for the currently-open run (autosaves + any Other
    // rows between the last SaveAndExit and either the next
    // SaveAndExit or end-of-history).
    Run acc;
    acc.characterFile = std::string(characterFile);
    acc.startEpoch    = nextStart;
    acc.inProgress    = true;   // stays true unless we hit a SaveAndExit

    // Walk oldest-first: iterate historyRows in reverse.
    for (auto it = historyRows.rbegin(); it != historyRows.rend(); ++it) {
        const auto& r = *it;
        if (r.date < sessionStart) continue;
        switch (r.state) {
            case BackupDb::State::Autosave:
            case BackupDb::State::Other:
                acc.autosaveDates.push_back(r.date);
                ++acc.autosaveCount;
                break;
            case BackupDb::State::SaveAndExit:
                // Close the current run.
                acc.endEpoch   = r.date;
                acc.inProgress = false;
                out.push_back(std::move(acc));
                // Open a new accumulator: left bound = this SaveAndExit's date.
                nextStart = r.date;
                acc = Run{};
                acc.characterFile = std::string(characterFile);
                acc.startEpoch    = nextStart;
                acc.inProgress    = true;
                break;
            case BackupDb::State::Deleted:
            case BackupDb::State::Startup:
                // Non-run signals; ignore in the grouping. The Backups
                // pane renders these above/below the Run list rather
                // than inside a run.
                break;
        }
    }

    // Emit any trailing in-progress run. Skip if it has no autosaves
    // AND the walk never opened it past the seed left bound (no rows
    // survived the sessionStart clip). The `autosaveCount > 0` guard
    // avoids emitting a phantom empty run for a file whose latest
    // recorded event was a SaveAndExit (its accumulator has zero
    // autosaves and no closing SaveAndExit -- the previous iteration
    // already closed the last real run).
    if (acc.autosaveCount > 0) {
        out.push_back(std::move(acc));
    }
    return out;
}

// ---------------------------------------------------------------------------
// runDurationSecs / computeSessionRunStats.
// ---------------------------------------------------------------------------

std::int64_t runDurationSecs(const Run& run, std::int64_t sessionEnd) noexcept {
    // A run spans from `startEpoch` (previous SaveAndExit's date OR
    // the session start, whichever is later -- set by
    // `groupRunsForFile`) to either its closing SaveAndExit
    // (`endEpoch`, closed runs) or the session end (in-progress
    // runs).
    //
    // For in-progress runs, `sessionEnd` tracks the newest backup on
    // record (via AppSession::autoEndEpoch). When the caller doesn't
    // supply one (sessionEnd == 0, e.g. bare unit tests), we fall
    // back to the run's last known autosave date so the return value
    // stays meaningful. If neither is available, duration is 0.
    std::int64_t end = 0;
    if (run.inProgress) {
        if (sessionEnd > run.startEpoch) {
            end = sessionEnd;
        } else if (!run.autosaveDates.empty()) {
            end = run.autosaveDates.back();
        } else {
            return 0;
        }
    } else {
        end = run.endEpoch;
    }
    return (end > run.startEpoch) ? (end - run.startEpoch) : 0;
}

// Strip a trailing ".d2s" / ".D2S" from `name` to produce a display
// stem. Case-insensitive; returns `name` unchanged if no such suffix.
static std::string stripD2sSuffix(std::string name) {
    if (name.size() < 4) return name;
    const auto tail = name.substr(name.size() - 4);
    if (tail == ".d2s" || tail == ".D2S") name.resize(name.size() - 4);
    return name;
}

SessionRunStats computeSessionRunStats(
    BackupDb*                 backupDb,
    const DashboardFileCache& cache,
    std::int64_t              sessionStart,
    std::int64_t              sessionEnd) {
    SessionRunStats out;
    if (!backupDb) return out;

    for (const auto& [filename, entry] : cache.d2s) {
        std::vector<BackupDb::HistoryRow> hist;
        try { hist = backupDb->historyFor(filename, 500); }
        catch (const std::exception&) { continue; }
        if (hist.empty()) continue;

        auto runs = groupRunsForFile(filename, hist, sessionStart);

        SessionRunStats::PerCharacter pc;
        pc.characterFile = filename;
        pc.characterName = entry.character.name.empty()
                             ? stripD2sSuffix(filename)
                             : entry.character.name;
        for (const auto& r : runs) {
            // Upper-bound clip: skip runs whose activity lies entirely
            // past the session end. For closed runs the SaveAndExit
            // date is the natural cutoff; for in-progress the OLDEST
            // autosave in the run must be within the window (a run
            // that started after the custom end doesn't count).
            if (sessionEnd > 0) {
                if (!r.inProgress && r.endEpoch > sessionEnd) continue;
                if (r.inProgress
                    && !r.autosaveDates.empty()
                    && r.autosaveDates.front() > sessionEnd) continue;
            }
            if (r.inProgress) {
                pc.hasInProgress = true;
            } else {
                ++pc.runCount;
            }
            pc.accumulatedSecs += runDurationSecs(r, sessionEnd);
        }
        if (pc.runCount == 0 && !pc.hasInProgress) continue;

        out.totalRuns     += pc.runCount;
        out.totalSecs     += pc.accumulatedSecs;
        if (pc.hasInProgress) out.anyInProgress = true;
        out.perCharacter.push_back(std::move(pc));
    }

    // Sort by accumulated wall-clock time descending, then by
    // character name ascending for ties. The Session Info pane
    // renders in this order.
    std::sort(out.perCharacter.begin(), out.perCharacter.end(),
              [](const SessionRunStats::PerCharacter& a,
                 const SessionRunStats::PerCharacter& b) {
                  if (a.accumulatedSecs != b.accumulatedSecs)
                      return a.accumulatedSecs > b.accumulatedSecs;
                  return a.characterName < b.characterName;
              });
    return out;
}

} // namespace d2r
