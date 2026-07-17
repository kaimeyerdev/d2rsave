// Implementation of the pane-tree config model + SQLite persistence.
// See include/d2r/DashboardConfig.hpp.

#include "d2r/DashboardConfig.hpp"

#include "d2r/Paths.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

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

} // namespace

// ---- Constructors -----------------------------------------------------------

PaneNode makeDefaultLayout() {
    // Three vertical panes: Sets | Uniques (all) | Inventory.
    auto sets = makeLeaf(PaneType::Chronicle, ChronicleCategory::Sets);
    auto uniques = makeLeaf(PaneType::Chronicle, ChronicleCategory::UniquesAll);
    PaneNode inv;
    inv.isSplit = false;
    inv.config.type = PaneType::Inventory;

    std::vector<PaneNode> panes;
    panes.push_back(std::move(sets));
    panes.push_back(std::move(uniques));
    panes.push_back(std::move(inv));
    return buildVerticalChain(std::move(panes));
}

// ---- String conversions -----------------------------------------------------

std::string_view toString(PaneType t) {
    switch (t) {
        case PaneType::Blank:     return "blank";
        case PaneType::Chronicle: return "chronicle";
        case PaneType::Inventory: return "inventory";
        case PaneType::Reconcile: return "reconcile";
    }
    return "blank";
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
    }
    return "(unconfigured)";
}

PaneType paneTypeFromString(std::string_view s) {
    if (s == "chronicle") return PaneType::Chronicle;
    if (s == "inventory") return PaneType::Inventory;
    if (s == "reconcile") return PaneType::Reconcile;
    return PaneType::Blank;
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
    }
    if (c.type == PaneType::Inventory) {
        j["inventoryQualityMask"] = c.inventoryQualityMask;
    }
    if (c.type == PaneType::Reconcile) {
        j["reconcileKind"] = std::string(toString(c.reconcileKind));
    }
    if (c.type == PaneType::Chronicle
     || c.type == PaneType::Inventory
     || c.type == PaneType::Reconcile) {
        j["searchQuery"] = c.searchQuery;
    }
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
        }
        if (n.config.type == PaneType::Inventory) {
            n.config.inventoryQualityMask = j.value(
                "inventoryQualityMask", 0xFFFFFFFFu);
        }
        if (n.config.type == PaneType::Reconcile) {
            n.config.reconcileKind = reconcileKindFromString(
                j.value("reconcileKind", std::string("both")));
        }
        if (n.config.type == PaneType::Chronicle
         || n.config.type == PaneType::Inventory
         || n.config.type == PaneType::Reconcile) {
            n.config.searchQuery = j.value("searchQuery", std::string());
        }
    } catch (const std::exception&) {
        // Corrupt / unexpected shape at this subtree: substitute a blank leaf.
        n = PaneNode{};
    }
    return n;
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
    return got ? std::move(result) : makeDefaultLayout();
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
