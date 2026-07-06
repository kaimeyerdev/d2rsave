// FTXUI-based dashboard: pane-tree layout, per-pane configuration, and a
// user-configurable persistent layout stored in a private SQLite DB.
//
// Only compiled when the ftxui vcpkg port is available; see CMakeLists.txt.

#include "d2r/Dashboard.hpp"
#include "d2r/DashboardConfig.hpp"
#include "d2r/DashboardModel.hpp"
#include "d2r/RefDb.hpp"

#if D2R_HAVE_INOTIFY
#include "d2r/Watcher.hpp"
#endif

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/terminal.hpp>

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace d2r {

namespace {

using namespace ftxui;

// --------------------------- small util helpers -----------------------------

std::string classString(CharacterClass c) {
    const auto sv = toString(c);
    return std::string(sv.data(), sv.size());
}

const char* difficultyName(std::uint8_t d) {
    switch (d) {
        case 0: return "Normal";
        case 1: return "Nightmare";
        case 2: return "Hell";
        default: return "?";
    }
}

std::string formatWithThousands(std::uint64_t n) {
    std::string s = std::to_string(n);
    std::string out;
    out.reserve(s.size() + s.size() / 3);
    int digits = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (digits && digits % 3 == 0) out.push_back(',');
        out.push_back(*it);
        ++digits;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::string formatTime(std::uint64_t epoch) {
    if (epoch == 0) return "-";
    std::time_t t = static_cast<std::time_t>(epoch);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    return buf;
}

bool containsCI(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    auto lc = [](unsigned char c) { return std::tolower(c); };
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [&](char a, char b) { return lc(a) == lc(b); });
    return it != hay.end();
}

// ---------------------- chronicle category dispatch -------------------------

struct KindTier { ChronicleKind kind; std::uint32_t tierMask; };

std::uint32_t tierBit(ChronicleTier t) { return 1u << static_cast<int>(t); }

KindTier categoryToKindTier(ChronicleCategory c) {
    switch (c) {
        case ChronicleCategory::Sets:
            return {ChronicleKind::Set, 0u};
        case ChronicleCategory::UniquesNormal:
            return {ChronicleKind::Unique, tierBit(ChronicleTier::Normal)};
        case ChronicleCategory::UniquesExceptional:
            return {ChronicleKind::Unique, tierBit(ChronicleTier::Exceptional)};
        case ChronicleCategory::UniquesElite:
            return {ChronicleKind::Unique, tierBit(ChronicleTier::Elite)};
        case ChronicleCategory::UniquesMisc:
            return {ChronicleKind::Unique, tierBit(ChronicleTier::Misc)};
        case ChronicleCategory::UniquesAll:
            return {ChronicleKind::Unique, 0u};
    }
    return {ChronicleKind::Set, 0u};
}

// -------------------------- shared UI state ---------------------------------

// Ephemeral UI state (not persisted). The persistent slice lives in
// `rootPane` and is written back through `savePaneTree` on layout changes
// and when the user quits.
struct UiState {
    // Data snapshot (owned; watcher thread replaces via mutex).
    std::mutex                                snapshotMutex;
    std::shared_ptr<const DashboardSnapshot>  snapshot;

    // Pane tree (persistent).
    PaneNode                                  rootPane;
    // Flattened leaf list, rebuilt each render.
    std::vector<PaneNode*>                    leaves;
    int                                       focusedLeaf = 0;

    // Overlay modes. Only one is active at a time.
    bool                                      configMode  = false;  // per-pane config menu
    int                                       configMenu  = 0;      // cursor within menu
    bool                                      searchMode  = false;  // typing into pane search
    bool                                      helpVisible = false;

    // Watched path (for footer).
    std::string                               watchedPath;

    // Signal from watcher to redraw.
    std::atomic<bool>                         shutdown{false};
};

// ------------------------- top summary panels -------------------------------

Element renderActivePlayer(const DashboardSnapshot& s) {
    if (!s.hasActivePlayer) {
        return window(text(" Active Player "), text(" (no .d2s files found)"));
    }
    const auto& p = s.activePlayer;
    const auto lvlLine = "Level " + std::to_string(p.level)
                       + " " + std::string(difficultyName(p.difficulty))
                       + " A" + std::to_string(p.act);
    const auto pctForBar = std::clamp(p.expPercent / 100.0, 0.0, 1.0);
    const bool tableStale = p.expForLevel > 0 && p.expInLevel > p.expForLevel;
    const auto pctLbl = [&]{
        char b[24];
        std::snprintf(b, sizeof(b), "%.2f%%%s", p.expPercent,
                      tableStale ? " *" : "");
        return std::string(b);
    }();
    const auto expLine = formatWithThousands(p.expInLevel) + " / "
                       + formatWithThousands(p.expForLevel);
    return window(
        text(" Active Player "),
        vbox({
            hbox({ text(p.name), text("  "),
                   text("(" + classString(p.characterClass) + ")") | dim }),
            text(p.file) | dim,
            text(lvlLine),
            hbox({ text("HC "), text(p.hardcore ? "yes" : "no ") | dim,
                   text("   Died "), text(p.died ? "yes" : "no ") | dim }),
            separator(),
            hbox({ text("Exp "), text(pctLbl) }),
            gauge(static_cast<float>(pctForBar)),
            text(expLine) | dim,
            text("total  " + formatWithThousands(p.experience)) | dim,
            tableStale
                ? text("* exp table stale for L" + std::to_string(p.level)
                       + "; % may be off") | dim
                : text(""),
        })
    );
}

Element renderHellfireTorch(const HellfireTorchQuest& q) {
    auto row = [](const char* label, std::uint32_t n) {
        return hbox({
            text(label) | size(WIDTH, EQUAL, 24),
            filler(),
            text(std::to_string(n)) | bold | align_right | size(WIDTH, EQUAL, 6),
        });
    };
    return window(
        text(" Hellfire Torch "),
        vbox({
            row("Key of Terror",       q.keysTerror),
            row("Key of Hate",         q.keysHate),
            row("Key of Destruction",  q.keysDestruction),
            separator(),
            row("Hellfire Torch",      q.torchesHellfire),
        })
    );
}

Element renderColossalAncients(const ColossalAncientsQuest& q) {
    auto row = [](const char* label, std::uint32_t n) {
        return hbox({
            text(label) | size(WIDTH, EQUAL, 26),
            filler(),
            text(std::to_string(n)) | bold | align_right | size(WIDTH, EQUAL, 6),
        });
    };
    return window(
        text(" Colossal Ancients "),
        vbox({
            row("Talic's Anguish",       q.talicAnguish),
            row("Madawc's Ire",          q.madawcIre),
            row("Korlic's Pain",         q.korlicPain),
            row("Bul-Kathos' Nightmare", q.bulKathosNightmare),
            row("Worusk's End",          q.woruskEnd),
            separator(),
            row("Colossal Jewel",        q.colossalJewels),
        })
    );
}

Element renderTerrorZones(const TerrorZones& t) {
    auto row = [](const char* label, std::uint32_t n) {
        return hbox({
            text(label) | size(WIDTH, EQUAL, 28),
            filler(),
            text(std::to_string(n)) | bold | align_right | size(WIDTH, EQUAL, 6),
        });
    };
    auto zoneLine = [](const char* label, const std::optional<std::string>& z) {
        return hbox({
            text(label) | size(WIDTH, EQUAL, 12),
            text(z.value_or("unavailable")) | (z ? bold : dim),
        });
    };
    return window(
        text(" Terror Zones "),
        vbox({
            row("Western Worldstone Shard",  t.shardWestern),
            row("Eastern Worldstone Shard",  t.shardEastern),
            row("Southern Worldstone Shard", t.shardSouthern),
            row("Deep Worldstone Shard",     t.shardDeep),
            row("Northern Worldstone Shard", t.shardNorthern),
            separator(),
            zoneLine("Current : ", t.currentZone),
            zoneLine("Next    : ", t.nextZone),
        })
    );
}

// -------------------------- chronicle rendering -----------------------------

std::string sortKeyLabel(PaneSortKey k) {
    switch (k) {
        case PaneSortKey::Name:  return "name";
        case PaneSortKey::Base:  return "base";
        case PaneSortKey::Owned: return "owned";
    }
    return "?";
}

std::string ownershipLabel(OwnershipFilter o) {
    switch (o) {
        case OwnershipFilter::All:             return "all";
        case OwnershipFilter::RemainingOnly:   return "remaining";
        case OwnershipFilter::DiscoveredOnly:  return "discovered";
    }
    return "?";
}

// Filter + sort chronicle rows for one leaf's configuration.
std::vector<const ChronicleRow*> filterForLeaf(const PaneConfig& c,
                                                const DashboardSnapshot& s) {
    const auto kt = categoryToKindTier(c.category);
    std::vector<const ChronicleRow*> out;
    out.reserve(s.chronicle.size());
    for (const auto& r : s.chronicle) {
        if (r.kind != kt.kind)                                     continue;
        if (kt.tierMask != 0 && (kt.tierMask & tierBit(r.tier)) == 0) continue;
        if (r.discovered  && c.ownership == OwnershipFilter::RemainingOnly)  continue;
        if (!r.discovered && c.ownership == OwnershipFilter::DiscoveredOnly) continue;
        if (!c.searchQuery.empty()) {
            const bool hit = containsCI(r.displayName, c.searchQuery)
                          || containsCI(r.baseName,    c.searchQuery)
                          || containsCI(r.setName,     c.searchQuery);
            if (!hit) continue;
        }
        out.push_back(&r);
    }

    auto cmpAsc = [&](const ChronicleRow* a, const ChronicleRow* b) {
        auto lc = [](std::string s) {
            for (auto& ch : s) ch = std::tolower(static_cast<unsigned char>(ch));
            return s;
        };
        switch (c.sortKey) {
            case PaneSortKey::Name:  return lc(a->displayName) < lc(b->displayName);
            case PaneSortKey::Base:  return lc(a->baseName)    < lc(b->baseName);
            case PaneSortKey::Owned: return a->discovered && !b->discovered;
        }
        return false;
    };
    if (c.sortAsc) std::stable_sort(out.begin(), out.end(), cmpAsc);
    else           std::stable_sort(out.begin(), out.end(),
                        [&](auto* a, auto* b){ return cmpAsc(b, a); });
    return out;
}

inline std::uint32_t qualityBit(ItemQuality q) {
    return 1u << static_cast<int>(q);
}

inline bool inventoryQualityAllowed(const PaneConfig& c, ItemQuality q) {
    return (c.inventoryQualityMask & qualityBit(q)) != 0;
}

// The set of qualities the inventory pane lets the user toggle. `None`
// and `Unknown` are omitted because they never appear on real items.
constexpr std::array<ItemQuality, 8> kInventoryQualities{{
    ItemQuality::Normal,   ItemQuality::Magic,   ItemQuality::Rare,
    ItemQuality::Unique,   ItemQuality::Set,     ItemQuality::Craft,
    ItemQuality::Superior, ItemQuality::Inferior,
}};

// Filter inventory items by the leaf's quality mask and search query.
std::vector<const InventoryItem*> filterInventory(const PaneConfig& c,
                                                    const DashboardSnapshot& s) {
    std::vector<const InventoryItem*> out;
    out.reserve(s.inventory.size());
    for (const auto& it : s.inventory) {
        if (!inventoryQualityAllowed(c, it.quality)) continue;
        if (!c.searchQuery.empty() &&
            !containsCI(it.name,     c.searchQuery) &&
            !containsCI(it.baseName, c.searchQuery) &&
            !containsCI(it.location, c.searchQuery)) continue;
        out.push_back(&it);
    }
    return out;
}

// --- content-width helpers ---

struct ChronWidths {
    int owned = 2, item = 4, base = 4, setLbl = 3;
};

ChronWidths measureChronicle(const std::vector<const ChronicleRow*>& rows,
                              bool includeSet) {
    ChronWidths w;
    auto bump = [](int& cur, std::size_t n, int cap) {
        cur = std::min(cap, std::max(cur, static_cast<int>(n)));
    };
    for (const auto* r : rows) {
        bump(w.item, r->displayName.size(), 36);
        bump(w.base, r->baseName.size(),    26);
        if (includeSet) bump(w.setLbl, r->setName.size(), 24);
    }
    return w;
}

constexpr int kCellPad = 2;

// Compose one padded cell of fixed content-width. The content is prefixed
// with a space; ftxui right-pads the rest of the cell with spaces.
Element cellText(const std::string& s, int contentW) {
    return text(" " + s) | size(WIDTH, EQUAL, contentW + kCellPad);
}

// Render a leaf as a Chronicle pane. `focused` controls the highlighted
// row + the inverted title.
Element renderChronicleLeaf(const PaneConfig& c, const DashboardSnapshot& s,
                             bool focused, bool searchMode) {
    const auto rows = filterForLeaf(c, s);
    const auto kt = categoryToKindTier(c.category);
    const bool isSet = kt.kind == ChronicleKind::Set;
    const auto w = measureChronicle(rows, isSet);

    // The discovered ("✓") column is redundant when the pane is showing
    // remaining-only rows: every row would have an empty cell. Hide it.
    const bool showDiscoveredCol = c.ownership != OwnershipFilter::RemainingOnly;

    // Header row.
    Elements header;
    if (showDiscoveredCol) header.push_back(cellText("✓", w.owned));
    header.push_back(cellText("Item", w.item));
    header.push_back(cellText("Base", w.base));
    if (isSet) header.push_back(cellText("Set", w.setLbl));

    const int shown = static_cast<int>(rows.size());
    const int owned = static_cast<int>(std::count_if(
        rows.begin(), rows.end(), [](auto* r){ return r->discovered; }));
    const int clamped = std::clamp(c.cursor, 0, std::max(0, shown - 1));

    Elements body;
    body.reserve(static_cast<std::size_t>(shown + 2));
    body.push_back(hbox(std::move(header)) | bold);
    body.push_back(separator());
    for (int i = 0; i < shown; ++i) {
        const auto* r = rows[static_cast<std::size_t>(i)];
        Elements cells;
        if (showDiscoveredCol) {
            cells.push_back(cellText(r->discovered ? "✓" : "", w.owned));
        }
        cells.push_back(cellText(r->displayName, w.item));
        cells.push_back(cellText(r->baseName,    w.base));
        if (isSet) cells.push_back(cellText(r->setName, w.setLbl));
        Element row = hbox(std::move(cells));
        if (focused && i == clamped)  row = row | inverted | ftxui::focus;
        else if (r->discovered)       row = row | dim;
        body.push_back(row);
    }

    // Status line above the frame.
    std::string status = "sort: " + sortKeyLabel(c.sortKey)
                       + (c.sortAsc ? " asc" : " desc")
                       + " | show: " + ownershipLabel(c.ownership);
    if (!c.searchQuery.empty() && !searchMode) {
        status += " | q=\"" + c.searchQuery + "\"";
    }

    // Title line.
    std::string title = " " + paneTitle(c) + "  "
                      + std::to_string(owned) + "/" + std::to_string(shown) + " ";
    Element titleEl = text(title);
    if (focused) titleEl = titleEl | inverted;

    // Search input line takes the status line's place when active.
    Element topLine = focused && searchMode
        ? hbox({ text(" search: ") | bold,
                 text(c.searchQuery),
                 text("_") | blink }) | inverted
        : text(status) | dim;

    return window(titleEl, vbox({
        topLine,
        separator(),
        vbox(std::move(body)) | vscroll_indicator | yframe | flex,
    }));
}

// Render a leaf as an Inventory pane. Shares scrolling model with
// chronicle panes; the row set is different.
Element renderInventoryLeaf(const PaneConfig& c, const DashboardSnapshot& s,
                             bool focused, bool searchMode) {
    const auto rows = filterInventory(c, s);

    // Widths (per-pane content).
    int wName = 4, wBase = 4, wLoc = 8;
    auto bump = [](int& cur, std::size_t n, int cap) {
        cur = std::min(cap, std::max(cur, static_cast<int>(n)));
    };
    for (const auto* it : rows) {
        bump(wName, it->name.size(),     36);
        bump(wBase, it->baseName.size(), 26);
        bump(wLoc,  it->location.size(), 26);
    }

    Elements header{
        cellText("Name",     wName),
        cellText("Base",     wBase),
        cellText("Location", wLoc),
    };

    const int shown = static_cast<int>(rows.size());
    const int clamped = std::clamp(c.cursor, 0, std::max(0, shown - 1));

    Elements body;
    body.reserve(static_cast<std::size_t>(shown + 2));
    body.push_back(hbox(std::move(header)) | bold);
    body.push_back(separator());
    for (int i = 0; i < shown; ++i) {
        const auto* it = rows[static_cast<std::size_t>(i)];
        Element row = hbox({
            cellText(it->name,     wName),
            cellText(it->baseName, wBase),
            cellText(it->location, wLoc),
        });
        if (focused && i == clamped) row = row | inverted | ftxui::focus;
        body.push_back(row);
    }

    std::string title = " Inventory  " + std::to_string(shown) + " items ";
    Element titleEl = text(title);
    if (focused) titleEl = titleEl | inverted;

    // Summarise which qualities are currently enabled so the user can see
    // at a glance why some items aren't showing.
    auto qualitySummary = [&]() -> std::string {
        std::string s;
        int enabled = 0;
        for (auto q : kInventoryQualities) {
            if (inventoryQualityAllowed(c, q)) ++enabled;
        }
        if (enabled == (int)kInventoryQualities.size()) return "all";
        if (enabled == 0) return "none";
        for (auto q : kInventoryQualities) {
            if (!inventoryQualityAllowed(c, q)) continue;
            if (!s.empty()) s += ",";
            switch (q) {
                case ItemQuality::Normal:   s += "norm";  break;
                case ItemQuality::Magic:    s += "magic"; break;
                case ItemQuality::Rare:     s += "rare";  break;
                case ItemQuality::Unique:   s += "uniq";  break;
                case ItemQuality::Set:      s += "set";   break;
                case ItemQuality::Craft:    s += "craft"; break;
                case ItemQuality::Superior: s += "sup";   break;
                case ItemQuality::Inferior: s += "inf";   break;
                default: break;
            }
        }
        return s;
    };

    std::string status = "quality: " + qualitySummary();
    if (!c.searchQuery.empty() && !searchMode) {
        status += "  |  q=\"" + c.searchQuery + "\"";
    } else if (c.searchQuery.empty() && !searchMode) {
        status += "  |  [/] search";
    }

    Element topLine = focused && searchMode
        ? hbox({ text(" search: ") | bold,
                 text(c.searchQuery),
                 text("_") | blink }) | inverted
        : text(" " + status) | dim;

    return window(titleEl, vbox({
        topLine,
        separator(),
        vbox(std::move(body)) | vscroll_indicator | yframe | flex,
    }));
}

// Render a leaf as a Reconcile pane. Shows every unique/set with a
// mismatch between `owned` (physically held in inventory/stash) and
// `discovered` (on the account chronicle). Sunder-charm siblings share
// their flags (see DashboardModel.cpp) so a Renewed variant doesn't
// falsely show up as "owned, not discovered".
Element renderReconcileLeaf(const PaneConfig& c, const DashboardSnapshot& s,
                            bool focused, bool searchMode) {
    std::vector<const ReconcileEntry*> rows;
    rows.reserve(s.reconcile.size());
    for (const auto& e : s.reconcile) {
        switch (c.reconcileKind) {
            case ReconcileKindFilter::Both: break;
            case ReconcileKindFilter::UniquesOnly:
                if (e.kind != ChronicleKind::Unique) continue;
                break;
            case ReconcileKindFilter::SetsOnly:
                if (e.kind != ChronicleKind::Set) continue;
                break;
        }
        if (!c.searchQuery.empty() &&
            !containsCI(e.displayName, c.searchQuery) &&
            !containsCI(e.baseName,    c.searchQuery) &&
            !containsCI(e.location,    c.searchQuery)) {
            continue;
        }
        rows.push_back(&e);
    }
    std::stable_sort(rows.begin(), rows.end(),
        [](auto* a, auto* b) {
            auto lc = [](std::string s) {
                for (auto& ch : s) ch = std::tolower(static_cast<unsigned char>(ch));
                return s;
            };
            const auto la = lc(a->baseName), lb = lc(b->baseName);
            if (la != lb) return la < lb;
            return lc(a->displayName) < lc(b->displayName);
        });

    // Column widths. Owned and Discovered are single-char markers.
    int wKind = 6, wItem = 4, wBase = 4, wOwn = 5, wDisc = 10;
    auto bump = [](int& cur, std::size_t n, int cap) {
        cur = std::min(cap, std::max(cur, static_cast<int>(n)));
    };
    for (const auto* r : rows) {
        bump(wItem, r->displayName.size(), 36);
        bump(wBase, r->baseName.size(),    26);
    }

    Elements header{
        cellText("Kind",       wKind),
        cellText("Item",       wItem),
        cellText("Base",       wBase),
        cellText("Owned",      wOwn),
        cellText("Discovered", wDisc),
    };

    const int shown = static_cast<int>(rows.size());
    const int clamped = std::clamp(c.cursor, 0, std::max(0, shown - 1));

    Elements body;
    body.reserve(static_cast<std::size_t>(shown + 2));
    body.push_back(hbox(std::move(header)) | bold);
    body.push_back(separator());
    for (int i = 0; i < shown; ++i) {
        const auto* r = rows[static_cast<std::size_t>(i)];
        std::string kindLabel = r->kind == ChronicleKind::Unique ? "unique" : "set";
        Element row = hbox({
            cellText(kindLabel,                wKind),
            cellText(r->displayName,           wItem),
            cellText(r->baseName,              wBase),
            cellText(r->owned      ? "✓" : "", wOwn),
            cellText(r->discovered ? "✓" : "", wDisc),
        });
        if (focused && i == clamped) row = row | inverted | ftxui::focus;
        body.push_back(row);
    }

    std::string title = " " + paneTitle(c) + "  " + std::to_string(shown) + " ";
    Element titleEl = text(title);
    if (focused) titleEl = titleEl | inverted;

    Element topLine = focused && searchMode
        ? hbox({ text(" search: ") | bold,
                 text(c.searchQuery),
                 text("_") | blink }) | inverted
        : c.searchQuery.empty()
            ? text(" items where owned and discovered disagree") | dim
            : text(" q=\"" + c.searchQuery + "\"") | dim;

    return window(titleEl, vbox({
        topLine,
        separator(),
        vbox(std::move(body)) | vscroll_indicator | yframe | flex,
    }));
}

Element renderBlankLeaf(bool focused) {
    Element titleEl = text(" (unconfigured) ");
    if (focused) titleEl = titleEl | inverted;
    return window(titleEl, vbox({
        filler(),
        hbox({ filler(),
               vbox({
                   text("This pane is unconfigured.") | dim,
                   text("Focus with [Tab], then press [c] to configure.") | dim,
               }),
               filler() }),
        filler(),
    }));
}

// ---- Config-mode menu -------------------------------------------------------

struct ConfigMenuItem {
    std::string label;
    // Discriminates the action taken when Enter is pressed.
    enum Kind {
        CycleType, CycleCategory, CycleSort, ToggleSortDir,
        CycleOwnership,
        CycleReconcileKind,
        ToggleQuality,
        SplitVertical, SplitHorizontal, DeletePane, Close,
    } kind;
    // Extra payload used when kind == ToggleQuality.
    ItemQuality quality = ItemQuality::None;
};

std::string reconcileKindLabel(ReconcileKindFilter k) {
    switch (k) {
        case ReconcileKindFilter::Both:        return "both";
        case ReconcileKindFilter::UniquesOnly: return "uniques only";
        case ReconcileKindFilter::SetsOnly:    return "sets only";
    }
    return "?";
}

std::string qualityLabel(ItemQuality q) {
    switch (q) {
        case ItemQuality::Inferior: return "inferior";
        case ItemQuality::Normal:   return "normal (potions, runes, gems, plain gear)";
        case ItemQuality::Superior: return "superior";
        case ItemQuality::Magic:    return "magic";
        case ItemQuality::Set:      return "set";
        case ItemQuality::Rare:     return "rare";
        case ItemQuality::Unique:   return "unique";
        case ItemQuality::Craft:    return "craft";
        default:                    return "?";
    }
}

// Build the menu appropriate to the leaf's current type. Items that only
// make sense for chronicle panes are hidden for other types.
std::vector<ConfigMenuItem> buildConfigMenu(const PaneConfig& c, bool canDelete) {
    std::vector<ConfigMenuItem> items;
    items.push_back({"type:        " + std::string(toString(c.type)),
                      ConfigMenuItem::CycleType});
    if (c.type == PaneType::Chronicle) {
        items.push_back({"category:    " + categoryLabel(c.category),
                          ConfigMenuItem::CycleCategory});
        items.push_back({"sort key:    " + sortKeyLabel(c.sortKey),
                          ConfigMenuItem::CycleSort});
        items.push_back({std::string("sort order:  ") + (c.sortAsc ? "ascending" : "descending"),
                          ConfigMenuItem::ToggleSortDir});
        items.push_back({"ownership:   " + ownershipLabel(c.ownership),
                          ConfigMenuItem::CycleOwnership});
    }
    if (c.type == PaneType::Reconcile) {
        items.push_back({"kind filter: " + reconcileKindLabel(c.reconcileKind),
                          ConfigMenuItem::CycleReconcileKind});
    }
    if (c.type == PaneType::Inventory) {
        for (auto q : kInventoryQualities) {
            const bool on = inventoryQualityAllowed(c, q);
            items.push_back({
                std::string("show ") + qualityLabel(q) + ":  " + (on ? "on" : "off"),
                ConfigMenuItem::ToggleQuality,
                q,
            });
        }
    }
    items.push_back({"split vertical   (side-by-side)", ConfigMenuItem::SplitVertical});
    items.push_back({"split horizontal (stacked)",       ConfigMenuItem::SplitHorizontal});
    if (canDelete) {
        items.push_back({"delete pane (sibling absorbs slot)", ConfigMenuItem::DeletePane});
    }
    items.push_back({"close config menu (Esc)", ConfigMenuItem::Close});
    return items;
}

Element renderConfigMenu(const UiState& ui, const PaneConfig& c, bool canDelete) {
    const auto items = buildConfigMenu(c, canDelete);
    const int cursor = std::clamp(ui.configMenu, 0, (int)items.size() - 1);

    Elements rows;
    rows.push_back(text(" Configure pane ") | bold);
    rows.push_back(separator());
    for (int i = 0; i < (int)items.size(); ++i) {
        Element row = text("  " + items[i].label);
        if (i == cursor) row = row | inverted;
        rows.push_back(row);
    }
    rows.push_back(separator());
    rows.push_back(text(" Up/Down navigate  Enter activate  Esc close ") | dim);

    Element titleEl = text(" " + paneTitle(c) + " [config] ") | inverted;
    return window(titleEl, vbox(std::move(rows)));
}

// How many independent vertical strips this subtree needs to render
// (i.e. its horizontal weight when it appears inside a vertical split).
// A leaf is 1. A vertical split (side-by-side) sums its children; a
// horizontal split (stacked) takes the max because both children share
// the same horizontal band.
int widthShare(const PaneNode& n) {
    if (!n.isSplit) return 1;
    if (n.direction == SplitDirection::Vertical)
        return widthShare(*n.a) + widthShare(*n.b);
    return std::max(widthShare(*n.a), widthShare(*n.b));
}

// Vertical counterpart to `widthShare`. A leaf is 1; a horizontal split
// (stacked) sums; a vertical split (side-by-side) takes the max because
// both children share the same vertical band.
int heightShare(const PaneNode& n) {
    if (!n.isSplit) return 1;
    if (n.direction == SplitDirection::Horizontal)
        return heightShare(*n.a) + heightShare(*n.b);
    return std::max(heightShare(*n.a), heightShare(*n.b));
}

// ---- Pane tree rendering ---------------------------------------------------

// Depth-first render. Increments `leafIdx` for each leaf reached; the
// caller compares that index against the focused index to highlight and
// applies config overlay when appropriate.
//
// `width` and `height` are the cell dimensions this subtree has been
// allocated. Splits divide them evenly and pin each child to its half via
// `size(WIDTH, EQUAL, ...)` / `size(HEIGHT, EQUAL, ...)`. Explicit sizing
// (rather than `flex`) is required because ftxui's flex distribution
// depends on minimum content widths: a wide chronicle sibling would
// otherwise starve a narrow Blank pane in a vertical split, making the
// blank one collapse to zero.
Element renderPane(PaneNode& node, const UiState& ui,
                   const DashboardSnapshot& s, int& leafIdx,
                   int width, int height) {
    if (node.isSplit) {
        // Weight each side by how much of the split's own axis it actually
        // consumes: vertical splits weight by column count (widthShare),
        // horizontal splits weight by row count (heightShare). Using plain
        // leaf counts overweights stacked panes when apportioning width
        // (splitting a column horizontally shouldn't make the column wider).
        if (node.direction == SplitDirection::Vertical) {
            const int la = widthShare(*node.a);
            const int lb = widthShare(*node.b);
            const int tot = std::max(1, la + lb);
            const int wa = std::max(1, (width * la) / tot);
            const int wb = std::max(1, width - wa);
            auto a = renderPane(*node.a, ui, s, leafIdx, wa, height);
            auto b = renderPane(*node.b, ui, s, leafIdx, wb, height);
            return hbox({
                std::move(a) | size(WIDTH, EQUAL, wa),
                std::move(b) | size(WIDTH, EQUAL, wb),
            }) | size(WIDTH, EQUAL, width) | size(HEIGHT, EQUAL, height);
        }
        const int la = heightShare(*node.a);
        const int lb = heightShare(*node.b);
        const int tot = std::max(1, la + lb);
        const int ha = std::max(1, (height * la) / tot);
        const int hb = std::max(1, height - ha);
        auto a = renderPane(*node.a, ui, s, leafIdx, width, ha);
        auto b = renderPane(*node.b, ui, s, leafIdx, width, hb);
        return vbox({
            std::move(a) | size(HEIGHT, EQUAL, ha),
            std::move(b) | size(HEIGHT, EQUAL, hb),
        }) | size(WIDTH, EQUAL, width) | size(HEIGHT, EQUAL, height);
    }
    const int myIdx = leafIdx++;
    const bool focused = (myIdx == ui.focusedLeaf);
    if (focused && ui.configMode) {
        const bool canDelete = findParent(*const_cast<PaneNode*>(&ui.rootPane),
                                          &node) != nullptr;
        return renderConfigMenu(ui, node.config, canDelete);
    }
    Element leafEl;
    switch (node.config.type) {
        case PaneType::Chronicle:
            leafEl = renderChronicleLeaf(node.config, s, focused,
                                          focused && ui.searchMode);
            break;
        case PaneType::Inventory:
            leafEl = renderInventoryLeaf(node.config, s, focused,
                                          focused && ui.searchMode);
            break;
        case PaneType::Reconcile:
            leafEl = renderReconcileLeaf(node.config, s, focused,
                                          focused && ui.searchMode);
            break;
        case PaneType::Blank:
            leafEl = renderBlankLeaf(focused);
            break;
    }
    // Pin the leaf so ftxui doesn't grow it to fit larger content (which
    // would steal rows from a horizontally-split sibling).
    return std::move(leafEl)
        | size(WIDTH, EQUAL, width)
        | size(HEIGHT, EQUAL, height);
}

// ---- Help modal ------------------------------------------------------------

Element renderHelpModal() {
    return window(text(" Keybindings "), vbox({
        text("Global (normal mode)") | bold,
        text("  q, Ctrl-C   quit + save layout"),
        text("  ?           this help"),
        text("  r           manual refresh (also used when live-watch off)"),
        text("  Tab / S-Tab focus next / prev pane"),
        text("  c           configure focused pane"),
        text("  /           search within focused pane"),
        text("  Up/Down     move cursor in focused pane"),
        text("  PgUp/PgDn   page cursor"),
        text("  Home/End    jump to first / last row"),
        text(""),
        text("Config menu (per pane)") | bold,
        text("  Up/Down     navigate options"),
        text("  Enter       activate selected option"),
        text("  Esc         leave config mode"),
        text(""),
        text("Search input (per pane)") | bold,
        text("  Enter       apply query (query persists)"),
        text("  Esc         cancel + clear query"),
        text(""),
        text("Enter/Esc to close") | dim,
    })) | size(WIDTH, GREATER_THAN, 70);
}

// ---- Footer ----------------------------------------------------------------

Element renderStatus(const UiState& ui, const DashboardSnapshot& s) {
    std::string mode = ui.configMode ? "config"
                     : ui.searchMode ? "search"
                                      : "normal";
    std::string focus = ui.focusedLeaf < (int)ui.leaves.size()
        ? paneTitle(ui.leaves[ui.focusedLeaf]->config)
        : "-";
    std::string line;
    line += "watching " + ui.watchedPath;
    line += "  |  last refresh " + formatTime(s.refreshedAtEpoch);
    line += "  |  " + std::to_string(s.inventory.size()) + " items indexed";
    line += "  |  panes " + std::to_string(ui.leaves.size());
    line += "  |  focus: " + focus;
    line += "  |  mode: " + mode;
#if !D2R_HAVE_INOTIFY
    line += "  |  live-watch OFF (press r to reload)";
#endif
    return text(line) | dim;
}

// ---- Config-menu action + pane operations ----------------------------------

// Duplicate a leaf's config to a new PaneNode. Cursor resets to 0.
PaneNode leafCopy(const PaneConfig& c) {
    PaneNode n;
    n.isSplit = false;
    n.config = c;
    n.config.cursor = 0;
    return n;
}

// Split the given leaf into two children with the parent's config
// duplicated. `direction` selects vertical (side-by-side) or horizontal
// (stacked). Focus shifts to the new left/top child (which is the
// original config).
void splitLeaf(PaneNode& leaf, SplitDirection dir) {
    // Snapshot the current config, then convert this node into a split
    // holding two copies. The `a` child inherits the exact original config;
    // `b` gets a fresh Blank leaf so the singleton constraint isn't
    // violated for chronicle categories.
    PaneConfig original = leaf.config;
    leaf.isSplit = true;
    leaf.direction = dir;
    leaf.a = std::make_unique<PaneNode>(leafCopy(original));
    PaneNode blank;
    blank.isSplit = false;
    blank.config = {};
    leaf.b = std::make_unique<PaneNode>(std::move(blank));
    // The parent node's config fields are left in place but they are
    // unreachable via isSplit==true. Reset them for cleanliness.
    leaf.config = {};
}

// Remove `leaf` by promoting its sibling into the parent's slot. Returns
// the address of the promoted node in-place (the parent memory), or
// nullptr if `leaf` has no parent (root deletion is disallowed).
PaneNode* deletePane(PaneNode& root, PaneNode& leaf) {
    PaneNode* parent = findParent(root, &leaf);
    if (!parent) return nullptr;   // root leaf; cannot delete.
    // Sibling is whichever of parent->a / parent->b isn't `leaf`.
    std::unique_ptr<PaneNode> sibling =
        (parent->a.get() == &leaf) ? std::move(parent->b) : std::move(parent->a);
    if (!sibling) return nullptr;
    // Replace the parent split with the sibling's contents.
    *parent = std::move(*sibling);
    return parent;
}

// Cycle helpers.
template <typename Enum>
Enum cycleEnum(Enum current, std::initializer_list<Enum> values) {
    auto* it = std::find(values.begin(), values.end(), current);
    if (it == values.end() || std::next(it) == values.end()) return *values.begin();
    return *std::next(it);
}

void cyclePaneType(PaneNode& leaf) {
    // Blank -> Chronicle -> Inventory -> Reconcile -> Blank
    leaf.config.type = leaf.config.type == PaneType::Blank     ? PaneType::Chronicle
                     : leaf.config.type == PaneType::Chronicle ? PaneType::Inventory
                     : leaf.config.type == PaneType::Inventory ? PaneType::Reconcile
                                                                : PaneType::Blank;
    leaf.config.cursor = 0;
}

void cycleCategory(PaneNode& leaf) {
    leaf.config.category = cycleEnum(leaf.config.category, {
        ChronicleCategory::Sets,
        ChronicleCategory::UniquesNormal,
        ChronicleCategory::UniquesExceptional,
        ChronicleCategory::UniquesElite,
        ChronicleCategory::UniquesMisc,
        ChronicleCategory::UniquesAll,
    });
    leaf.config.cursor = 0;
}

void cycleSortKey(PaneConfig& c) {
    c.sortKey = cycleEnum(c.sortKey, {
        PaneSortKey::Name, PaneSortKey::Base, PaneSortKey::Owned,
    });
}

void cycleOwnership(PaneConfig& c) {
    c.ownership = cycleEnum(c.ownership, {
        OwnershipFilter::All,
        OwnershipFilter::RemainingOnly,
        OwnershipFilter::DiscoveredOnly,
    });
}

void cycleReconcileKind(PaneConfig& c) {
    c.reconcileKind = cycleEnum(c.reconcileKind, {
        ReconcileKindFilter::Both,
        ReconcileKindFilter::UniquesOnly,
        ReconcileKindFilter::SetsOnly,
    });
    c.cursor = 0;
}

int cyclei(int v, int mod) { if (mod <= 0) return 0; v %= mod; if (v < 0) v += mod; return v; }

} // namespace

// ============================================================================
// Driver
// ============================================================================

int runDashboard(const std::filesystem::path& savePath,
                 const std::string& referenceDbOverride,
                 const std::filesystem::path& exePath) {
    const auto dbPath = findReferenceDb(exePath, referenceDbOverride);
    if (!dbPath) {
        std::fprintf(stderr, "error: reference DB not found\n");
        return 1;
    }
    RefDb db(*dbPath);
    db.loadItemTables();

    // Locate the stash file (mirrors main.cpp findStashFile).
    std::filesystem::path stashPath;
    {
        static constexpr const char* kCandidates[] = {
            "ModernSharedStashSoftCoreV2.d2i",
            "ModernSharedStashHardCoreV2.d2i",
            "SharedStashSoftCoreV2.d2i",
            "SharedStashHardCoreV2.d2i",
        };
        for (const char* name : kCandidates) {
            auto p = savePath / name;
            std::error_code ec;
            if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) {
                stashPath = p;
                break;
            }
        }
    }

    // Open the user-config DB, load or fabricate the initial pane tree.
    sqlite3* configDb = nullptr;
    try {
        configDb = openDashboardConfigDb();
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "warning: %s (layout will not persist)\n", ex.what());
    }

    UiState ui;
    ui.watchedPath = savePath.string();
    ui.rootPane = configDb ? loadPaneTree(configDb) : makeDefaultLayout();
    {
        auto snap = std::make_shared<DashboardSnapshot>(
            buildSnapshot(db, savePath, stashPath));
        std::lock_guard g(ui.snapshotMutex);
        ui.snapshot = std::move(snap);
    }

    auto screen = ScreenInteractive::Fullscreen();

    auto currentSnapshot = [&]() {
        std::lock_guard g(ui.snapshotMutex);
        return ui.snapshot;
    };
    auto rebuild = [&]() {
        auto snap = std::make_shared<DashboardSnapshot>(
            buildSnapshot(db, savePath, stashPath));
        std::lock_guard g(ui.snapshotMutex);
        ui.snapshot = std::move(snap);
    };
    auto persistLayout = [&]() {
        if (configDb) savePaneTree(configDb, ui.rootPane);
    };

    // Renderer walks the tree, updating `ui.leaves` as it goes so event
    // handlers can address panes by index.
    auto layout = Renderer([&] {
        auto snap = currentSnapshot();
        const auto& s = *snap;
        // Rebuild the flat leaf list (pointers into rootPane).
        ui.leaves = flattenLeaves(ui.rootPane);
        if (ui.focusedLeaf < 0 || ui.focusedLeaf >= (int)ui.leaves.size()) {
            ui.focusedLeaf = 0;
        }

        Element topRow = hbox({
            renderActivePlayer(s)                      | flex,
            renderHellfireTorch(s.hellfireTorch)       | flex,
            renderColossalAncients(s.colossalAncients) | flex,
            renderTerrorZones(s.terrorZones)           | flex,
        }) | size(HEIGHT, EQUAL, 12);

        int leafIdx = 0;
        // Top panels take 12 rows; the status footer takes 1. Whatever is
        // left is what the pane tree can use. `renderPane` returns an
        // element with pinned size(W,H) so we don't need `| flex` here --
        // adding flex on top of pinned size lets ftxui expand the outer
        // element and lose our split-symmetry guarantees.
        const auto dims = ftxui::Terminal::Size();
        const int paneWidth  = std::max(1, dims.dimx);
        const int paneHeight = std::max(1, dims.dimy - 12 - 1);
        Element panes = renderPane(ui.rootPane, ui, s, leafIdx,
                                    paneWidth, paneHeight);

        std::string globalHint;
        if      (ui.configMode) globalHint = " Up/Down  Enter  Esc ";
        else if (ui.searchMode) globalHint = " type... Enter apply | Esc cancel ";
        else                    globalHint = " [Tab] focus  [c] config  [/] search  [q] quit  [?] help ";

        Element root = vbox({
            topRow,
            panes,
            hbox({ renderStatus(ui, s) | flex, text(globalHint) | dim }),
        });

        if (ui.helpVisible) root = dbox({ root, renderHelpModal() | center });
        return root;
    });

    auto postRebuild = [&] { screen.PostEvent(Event::Custom); };

    // --------------------------- event routing ---------------------------
    auto rootComp = CatchEvent(layout, [&](const Event& e) -> bool {
        // Watcher wake.
        if (e == Event::Custom) { rebuild(); return true; }

        // ---- HELP modal ----
        if (ui.helpVisible) {
            if (e == Event::Escape || e == Event::Return || e == Event::Character('?')) {
                ui.helpVisible = false;
            }
            return true;
        }

        auto focusedLeaf = [&]() -> PaneNode* {
            if (ui.focusedLeaf < 0 || ui.focusedLeaf >= (int)ui.leaves.size())
                return nullptr;
            return ui.leaves[ui.focusedLeaf];
        };

        // ---- SEARCH mode (per pane) ----
        if (ui.searchMode) {
            auto* leaf = focusedLeaf();
            if (!leaf) { ui.searchMode = false; return true; }
            auto& q = leaf->config.searchQuery;
            if (e == Event::Escape) {
                q.clear();
                leaf->config.cursor = 0;
                ui.searchMode = false;
                persistLayout();
                return true;
            }
            if (e == Event::Return) {
                leaf->config.cursor = 0;
                ui.searchMode = false;
                persistLayout();
                return true;
            }
            if (e == Event::Backspace) {
                if (!q.empty()) q.pop_back();
                return true;
            }
            // Accept typed characters (single-byte).
            if (e.is_character() && !e.character().empty()) {
                const auto& ch = e.character();
                q.append(ch);
                return true;
            }
            return true;
        }

        // ---- CONFIG mode ----
        if (ui.configMode) {
            auto* leaf = focusedLeaf();
            if (!leaf) { ui.configMode = false; return true; }
            const bool canDelete = findParent(ui.rootPane, leaf) != nullptr;
            const auto items = buildConfigMenu(leaf->config, canDelete);
            const int n = (int)items.size();
            ui.configMenu = std::clamp(ui.configMenu, 0, std::max(0, n - 1));

            // Singleton constraints are applied ONLY when leaving config
            // mode (Esc / Close / Split / Delete). While cycling options
            // we let the user land on any type or category without ripping
            // configuration out of neighbouring panes; the commit path
            // below decides which pane wins.
            auto applySingletons = [&] {
                if (leaf->config.type == PaneType::Chronicle) {
                    enforceCategorySingleton(ui.rootPane, leaf,
                                             leaf->config.category);
                } else if (leaf->config.type == PaneType::Inventory) {
                    enforceInventorySingleton(ui.rootPane, leaf);
                }
            };

            if (e == Event::Escape) {
                applySingletons();
                ui.configMode = false;
                persistLayout();
                return true;
            }
            if (e == Event::ArrowUp)   { ui.configMenu = cyclei(ui.configMenu - 1, n); return true; }
            if (e == Event::ArrowDown) { ui.configMenu = cyclei(ui.configMenu + 1, n); return true; }
            if (e == Event::Return) {
                const auto& item = items[ui.configMenu];
                const auto action = item.kind;
                switch (action) {
                    case ConfigMenuItem::CycleType:      cyclePaneType(*leaf); break;
                    case ConfigMenuItem::CycleCategory:  cycleCategory(*leaf); break;
                    case ConfigMenuItem::CycleSort:      cycleSortKey(leaf->config); break;
                    case ConfigMenuItem::ToggleSortDir:  leaf->config.sortAsc = !leaf->config.sortAsc; break;
                    case ConfigMenuItem::CycleOwnership: cycleOwnership(leaf->config); break;
                    case ConfigMenuItem::CycleReconcileKind: cycleReconcileKind(leaf->config); break;
                    case ConfigMenuItem::ToggleQuality: {
                        leaf->config.inventoryQualityMask ^= qualityBit(item.quality);
                        leaf->config.cursor = 0;
                        break;
                    }
                    case ConfigMenuItem::SplitVertical:
                    case ConfigMenuItem::SplitHorizontal: {
                        // Commit the current pane's config before duplicating.
                        applySingletons();
                        splitLeaf(*leaf,
                                  action == ConfigMenuItem::SplitVertical
                                    ? SplitDirection::Vertical
                                    : SplitDirection::Horizontal);
                        ui.configMode = false;
                        break;
                    }
                    case ConfigMenuItem::DeletePane: {
                        deletePane(ui.rootPane, *leaf);
                        ui.configMode = false;
                        // Refocus onto whatever is now at that DFS slot.
                        ui.leaves = flattenLeaves(ui.rootPane);
                        if (ui.focusedLeaf >= (int)ui.leaves.size()) {
                            ui.focusedLeaf = std::max(0, (int)ui.leaves.size() - 1);
                        }
                        break;
                    }
                    case ConfigMenuItem::Close:
                        applySingletons();
                        ui.configMode = false;
                        break;
                }
                persistLayout();
                return true;
            }
            return true;
        }

        // ---- NORMAL mode ----
        if (e == Event::Character('q')) {
            persistLayout();
            screen.ExitLoopClosure()();
            return true;
        }
        if (e == Event::Character('?')) { ui.helpVisible = true; return true; }
        if (e == Event::Character('r')) { rebuild();             return true; }
        if (e == Event::Character('c')) {
            if (focusedLeaf()) { ui.configMode = true; ui.configMenu = 0; }
            return true;
        }
        if (e == Event::Character('/')) {
            auto* leaf = focusedLeaf();
            if (leaf && (leaf->config.type == PaneType::Chronicle ||
                         leaf->config.type == PaneType::Inventory ||
                         leaf->config.type == PaneType::Reconcile)) {
                ui.searchMode = true;
            }
            return true;
        }
        if (e == Event::Tab) {
            if (!ui.leaves.empty())
                ui.focusedLeaf = cyclei(ui.focusedLeaf + 1, (int)ui.leaves.size());
            return true;
        }
        if (e == Event::TabReverse) {
            if (!ui.leaves.empty())
                ui.focusedLeaf = cyclei(ui.focusedLeaf - 1, (int)ui.leaves.size());
            return true;
        }

        // Row navigation in focused pane.
        auto* leaf = focusedLeaf();
        if (!leaf) return false;
        const auto shownRows = [&]() -> int {
            auto snap = currentSnapshot();
            if (leaf->config.type == PaneType::Chronicle)
                return (int)filterForLeaf(leaf->config, *snap).size();
            if (leaf->config.type == PaneType::Inventory)
                return (int)filterInventory(leaf->config, *snap).size();
            if (leaf->config.type == PaneType::Reconcile) {
                int n = 0;
                for (const auto& e2 : snap->reconcile) {
                    switch (leaf->config.reconcileKind) {
                        case ReconcileKindFilter::Both:        break;
                        case ReconcileKindFilter::UniquesOnly:
                            if (e2.kind != ChronicleKind::Unique) continue;
                            break;
                        case ReconcileKindFilter::SetsOnly:
                            if (e2.kind != ChronicleKind::Set) continue;
                            break;
                    }
                    if (!leaf->config.searchQuery.empty() &&
                        !containsCI(e2.displayName, leaf->config.searchQuery) &&
                        !containsCI(e2.baseName,    leaf->config.searchQuery) &&
                        !containsCI(e2.location,    leaf->config.searchQuery)) continue;
                    ++n;
                }
                return n;
            }
            return 0;
        };
        auto move = [&](int delta) {
            const int total = shownRows();
            if (total <= 0) { leaf->config.cursor = 0; return; }
            leaf->config.cursor = std::clamp(leaf->config.cursor + delta, 0, total - 1);
        };
        if (e == Event::ArrowUp)   { move(-1);  return true; }
        if (e == Event::ArrowDown) { move(+1);  return true; }
        if (e == Event::PageUp)    { move(-20); return true; }
        if (e == Event::PageDown)  { move(+20); return true; }
        if (e == Event::Home)      { leaf->config.cursor = 0; return true; }
        if (e == Event::End) {
            const int total = shownRows();
            leaf->config.cursor = std::max(0, total - 1);
            return true;
        }
        return false;
    });

    // Watcher thread (Linux inotify only). The watcher instance lives on
    // the main thread so shutdown can wake it via `shutdown()` and the
    // worker can then be joined cleanly.
#if D2R_HAVE_INOTIFY
    std::unique_ptr<DirectoryWatcher> watcher;
    std::thread watcherThread;
    try {
        watcher = std::make_unique<DirectoryWatcher>(savePath);
    } catch (const std::exception&) {
        // Watcher startup failure -> silently skip live refresh.
    }
    if (watcher) {
        try {
            watcherThread = std::thread([w = watcher.get(), &ui, postRebuild] {
                while (!ui.shutdown.load(std::memory_order_relaxed)) {
                    auto trig = w->waitForChange();
                    if (!trig) break;   // shutdown() or spurious signal
                    if (ui.shutdown.load()) break;
                    postRebuild();
                }
            });
        } catch (const std::exception&) {
            // Thread creation failure -> no live refresh.
        }
    }
#endif

    screen.Loop(rootComp);

    // Save on exit as a belt-and-braces measure (already saved on layout ops).
    persistLayout();

#if D2R_HAVE_INOTIFY
    ui.shutdown.store(true);
    if (watcher) watcher->shutdown();     // Wakes the blocked poll().
    if (watcherThread.joinable()) watcherThread.join();
    watcher.reset();
#endif
    if (configDb) closeDashboardConfigDb(configDb);
    return 0;
}

} // namespace d2r
