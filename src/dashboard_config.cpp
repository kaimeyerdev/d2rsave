// Implementation of the pane-tree config model + SQLite persistence.
// See include/d2r/DashboardConfig.hpp.

#include "d2r/DashboardConfig.hpp"

#include "d2r/Paths.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace d2r {

namespace {

PaneNode makeLeaf(PaneType t, ChronicleCategory c) {
    PaneNode n;
    n.isSplit = false;
    n.config.type = t;
    n.config.category = c;
    return n;
}

PaneNode makeLeaf(PaneType t) {
    PaneNode n;
    n.isSplit = false;
    n.config.type = t;
    return n;
}

// Build a right-leaning vertical split chain from a list of leaves, so
// they render left-to-right in the given order.
PaneNode buildVerticalChain(std::vector<PaneNode>&& leaves) {
    if (leaves.empty()) {
        PaneNode blank;
        return blank;
    }
    // Fold from the tail so leaves[0] ends up as the leftmost pane.
    PaneNode acc = std::move(leaves.back());
    leaves.pop_back();
    while (!leaves.empty()) {
        PaneNode next;
        next.isSplit = true;
        next.direction = SplitDirection::Vertical;
        next.a = std::make_unique<PaneNode>(std::move(leaves.back()));
        next.b = std::make_unique<PaneNode>(std::move(acc));
        leaves.pop_back();
        acc = std::move(next);
    }
    return acc;
}

// Set every leaf's paneWeight in-place. Used by makeDefaultLayout to
// bias the top row toward a compact 12-row-equivalent share of the
// terminal height, matching the pre-refactor hardcoded top-row look.
void setLeafWeight(PaneNode& root, int w) {
    if (root.isSplit) {
        if (root.a) setLeafWeight(*root.a, w);
        if (root.b) setLeafWeight(*root.b, w);
        return;
    }
    root.config.paneWeight = std::max(1, w);
}

} // namespace

// ---- Constructors -----------------------------------------------------------

PaneNode makeDefaultLayout() {
    // Two-row layout: a top row of four configurable summary panes
    // stacked over the classic chronicle chain. Weights bias the split
    // roughly 12/40 vertically (the pre-refactor hardcoded top row was
    // 12 rows tall); leaves inside each row keep equal-width defaults.
    std::vector<PaneNode> top;
    top.push_back(makeLeaf(PaneType::Character));
    top.push_back(makeLeaf(PaneType::Uber));
    top.push_back(makeLeaf(PaneType::TerrorZone));
    top.push_back(makeLeaf(PaneType::SessionLoot));
    auto topRow = buildVerticalChain(std::move(top));
    setLeafWeight(topRow, 12);

    std::vector<PaneNode> bottom;
    bottom.push_back(makeLeaf(PaneType::Chronicle, ChronicleCategory::Sets));
    bottom.push_back(makeLeaf(PaneType::Chronicle, ChronicleCategory::UniquesAll));
    bottom.push_back(makeLeaf(PaneType::Inventory));
    auto bottomRow = buildVerticalChain(std::move(bottom));
    setLeafWeight(bottomRow, 40);

    PaneNode root;
    root.isSplit   = true;
    root.direction = SplitDirection::Horizontal;   // stacked (top / bottom)
    root.a = std::make_unique<PaneNode>(std::move(topRow));
    root.b = std::make_unique<PaneNode>(std::move(bottomRow));
    return root;
}

// ---- String conversions -----------------------------------------------------

std::string_view toString(PaneType t) {
    switch (t) {
        case PaneType::Blank:       return "blank";
        case PaneType::Chronicle:   return "chronicle";
        case PaneType::Inventory:   return "inventory";
        case PaneType::Reconcile:   return "reconcile";
        case PaneType::Backups:     return "backups";
        case PaneType::BackupLog:   return "backup_log";
        case PaneType::Session:     return "session";
        case PaneType::Character:   return "character";
        case PaneType::SessionLoot: return "session_loot";
        case PaneType::Uber:        return "uber";
        case PaneType::TerrorZone:  return "terror_zone";
    }
    return "blank";
}

std::string_view toString(BackupViewMode m) {
    return m == BackupViewMode::Detail ? "detail" : "summary";
}

std::string_view toString(ChronicleCategory c) {
    switch (c) {
        case ChronicleCategory::Sets:               return "sets";
        case ChronicleCategory::UniquesNormal:      return "uniques_normal";
        case ChronicleCategory::UniquesExceptional: return "uniques_exceptional";
        case ChronicleCategory::UniquesElite:       return "uniques_elite";
        case ChronicleCategory::UniquesMisc:        return "uniques_misc";
        case ChronicleCategory::UniquesAll:         return "uniques_all";
    }
    return "sets";
}

std::string_view toString(OwnershipFilter o) {
    switch (o) {
        case OwnershipFilter::All:             return "all";
        case OwnershipFilter::RemainingOnly:   return "remaining";
        case OwnershipFilter::DiscoveredOnly:  return "discovered";
    }
    return "all";
}

std::string_view toString(PaneSortKey k) {
    switch (k) {
        case PaneSortKey::Name:  return "name";
        case PaneSortKey::Base:  return "base";
        case PaneSortKey::Owned: return "owned";
        case PaneSortKey::Tier:  return "tier";
    }
    return "base";
}

std::string_view toString(SplitDirection d) {
    return d == SplitDirection::Vertical ? "vertical" : "horizontal";
}

std::string_view toString(ReconcileKindFilter k) {
    switch (k) {
        case ReconcileKindFilter::Both:        return "both";
        case ReconcileKindFilter::UniquesOnly: return "uniques";
        case ReconcileKindFilter::SetsOnly:    return "sets";
    }
    return "both";
}

std::string categoryLabel(ChronicleCategory c) {
    switch (c) {
        case ChronicleCategory::Sets:               return "Sets";
        case ChronicleCategory::UniquesNormal:      return "Uniques - Normal";
        case ChronicleCategory::UniquesExceptional: return "Uniques - Except.";
        case ChronicleCategory::UniquesElite:       return "Uniques - Elite";
        case ChronicleCategory::UniquesMisc:        return "Uniques - Misc";
        case ChronicleCategory::UniquesAll:         return "Uniques (all)";
    }
    return "Sets";
}

std::string paneTitle(const PaneConfig& c) {
    switch (c.type) {
        case PaneType::Blank:     return "(unconfigured)";
        case PaneType::Chronicle: return categoryLabel(c.category);
        case PaneType::Inventory: return "Inventory";
        case PaneType::Reconcile: {
            std::string t = "Reconcile";
            switch (c.reconcileKind) {
                case ReconcileKindFilter::Both:        break;
                case ReconcileKindFilter::UniquesOnly: t += " (uniques)"; break;
                case ReconcileKindFilter::SetsOnly:    t += " (sets)";    break;
            }
            return t;
        }
        case PaneType::Backups:
            if (c.backupViewMode == BackupViewMode::Detail && !c.selectedBackupFile.empty()) {
                return "Backups  " + c.selectedBackupFile;
            }
            return "Backups";
        case PaneType::BackupLog:   return "Backup Actions";
        case PaneType::Session:     return "Session";
        case PaneType::Character: {
            std::string t = "Character";
            if (!c.characterSelection.empty()) t += "  " + c.characterSelection;
            return t;
        }
        case PaneType::SessionLoot: return "Session Info";
        case PaneType::Uber:        return "Uber";
        case PaneType::TerrorZone:  return "Terror Zone";
    }
    return "(unconfigured)";
}

PaneType paneTypeFromString(std::string_view s) {
    if (s == "chronicle")    return PaneType::Chronicle;
    if (s == "inventory")    return PaneType::Inventory;
    if (s == "reconcile")    return PaneType::Reconcile;
    if (s == "backups")      return PaneType::Backups;
    if (s == "backup_log")   return PaneType::BackupLog;
    if (s == "session")      return PaneType::Session;
    if (s == "character")    return PaneType::Character;
    if (s == "session_loot") return PaneType::SessionLoot;
    if (s == "uber")         return PaneType::Uber;
    if (s == "terror_zone")  return PaneType::TerrorZone;
    return PaneType::Blank;
}

BackupViewMode backupViewModeFromString(std::string_view s) {
    return s == "detail" ? BackupViewMode::Detail : BackupViewMode::Summary;
}

ChronicleCategory chronicleCategoryFromString(std::string_view s) {
    if (s == "uniques_normal")      return ChronicleCategory::UniquesNormal;
    if (s == "uniques_exceptional") return ChronicleCategory::UniquesExceptional;
    if (s == "uniques_elite")       return ChronicleCategory::UniquesElite;
    if (s == "uniques_misc")        return ChronicleCategory::UniquesMisc;
    if (s == "uniques_all")         return ChronicleCategory::UniquesAll;
    return ChronicleCategory::Sets;
}

OwnershipFilter ownershipFromString(std::string_view s) {
    if (s == "remaining")  return OwnershipFilter::RemainingOnly;
    if (s == "discovered") return OwnershipFilter::DiscoveredOnly;
    return OwnershipFilter::All;
}

PaneSortKey sortKeyFromString(std::string_view s) {
    if (s == "name")  return PaneSortKey::Name;
    if (s == "owned") return PaneSortKey::Owned;
    if (s == "tier")  return PaneSortKey::Tier;
    return PaneSortKey::Base;
}

SplitDirection splitDirectionFromString(std::string_view s) {
    return s == "horizontal" ? SplitDirection::Horizontal
                             : SplitDirection::Vertical;
}

ReconcileKindFilter reconcileKindFromString(std::string_view s) {
    if (s == "uniques") return ReconcileKindFilter::UniquesOnly;
    if (s == "sets")    return ReconcileKindFilter::SetsOnly;
    return ReconcileKindFilter::Both;
}

// ---- Persistence ------------------------------------------------------------

sqlite3* openDashboardConfigDb() {
    const auto path = d2r::dashboardConfigDbPath().string();
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
        std::string err = db ? sqlite3_errmsg(db) : "sqlite3_open_v2 failed";
        if (db) sqlite3_close(db);
        throw std::runtime_error("cannot open dashboard config db (" + path + "): " + err);
    }
    static constexpr const char* kSchema =
        "CREATE TABLE IF NOT EXISTS dashboard_layout ("
        "  id INTEGER PRIMARY KEY,"
        "  updated_at INTEGER NOT NULL,"
        "  tree TEXT NOT NULL"
        ");";
    char* errMsg = nullptr;
    if (sqlite3_exec(db, kSchema, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string err = errMsg ? errMsg : "unknown";
        if (errMsg) sqlite3_free(errMsg);
        sqlite3_close(db);
        throw std::runtime_error("cannot initialise dashboard config db: " + err);
    }
    return db;
}

void closeDashboardConfigDb(sqlite3* db) {
    if (db) sqlite3_close(db);
}

namespace {

nlohmann::json toJson(const PaneNode& n) {
    nlohmann::json j;
    if (n.isSplit) {
        j["split"] = std::string(toString(n.direction));
        j["a"] = toJson(*n.a);
        j["b"] = toJson(*n.b);
        return j;
    }
    const auto& c = n.config;
    j["type"] = std::string(toString(c.type));
    if (c.type == PaneType::Chronicle) {
        j["category"]     = std::string(toString(c.category));
        j["sortKey"]      = std::string(toString(c.sortKey));
        j["sortAsc"]      = c.sortAsc;
        j["ownership"]    = std::string(toString(c.ownership));
        // Only meaningful on Uniques categories; kept flat for shape
        // stability. Default (true) is elided.
        if (!c.uniquesShowMisc) j["uniquesShowMisc"] = false;
    }
    if (c.type == PaneType::Inventory) {
        j["inventoryQualityMask"] = c.inventoryQualityMask;
    }
    if (c.type == PaneType::Reconcile) {
        j["reconcileKind"] = std::string(toString(c.reconcileKind));
    }
    if (c.type == PaneType::Backups) {
        j["backupViewMode"]     = std::string(toString(c.backupViewMode));
        j["selectedBackupFile"] = c.selectedBackupFile;
    }
    if (c.type == PaneType::Session
     || c.type == PaneType::Character
     || c.type == PaneType::SessionLoot) {
        j["sessionAnchorPinned"]         = c.sessionAnchorPinned;
        j["sessionAnchorPinnedDate"]     = c.sessionAnchorPinnedDate;
        // Only emit end pin when set; keeps stored JSON tidy for the
        // common auto-end case.
        if (c.sessionAnchorPinnedEndDate > 0) {
            j["sessionAnchorPinnedEndDate"] = c.sessionAnchorPinnedEndDate;
        }
    }
    if (c.type == PaneType::Character
     || c.type == PaneType::SessionLoot) {
        j["characterSelection"] = c.characterSelection;
    }
    if (c.type == PaneType::Uber) {
        j["uberShowUbers"]        = c.uberShowUbers;
        j["uberShowTorchByClass"] = c.uberShowTorchByClass;
    }
    if (c.type == PaneType::SessionLoot) {
        j["sessionLootShowRunes"] = c.sessionLootShowRunes;
    }
    if (c.type == PaneType::Chronicle
     || c.type == PaneType::Inventory
     || c.type == PaneType::Reconcile) {
        j["searchQuery"] = c.searchQuery;
    }
    // Layout weight defaults to 1; omit when unchanged to keep the
    // stored JSON tidy for the common case.
    if (c.paneWeight != 1) j["paneWeight"] = c.paneWeight;
    if (c.infoLevel  != 1) j["infoLevel"]  = c.infoLevel;
    return j;
}

PaneNode fromJson(const nlohmann::json& j) {
    PaneNode n;
    try {
        if (j.contains("split")) {
            n.isSplit = true;
            n.direction = splitDirectionFromString(
                j.value("split", std::string("vertical")));
            n.a = std::make_unique<PaneNode>(fromJson(j.at("a")));
            n.b = std::make_unique<PaneNode>(fromJson(j.at("b")));
            return n;
        }
        auto t = j.value("type", std::string("blank"));
        n.config.type = paneTypeFromString(t);
        if (n.config.type == PaneType::Chronicle) {
            n.config.category  = chronicleCategoryFromString(
                j.value("category", std::string("sets")));
            n.config.sortKey   = sortKeyFromString(
                j.value("sortKey", std::string("base")));
            n.config.sortAsc   = j.value("sortAsc", true);
            n.config.ownership = ownershipFromString(
                j.value("ownership", std::string("all")));
            n.config.uniquesShowMisc = j.value("uniquesShowMisc", true);
        }
        if (n.config.type == PaneType::Inventory) {
            n.config.inventoryQualityMask = j.value(
                "inventoryQualityMask", 0xFFFFFFFFu);
        }
        if (n.config.type == PaneType::Reconcile) {
            n.config.reconcileKind = reconcileKindFromString(
                j.value("reconcileKind", std::string("both")));
        }
        if (n.config.type == PaneType::Backups) {
            n.config.backupViewMode = backupViewModeFromString(
                j.value("backupViewMode", std::string("summary")));
            n.config.selectedBackupFile = j.value(
                "selectedBackupFile", std::string());
        }
        if (n.config.type == PaneType::Session
         || n.config.type == PaneType::Character
         || n.config.type == PaneType::SessionLoot) {
            n.config.sessionAnchorPinned     = j.value(
                "sessionAnchorPinned", false);
            n.config.sessionAnchorPinnedDate = j.value(
                "sessionAnchorPinnedDate", std::int64_t{0});
            n.config.sessionAnchorPinnedEndDate = j.value(
                "sessionAnchorPinnedEndDate", std::int64_t{0});
        }
        if (n.config.type == PaneType::Character
         || n.config.type == PaneType::SessionLoot) {
            n.config.characterSelection = j.value(
                "characterSelection", std::string());
        }
        if (n.config.type == PaneType::Uber) {
            n.config.uberShowUbers        = j.value("uberShowUbers", false);
            n.config.uberShowTorchByClass = j.value("uberShowTorchByClass", false);
        }
        if (n.config.type == PaneType::SessionLoot) {
            n.config.sessionLootShowRunes = j.value("sessionLootShowRunes", true);
        }
        if (n.config.type == PaneType::Chronicle
         || n.config.type == PaneType::Inventory
         || n.config.type == PaneType::Reconcile) {
            n.config.searchQuery = j.value("searchQuery", std::string());
        }
        n.config.paneWeight = std::max(1, j.value("paneWeight", 1));
        n.config.infoLevel  = std::clamp(j.value("infoLevel", 1), 1, 3);
    } catch (const std::exception&) {
        // Corrupt / unexpected shape at this subtree: substitute a blank leaf.
        n = PaneNode{};
    }
    return n;
}

// Detect whether a loaded tree contains any of the configurable-top-row
// pane types. Legacy dashboards (before the configurable-top-row work)
// stored a flat vertical chain of Chronicle / Inventory / etc. leaves
// under a hardcoded top row that was never persisted. When we see such
// a tree, wrap it beneath a fresh default top row on load so returning
// users don't lose the summary panes they used to see.
bool treeHasTopRowPane(const PaneNode& n) {
    if (n.isSplit) {
        return (n.a && treeHasTopRowPane(*n.a))
            || (n.b && treeHasTopRowPane(*n.b));
    }
    switch (n.config.type) {
        case PaneType::Character:
        case PaneType::SessionLoot:
        case PaneType::Uber:
        case PaneType::TerrorZone:
            return true;
        default:
            return false;
    }
}

// Prepend the default top row (Character | Uber | TerrorZone | SessionLoot)
// above `existing`, matching the shape produced by makeDefaultLayout().
PaneNode migrateLegacyLayout(PaneNode&& existing) {
    std::vector<PaneNode> top;
    top.push_back(makeLeaf(PaneType::Character));
    top.push_back(makeLeaf(PaneType::Uber));
    top.push_back(makeLeaf(PaneType::TerrorZone));
    top.push_back(makeLeaf(PaneType::SessionLoot));
    auto topRow = buildVerticalChain(std::move(top));
    setLeafWeight(topRow, 12);
    setLeafWeight(existing, 40);

    PaneNode root;
    root.isSplit   = true;
    root.direction = SplitDirection::Horizontal;
    root.a = std::make_unique<PaneNode>(std::move(topRow));
    root.b = std::make_unique<PaneNode>(std::move(existing));
    return root;
}

} // namespace

PaneNode loadPaneTree(sqlite3* db) {
    const char* sql = "SELECT tree FROM dashboard_layout WHERE id=1 LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return makeDefaultLayout();
    }
    PaneNode result;
    bool got = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (const auto* txt = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 0))) {
            try {
                result = fromJson(nlohmann::json::parse(txt));
                got = true;
            } catch (const std::exception&) {
                got = false;
            }
        }
    }
    sqlite3_finalize(stmt);
    if (!got) return makeDefaultLayout();
    // Auto-migrate a pre-top-row layout the first time we load it.
    if (!treeHasTopRowPane(result)) {
        result = migrateLegacyLayout(std::move(result));
    }
    return result;
}

void savePaneTree(sqlite3* db, const PaneNode& root) {
    const auto payload = toJson(root).dump();
    const char* sql =
        "INSERT INTO dashboard_layout (id, updated_at, tree) "
        "VALUES (1, strftime('%s','now'), ?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "  updated_at = excluded.updated_at, tree = excluded.tree";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, payload.c_str(),
                      static_cast<int>(payload.size()), SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string serializePaneTree(const PaneNode& root) {
    return toJson(root).dump();
}

PaneNode deserializePaneTree(std::string_view json) {
    try {
        // nlohmann::json::parse takes a string-like input; construct
        // explicitly since std::string_view isn't directly consumable
        // by every overload.
        return fromJson(nlohmann::json::parse(std::string(json)));
    } catch (const std::exception&) {
        return PaneNode{};
    }
}

namespace {

// Ensure the backup_retention table exists. Idempotent -- called on every
// load/save so a fresh DB (or one from before this feature) picks up the
// schema without a versioning dance.
void ensureBackupRetentionSchema(sqlite3* db) {
    static constexpr const char* kSchema =
        "CREATE TABLE IF NOT EXISTS backup_retention ("
        "  id INTEGER PRIMARY KEY,"
        "  days INTEGER NOT NULL,"
        "  sessions INTEGER NOT NULL"
        ");";
    char* err = nullptr;
    if (sqlite3_exec(db, kSchema, nullptr, nullptr, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
    }
}

} // namespace

BackupRetentionConfig loadBackupRetention(sqlite3* db) {
    BackupRetentionConfig cfg;
    if (!db) return cfg;
    ensureBackupRetentionSchema(db);
    const char* sql = "SELECT days, sessions FROM backup_retention WHERE id=1 LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return cfg;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        cfg.days     = static_cast<int>(sqlite3_column_int64(stmt, 0));
        cfg.sessions = static_cast<int>(sqlite3_column_int64(stmt, 1));
    }
    sqlite3_finalize(stmt);
    return cfg;
}

void saveBackupRetention(sqlite3* db, BackupRetentionConfig cfg) {
    if (!db) return;
    ensureBackupRetentionSchema(db);
    if (cfg.days < 0)     cfg.days = 0;
    if (cfg.sessions < 0) cfg.sessions = 0;
    const char* sql =
        "INSERT INTO backup_retention (id, days, sessions) VALUES (1, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET days = excluded.days, "
        "                              sessions = excluded.sessions";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int64(stmt, 1, cfg.days);
    sqlite3_bind_int64(stmt, 2, cfg.sessions);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ---- Tree traversal helpers -------------------------------------------------

namespace {

void flattenImpl(PaneNode& n, std::vector<PaneNode*>& out) {
    if (n.isSplit) {
        if (n.a) flattenImpl(*n.a, out);
        if (n.b) flattenImpl(*n.b, out);
    } else {
        out.push_back(&n);
    }
}

void flattenConstImpl(const PaneNode& n, std::vector<const PaneNode*>& out) {
    if (n.isSplit) {
        if (n.a) flattenConstImpl(*n.a, out);
        if (n.b) flattenConstImpl(*n.b, out);
    } else {
        out.push_back(&n);
    }
}

PaneNode* findParentImpl(PaneNode& n, const PaneNode* target) {
    if (!n.isSplit) return nullptr;
    if (n.a.get() == target || n.b.get() == target) return &n;
    if (auto* r = findParentImpl(*n.a, target)) return r;
    if (auto* r = findParentImpl(*n.b, target)) return r;
    return nullptr;
}

} // namespace

std::vector<PaneNode*> flattenLeaves(PaneNode& root) {
    std::vector<PaneNode*> out;
    flattenImpl(root, out);
    return out;
}

std::vector<const PaneNode*> flattenLeaves(const PaneNode& root) {
    std::vector<const PaneNode*> out;
    flattenConstImpl(root, out);
    return out;
}

PaneNode* findParent(PaneNode& root, const PaneNode* child) {
    return findParentImpl(root, child);
}

void enforceCategorySingleton(PaneNode& root, const PaneNode* keep,
                              ChronicleCategory cat) {
    for (auto* leaf : flattenLeaves(root)) {
        if (leaf == keep) continue;
        if (leaf->config.type == PaneType::Chronicle &&
            leaf->config.category == cat) {
            leaf->config.type = PaneType::Blank;
        }
    }
}

void enforceInventorySingleton(PaneNode& root, const PaneNode* keep) {
    for (auto* leaf : flattenLeaves(root)) {
        if (leaf == keep) continue;
        if (leaf->config.type == PaneType::Inventory) {
            leaf->config.type = PaneType::Blank;
        }
    }
}

} // namespace d2r
