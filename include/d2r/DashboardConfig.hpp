// Dashboard user configuration: pane tree model + SQLite persistence.
//
// The dashboard renders a binary tree of splits and leaves. Each leaf holds
// a PaneConfig that fully describes what it shows (chronicle category,
// sort, ownership filter, per-pane search, location column) and how it
// behaves. The tree is serialized as JSON into a single row of a private
// SQLite database under $XDG_DATA_HOME/d2rsave (fallback:
// $HOME/.local/share/d2rsave). The reference DB is not modified.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace d2r {

// ---- Enums ------------------------------------------------------------------

enum class PaneType : std::uint8_t {
    Blank,      // Placeholder; renders instructions to press [c] to configure.
    Chronicle,  // One category's rows from the chronicle.
    Inventory,  // Global item-search across every parsed source.
    Reconcile,  // Diff owned items vs chronicle entries.
    Backups,    // Save-file history from the backup DB (see BackupDb.hpp).
    BackupLog,  // Ephemeral ring buffer of this-process backup events.
    Session,    // XP + new items acquired during the current play session
                // (or the just-ended one). A session ends with the
                // active character's SaveAndExit backup; its diff base
                // is the first save that opened it.
};

enum class ChronicleCategory : std::uint8_t {
    Sets,
    UniquesNormal,
    UniquesExceptional,
    UniquesElite,
    UniquesMisc,
    UniquesAll,
};

enum class OwnershipFilter : std::uint8_t {
    All,             // Show discovered + remaining.
    RemainingOnly,   // Hide owned rows.
    DiscoveredOnly,  // Hide unowned rows.
};

enum class PaneSortKey : std::uint8_t { Name, Base, Owned };

// Vertical split   = side-by-side (a vertical separator between two panes).
// Horizontal split = stacked      (a horizontal separator between two panes).
enum class SplitDirection : std::uint8_t { Horizontal, Vertical };

// Which item classes a reconcile pane shows. The reconcile pane always
// displays every item where owned != discovered; this filter narrows the
// list to one item class.
enum class ReconcileKindFilter : std::uint8_t {
    Both,
    UniquesOnly,
    SetsOnly,
};

// Which view the Backups pane is showing.
//
//   Summary -- one row per known file (stash pinned at top).
//   Detail  -- reverse-chronological history for `selectedBackupFile`.
enum class BackupViewMode : std::uint8_t {
    Summary,
    Detail,
};

// ---- Leaf configuration -----------------------------------------------------

struct PaneConfig {
    PaneType          type         = PaneType::Blank;

    // Chronicle-only.
    ChronicleCategory category     = ChronicleCategory::Sets;
    PaneSortKey       sortKey      = PaneSortKey::Base;
    bool              sortAsc      = true;
    OwnershipFilter   ownership    = OwnershipFilter::All;

    // Inventory-only. Bitmask over ItemQuality (bit N = 1u << (int)quality).
    // 0 means "no quality passes" (nothing shown); the initial default set
    // in makeDefaultLayout / on new leaves is "all-on".
    std::uint32_t     inventoryQualityMask = 0xFFFFFFFFu;

    // Reconcile-only.
    ReconcileKindFilter reconcileKind = ReconcileKindFilter::Both;

    // Backups-only.
    BackupViewMode    backupViewMode = BackupViewMode::Summary;
    // Basename (e.g. "Kai.d2s"). Empty in Summary view; set to the row
    // the user drilled into for Detail view. Also used to remember the
    // most recently-viewed file across dashboard restarts.
    std::string       selectedBackupFile;

    // Session-only. When `sessionAnchorPinned` is true, the Session
    // pane uses `sessionAnchorPinnedDate` as its diff base -- the
    // newest .d2s row for the active player with date <= this value,
    // plus the matching stash backup. When false, the anchor tracks
    // whatever SaveAndExit the scheduler last captured (auto mode).
    // Pins are per-pane and persisted across dashboard restarts.
    bool              sessionAnchorPinned     = false;
    std::int64_t      sessionAnchorPinnedDate = 0;   // unix seconds

    // Chronicle + Inventory + Reconcile.
    std::string       searchQuery;

    // Runtime-only (not persisted): scroll cursor.
    int               cursor       = 0;
};

// ---- Tree -------------------------------------------------------------------

struct PaneNode {
    bool                        isSplit   = false;
    // Leaf state (isSplit == false).
    PaneConfig                  config;
    // Split state (isSplit == true).
    SplitDirection              direction = SplitDirection::Vertical;
    std::unique_ptr<PaneNode>   a;   // left (Vertical) or top    (Horizontal)
    std::unique_ptr<PaneNode>   b;   // right           or bottom
};

// ---- Constructors -----------------------------------------------------------

// The default layout: five side-by-side chronicle panes:
//   Sets | Uniques-Normal | Uniques-Exceptional | Uniques-Elite | Uniques-Misc
PaneNode makeDefaultLayout();

// ---- String conversions -----------------------------------------------------

std::string_view toString(PaneType);
std::string_view toString(ChronicleCategory);
std::string_view toString(OwnershipFilter);
std::string_view toString(PaneSortKey);
std::string_view toString(SplitDirection);
std::string_view toString(ReconcileKindFilter);
std::string_view toString(BackupViewMode);

std::string categoryLabel(ChronicleCategory);   // "Uniques - Elite", etc.
std::string paneTitle(const PaneConfig&);       // Full pane title for the window bar.

PaneType            paneTypeFromString(std::string_view);
ChronicleCategory   chronicleCategoryFromString(std::string_view);
OwnershipFilter     ownershipFromString(std::string_view);
PaneSortKey         sortKeyFromString(std::string_view);
SplitDirection      splitDirectionFromString(std::string_view);
ReconcileKindFilter reconcileKindFromString(std::string_view);
BackupViewMode      backupViewModeFromString(std::string_view);

// ---- Persistence ------------------------------------------------------------

// Open the config DB (create-if-missing) and ensure the schema exists.
// Caller must release with closeDashboardConfigDb.
sqlite3* openDashboardConfigDb();
void     closeDashboardConfigDb(sqlite3* db);

// Load the saved layout; returns makeDefaultLayout() if the DB is empty or
// the stored JSON fails to parse.
PaneNode loadPaneTree(sqlite3* db);
void     savePaneTree(sqlite3* db, const PaneNode& root);

// Persisted retention config for the backup subsystem. Loaded from
// `dashboard.sqlite`; stored back on Save from the Backups pane
// retention editor. The values map directly to
// BackupDb::enforceRetention(days, sessionsPerFile, now).
struct BackupRetentionConfig {
    int days     = 30;
    int sessions = 100;
};
[[nodiscard]] BackupRetentionConfig loadBackupRetention(sqlite3* db);
void                                saveBackupRetention(sqlite3* db,
                                                        BackupRetentionConfig cfg);

// ---- Tree traversal helpers -------------------------------------------------

std::vector<PaneNode*>       flattenLeaves(PaneNode& root);
std::vector<const PaneNode*> flattenLeaves(const PaneNode& root);

// Parent split of `child`; nullptr if `child` is the root or not in the tree.
PaneNode* findParent(PaneNode& root, const PaneNode* child);

// Blank any leaves (other than `keep`) currently displaying `cat`.
void enforceCategorySingleton(PaneNode& root, const PaneNode* keep,
                              ChronicleCategory cat);
// Same for the Inventory pane type.
void enforceInventorySingleton(PaneNode& root, const PaneNode* keep);

} // namespace d2r
