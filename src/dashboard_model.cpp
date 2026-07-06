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

// Return the tier of every base code (armor/weapons three-tier, misc = Misc).
std::unordered_map<std::string, ChronicleTier> loadTierMap(RefDb& db) {
    std::unordered_map<std::string, ChronicleTier> out;
    auto st = db.prepare(
        "SELECT code, normcode, ubercode, ultracode FROM armor "
        "UNION ALL "
        "SELECT code, normcode, ubercode, ultracode FROM weapons");
    while (st.step()) {
        const auto code = st.columnText(0);
        if (code.empty()) continue;
        const auto norm  = st.columnText(1);
        const auto uber  = st.columnText(2);
        const auto ultra = st.columnText(3);
        ChronicleTier t = ChronicleTier::None;
        if      (code == norm)  t = ChronicleTier::Normal;
        else if (code == uber)  t = ChronicleTier::Exceptional;
        else if (code == ultra) t = ChronicleTier::Elite;
        out.emplace(code, t);
    }
    auto stMisc = db.prepare("SELECT code FROM misc");
    while (stMisc.step()) {
        const auto code = stMisc.columnText(0);
        if (!code.empty()) out.emplace(code, ChronicleTier::Misc);
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

} // namespace

std::uint64_t experienceToReachLevel(std::uint32_t level) noexcept {
    if (level >= 100) return kExpToReachLevel[99];
    return kExpToReachLevel[level];
}

DashboardSnapshot buildSnapshot(RefDb& db,
                                const std::filesystem::path& saveDir,
                                const std::filesystem::path& stashPath) {
    DashboardSnapshot snap;
    snap.refreshedAtEpoch = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    const auto tierByCode = loadTierMap(db);
    const auto collectableUniqueIds = loadCollectable(db, "uniqueitems", "code");
    const auto collectableSetIds    = loadCollectable(db, "setitems",    "item");

    // Chronicle-tab entries from the shared stash (which the account has
    // already "collected"). Location string is left blank; the per-.d2s
    // scan below overlays a location for still-held items. The full
    // ChronicleTab is retained for the reconcile diff below (needs the
    // per-entry timestamps).
    std::unordered_set<std::uint32_t> foundUniqueIds, foundSetIds;
    ChronicleTab chron;
    if (!stashPath.empty()) {
        try {
            const auto bytes = readFile(stashPath.string());
            SharedStashParser sp(db);
            chron = sp.parseChronicleOnly(bytes);
            for (const auto& e : chron.uniques)  foundUniqueIds.insert(e.itemId);
            for (const auto& e : chron.setItems) foundSetIds.insert(e.itemId);

            // Storage-tab items (keys, torches, shards, statues, cjw).
            const auto full = sp.parse(bytes);
            for (std::size_t i = 0; i < full.tabs.size(); ++i) {
                const std::string loc = "stash tab " + std::to_string(i + 1);
                for (const auto& it : full.tabs[i].items) {
                    countQuestItem(it, snap.hellfireTorch,
                                   snap.colossalAncients, snap.terrorZones);
                    InventoryItem inv;
                    inv.name     = primaryName(db, it);
                    inv.baseName = lookupBaseName(db, it.code);
                    inv.location = loc;
                    inv.quality  = it.quality;
                    snap.inventory.push_back(std::move(inv));
                    for (const auto& s : it.socketedItems) {
                        countQuestItem(s, snap.hellfireTorch,
                                       snap.colossalAncients, snap.terrorZones);
                        InventoryItem sInv;
                        sInv.name     = primaryName(db, s);
                        sInv.baseName = lookupBaseName(db, s.code);
                        sInv.location = loc;
                        sInv.quality  = s.quality;
                        snap.inventory.push_back(std::move(sInv));
                    }
                }
            }
        } catch (const std::exception&) {
            // Ignored: absent / corrupt stash leaves quest counts and
            // chronicle sets at zero.
        }
    }

    // Character walk: pick the newest-by-timestamp for the ActivePlayer
    // slot, count quest items across every inventory, and record ownership
    // locations for chronicle rows.
    std::unordered_map<std::uint32_t, std::string> uniqueLocations;
    std::unordered_map<std::uint32_t, std::string> setLocations;
    // Reconcile: track every owned unique / set id + first-seen location,
    // regardless of collectable-catalog membership. Used below to compute
    // the OwnedNotChronicled and ChronicledNotOwned lists.
    std::unordered_map<std::uint32_t, std::string> ownedUniqueLocs;
    std::unordered_map<std::uint32_t, std::string> ownedSetLocs;

    // Record ownership for a single parsed Item into the reconcile maps.
    // Socketed items (uniques like Rainbow Facet, or Defender's Fire type
    // Colossal Jewels commonly slotted into armour) are just as much
    // "owned" as the container -- reconcile has to see them or it'll
    // report a false "discovered, not owned" discrepancy. Mirrors the
    // recursion in `collectOwned` in src/main.cpp.
    //
    // Note: no `id != 0` guard. Both `uniqueitems.txt` and `setitems.txt`
    // use id 0 for legitimate rows (The Gnasher, Civerb's Ward). The
    // ItemQuality check is sufficient to know the id field is meaningful.
    auto recordOwnership = [&](const Item& it, const std::string& loc) {
        if (it.quality == ItemQuality::Unique) {
            ownedUniqueLocs.try_emplace(it.uniqueId, loc);
        } else if (it.quality == ItemQuality::Set) {
            ownedSetLocs.try_emplace(it.setItemId, loc);
        }
    };

    // Extend the stash-tab pass above: capture ownership for uniques/sets
    // in stash tabs too. We do this in a second walk over `full.tabs` to
    // keep the change local; parse cost is negligible relative to the
    // characters below.
    if (!stashPath.empty()) {
        try {
            const auto bytes = readFile(stashPath.string());
            SharedStashParser sp(db);
            const auto full = sp.parse(bytes);
            for (std::size_t i = 0; i < full.tabs.size(); ++i) {
                const std::string loc = "stash tab " + std::to_string(i + 1);
                for (const auto& it : full.tabs[i].items) {
                    recordOwnership(it, loc);
                    for (const auto& s : it.socketedItems) recordOwnership(s, loc);
                }
            }
        } catch (const std::exception&) {}
    }

    Character bestChar;
    bool haveBest = false;

    if (!saveDir.empty()) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(saveDir, ec)) {
            if (ec) break;
            if (entry.path().extension() != ".d2s") continue;
            try {
                const auto bytes = readFile(entry.path().string());
                const auto ch    = parseCharacter(bytes);

                if (!haveBest || ch.timestamp > bestChar.timestamp) {
                    bestChar = ch;
                    snap.activePlayer.file = entry.path().filename().string();
                    haveBest = true;
                }

                if (ch.itemsOffset == 0) continue;
                ItemParser p(db);
                const auto file = entry.path().filename().string();

                auto record = [&](const std::vector<Item>& items,
                                  const std::string& loc) {
                    for (const auto& it : items) {
                        countQuestItem(it, snap.hellfireTorch,
                                       snap.colossalAncients, snap.terrorZones);
                        InventoryItem inv;
                        inv.name     = primaryName(db, it);
                        inv.baseName = lookupBaseName(db, it.code);
                        inv.location = loc;
                        inv.quality  = it.quality;
                        snap.inventory.push_back(std::move(inv));

                        if (it.quality == ItemQuality::Unique &&
                            collectableUniqueIds.contains(it.uniqueId)) {
                            uniqueLocations.emplace(it.uniqueId, loc);
                        } else if (it.quality == ItemQuality::Set &&
                                   collectableSetIds.contains(it.setItemId)) {
                            setLocations.emplace(it.setItemId, loc);
                        }
                        // Reconcile tracking (independent of collectable-catalog
                        // membership so genuinely off-catalog owns still surface).
                        recordOwnership(it, loc);

                        for (const auto& s : it.socketedItems) {
                            countQuestItem(s, snap.hellfireTorch,
                                           snap.colossalAncients, snap.terrorZones);
                            InventoryItem sInv;
                            sInv.name     = primaryName(db, s);
                            sInv.baseName = lookupBaseName(db, s.code);
                            sInv.location = loc;
                            sInv.quality  = s.quality;
                            snap.inventory.push_back(std::move(sInv));
                            // Also track socketed uniques/sets (Rainbow Facets,
                            // Defender's Fire etc. are usually socketed).
                            recordOwnership(s, loc);
                        }
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
                        std::vector<Item> golem{
                            p.parseSingleItem(bytes, ch.ironGolemItemOffset)};
                        record(golem, file + " (iron golem)");
                    } catch (const std::exception&) {}
                }
            } catch (const std::exception&) {
                // Skip this .d2s; keep going.
            }
        }
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
    auto tierOf = [&](std::string_view code) -> ChronicleTier {
        if (code.empty()) return ChronicleTier::None;
        auto it = tierByCode.find(std::string(code));
        return it == tierByCode.end() ? ChronicleTier::None : it->second;
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
            r.baseName    = st.columnText(2);
            r.tier        = tierOf(st.columnText(3));
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
            "       t.\"set\" AS set_name "
            "FROM setitems t "
            "LEFT JOIN (SELECT code, name, namestr, quest FROM armor "
            "           UNION ALL SELECT code, name, namestr, quest FROM weapons "
            "           UNION ALL SELECT code, name, namestr, quest FROM misc) b "
            "  ON b.code = t.item "
            "LEFT JOIN item_names inm     ON inm.\"key\"     = b.namestr "
            "LEFT JOIN item_names inm_idx ON inm_idx.\"key\" = t.\"index\" "
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

} // namespace d2r
