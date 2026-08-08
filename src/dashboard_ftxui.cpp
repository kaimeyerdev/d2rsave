// FTXUI-based dashboard: pane-tree layout, per-pane configuration, and a
// user-configurable persistent layout stored in a private SQLite DB.
//
// Only compiled when the ftxui vcpkg port is available; see CMakeLists.txt.

#include "d2r/Dashboard.hpp"
#include "d2r/DashboardConfig.hpp"
#include "d2r/DashboardModel.hpp"
#include "d2r/ItemColors.hpp"
#include "d2r/RefDb.hpp"

#if D2R_HAVE_INOTIFY
#include "d2r/BackupDb.hpp"
#include "d2r/BackupScheduler.hpp"
#include "d2r/CharacterParser.hpp"
#include "d2r/Paths.hpp"
#include "d2r/Recovery.hpp"
#include "d2r/Watcher.hpp"
#endif

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace d2r {

namespace {

using namespace ftxui;

// Shared highlight color used for section headers, session-gain
// overlays, and any pane-level "attention" accents. Kept as a single
// constant so the palette can be tweaked in one place. Cannot be
// constexpr because ftxui::Color is not a literal type; inline const
// gives us one shared instance across TUs without a definition file.
inline const Color kHighlightColor = Color::Cyan;

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

bool containsCI(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    auto lc = [](unsigned char c) { return std::tolower(c); };
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [&](char a, char b) { return lc(a) == lc(b); });
    return it != hay.end();
}

// Formatter for signed integer deltas with thousands separators and a
// leading '+' / '-'. Zero renders as "+0" so the pane always shows a
// concrete delta.
std::string formatSignedDelta(std::int64_t v) {
    if (v == 0) return "+0";
    const bool neg = v < 0;
    std::uint64_t mag = neg ? static_cast<std::uint64_t>(-v)
                            : static_cast<std::uint64_t>(v);
    std::string body = formatWithThousands(mag);
    return (neg ? "-" : "+") + body;
}

// Total XP earned at `level - 1` (i.e. baseline for the current level).
// experienceToReachLevel returns the *end* of `level`, so this is the
// value we need to convert `(level, expInLevel)` -> cumulative XP.
std::uint64_t cumulativeXp(std::uint32_t level, std::uint64_t inLevel) {
    if (level == 0) return inLevel;
    const std::uint64_t prev = experienceToReachLevel(level - 1);
    return prev + inLevel;
}

// Forward-declared here so renderers in this first anonymous namespace
// (Character, SessionLoot) can format timestamps; the definitions live
// alongside the other backup-pane helpers further down.
std::string formatWallDateTime(std::int64_t unix) {
    if (unix <= 0) return "-";
    const std::time_t t = static_cast<std::time_t>(unix);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}
std::string formatWallDate(std::int64_t unix) {
    if (unix <= 0) return "-";
    const std::time_t t = static_cast<std::time_t>(unix);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
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
    mutable std::mutex                        snapshotMutex;
    std::shared_ptr<const DashboardSnapshot>  snapshot;

    // Application-wide Session singleton. Character-agnostic; not
    // persisted across dashboard restarts. Read by buildSession and
    // by the Session / SessionLoot / Character panes' config menus.
    // Guarded by `snapshotMutex` (same mutex that guards `snapshot`
    // and `session`; the fields co-vary during rebuild).
    AppSession                                appSession;

    // Lightweight point-in-time record the Session pane diffs against.
    // Rebuilt by `buildSession` on every rebuild, but cached by
    // `sessionCacheKey` -- consecutive rebuilds during an autosave
    // burst (e.g. gem-combine) share the same key and short-circuit
    // without any byte parsing. Renderer just reads
    // `session->startState.itemKeys.contains(...)` per current item.
    std::shared_ptr<const Session>            session;
    // Cache key: character-side start date, stash-side start date,
    // effective session start epoch. `endEpoch` is deliberately NOT
    // part of the key -- endEpoch drifts every time a new save lands,
    // and the item-pool of the start-state is invariant under end
    // drift. On cache hit we clone-and-patch endEpoch (see the miss
    // path below).
    struct SessionCacheKey {
        std::int64_t characterDate = 0;
        std::int64_t stashDate     = 0;
        std::int64_t startEpoch    = 0;
        bool operator==(const SessionCacheKey& o) const noexcept {
            return characterDate == o.characterDate
                && stashDate     == o.stashDate
                && startEpoch    == o.startEpoch;
        }
    };
    SessionCacheKey                           sessionCacheKey{};

    // Per-character run stats for the current session, refreshed on
    // every rebuild(). Displayed by the Session Info pane's "Runs"
    // section. Cheap to recompute (one indexed historyFor per file);
    // done outside the buildSession cache so autosave bursts see fresh
    // run counts + duration without invalidating the item-pool cache.
    SessionRunStats                           sessionRunStats;

    // Ephemeral backup-action ring buffer for the BackupLog pane. Written
    // by the watcher/scheduler thread via setInsertCallback (and by the
    // Recovery flow), read by the render thread. `kBackupLogCap` bounds
    // memory across long dashboard sessions; older events fall off the
    // front. Cleared from the pane's config menu.
    struct BackupLogEntry {
        std::string  filename;   // raw basename, e.g. "Kai.d2s"
        std::int64_t when   = 0; // unix seconds
    };
    mutable std::mutex                        backupLogMutex;
    mutable std::deque<BackupLogEntry>        backupLog;

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

    // Persistent per-file parse cache. Populated with a full scan at
    // startup; the rebuild path only re-parses files that inotify
    // reported as changed (see pendingChangedFiles below).
    DashboardFileCache                        fileCache;

    // Names (basenames) of files the watcher told us changed since the
    // last rebuild. Written by the watcher thread under the mutex,
    // drained by the main-thread rebuild(). Empty list means "no
    // targeted invalidation" -- rebuild() then falls back to a full
    // rescan (used by manual [r] refresh and by the initial init).
    mutable std::mutex                        pendingChangesMutex;
    std::vector<std::string>                  pendingChangedFiles;

    // Print-once mode: forces the layout builder to use a fixed
    // dimension instead of querying the real tty. Empty in the
    // interactive path.
    std::optional<ftxui::Dimensions>          printSizeOverride;

    // Non-owning: the runDashboard-owned BackupDb. nullptr when the
    // DB failed to open (or when --print skipped it). renderBackupsLeaf
    // handles the nullptr case gracefully.
    BackupDb*                                 backupDb        = nullptr;
    BackupScheduler*                          backupScheduler = nullptr;

    // Cache for the Backups detail view's "exp %" column. Parsing a
    // .d2s blob is a few ms; we only compute values for the rows the
    // renderer is about to draw and remember them here. Keyed on the
    // row's `date`; nullopt means "not applicable / parse failed"
    // (tombstones, non-character files, corrupt bytes). The cache is
    // invalidated when the detail view switches to a different file.
    mutable std::string                                           backupsExpCacheFilename;
    mutable std::unordered_map<std::int64_t, std::optional<double>> backupsExpCache;

    // Backups detail collapsed view: date of the currently-expanded
    // Run (`runEndEpoch` from `groupRunsForFile`). Single-open policy
    // -- opening a new run auto-collapses the previous. `nullopt` when
    // no run is expanded. Ephemeral (not persisted).
    std::optional<std::int64_t>               expandedRunEndEpoch;

    // Per-leaf scroll fraction for panes that don't drive their
    // viewport off a row-index cursor (Session Info, Runes). Keyed by
    // the PaneNode address so a two-column layout can scroll the
    // Session pane independently of the Runes pane. Value in [0, 1]
    // feeds `focusPositionRelative(0, frac)` inside the pane's yframe;
    // 0 == top, 1 == bottom. Missing entry defaults to 0 (top).
    // Ephemeral: not persisted across dashboard restarts.
    std::unordered_map<const PaneNode*, float> paneScrollFrac;

    // Per-leaf collapse state for the Inventory pane's tree view.
    // The nested set holds the section paths the user has expanded
    // ("Shared Stash", "Kai/Equipped", etc.); a missing entry (or
    // absent leaf) means the section is collapsed. Default is empty
    // -- everything starts collapsed and the user drills in with the
    // right arrow. Search mode overrides this (all sections
    // effectively expanded) without mutating the set, so clearing
    // the query restores the user's earlier drill-in. Ephemeral:
    // not persisted across dashboard restarts.
    std::unordered_map<const PaneNode*, std::unordered_set<std::string>> paneExpanded;

    // Recovery modal state. Populated when the user presses [R] on the
    // Backups pane detail view; cleared on confirm or cancel.
    struct RecoveryModal {
        std::string  filename;        // basename of the file to restore
        std::int64_t atUnix       = 0;
        int          destChoice   = 0;   // 0=primary, 1=alt
        std::string  altPathInput;
        std::string  status;          // populated after confirm
    } recoveryModal;
    bool                                      recoveryModalVisible = false;

    // Retention editor modal (opened by [E] from the Backups summary).
    // The values are edited in the buffers; on save they're written to
    // dashboard.sqlite AND the live scheduler's retention config.
    struct RetentionModal {
        std::string daysBuf;
        std::string sessionsBuf;
        int         focused = 0;  // 0 = days, 1 = sessions
        std::string status;
    } retentionModal;
    bool                                      retentionModalVisible = false;

    // Character picker modal, opened from a Character pane's config
    // menu via ConfigMenuItem::PickCharacter. `options[0] == ""` always
    // represents "auto (newest saved)"; the rest are basenames from
    // ui.fileCache.d2s in a stable sort order.
    struct CharacterPicker {
        PaneNode*                target = nullptr;
        std::vector<std::string> options;   // "" for auto, else basenames
        int                      cursor = 0;
    } characterPicker;
    bool                                      characterPickerVisible = false;

    // Manual session-time input modal. Opened by the Session Info /
    // Session panes' "start: <auto | custom @ ...>" / "end: <...>"
    // menu actions. Text buffer is parsed by d2r::parseUserDateTime on
    // Enter; on success the pane's custom start / end date field is
    // updated and the session is rebuilt.
    struct SessionTimeInputModal {
        PaneNode*   target = nullptr;
        std::string buffer;               // user-typed input
        std::string status;               // parse error / empty on ok
        bool        isEnd  = false;
    } sessionTimeInputModal;
    bool                                      sessionTimeInputVisible = false;
};

// -----------------------------------------------------------------------
// Character pane (configurable top-row): combined active-player summary
// + session XP delta for a resolved character. Auto mode (empty
// characterSelection) tracks the newest-saved .d2s; pinned mode locks
// to a specific file stem via the pane's character picker.
// -----------------------------------------------------------------------

// Character.locations -> highest active difficulty index (0..2) + act (1..5).
// Mirror of the anonymous computeDifficulty() in dashboard_model.cpp; kept
// here so the renderer doesn't need to reach into the model TU. Cheap
// duplication -- 5 lines -- so we don't leak an internal helper.
void characterDifficultyAct(const Character& c,
                            std::uint8_t& outDiff, std::uint8_t& outAct) {
    outDiff = 0;
    outAct  = 1;
    for (std::uint8_t i = 0; i < 3; ++i) {
        if (c.locations[i].active) {
            outDiff = i;
            outAct  = c.locations[i].act ? c.locations[i].act : 1;
        }
    }
}

// Distilled per-character render inputs. Populated either from the
// snapshot's ActivePlayer (auto mode) or from a cache entry's Character
// (pinned mode). Keeping this a POD lets renderCharacterPane share one
// draw path across both cases.
struct CharacterPaneInput {
    bool           present         = false;
    std::string    file;               // ""    if snapshot's activePlayer had no file
    std::string    name;
    CharacterClass characterClass  = CharacterClass::Unknown;
    std::uint32_t  level           = 0;
    std::uint32_t  experience      = 0;
    std::uint64_t  expInLevel      = 0;
    std::uint64_t  expForLevel     = 0;
    double         expPercent      = 0.0;
    bool           hardcore        = false;
    bool           died            = false;
    std::uint8_t   difficulty      = 0;
    std::uint8_t   act             = 1;
    std::uint32_t  mapSeed         = 0;
};

CharacterPaneInput characterInputFromActivePlayer(const ActivePlayer& p) {
    CharacterPaneInput out;
    out.present        = true;
    out.file           = p.file;
    out.name           = p.name;
    out.characterClass = p.characterClass;
    out.level          = p.level;
    out.experience     = p.experience;
    out.expInLevel     = p.expInLevel;
    out.expForLevel    = p.expForLevel;
    out.expPercent     = p.expPercent;
    out.hardcore       = p.hardcore;
    out.died           = p.died;
    out.difficulty     = p.difficulty;
    out.act            = p.act;
    out.mapSeed        = p.mapSeed;
    return out;
}

CharacterPaneInput characterInputFromCache(const std::string& file,
                                            const Character& c) {
    CharacterPaneInput out;
    out.present        = true;
    out.file           = file;
    out.name           = c.name;
    out.characterClass = c.characterClass;
    out.level          = c.attributes.level ? c.attributes.level
                                            : c.level;
    out.experience     = c.attributes.experience;
    out.hardcore       = c.hardcore;
    out.died           = c.died;
    out.mapSeed        = c.mapId;
    characterDifficultyAct(c, out.difficulty, out.act);

    // Progress within the current level -- same math the model uses to
    // populate ActivePlayer, duplicated here so pinned characters don't
    // require re-running buildSnapshot.
    const std::uint64_t curFloor  = experienceToReachLevel(out.level);
    const std::uint64_t nextFloor = experienceToReachLevel(out.level + 1);
    if (nextFloor > curFloor) {
        out.expForLevel = nextFloor - curFloor;
        out.expInLevel  = out.experience > curFloor
                            ? out.experience - curFloor : 0;
        out.expPercent  = 100.0 * static_cast<double>(out.expInLevel)
                                 / static_cast<double>(out.expForLevel);
    } else {
        out.expForLevel = 0;
        out.expInLevel  = 0;
        out.expPercent  = 100.0;
    }
    return out;
}

// Resolve a Character pane's target: auto (empty selection -> snapshot's
// active player) or pinned (look up cache by filename). Returns a POD
// with `present == false` when neither path produced anything usable.
CharacterPaneInput resolveCharacterPaneInput(const PaneConfig& c,
                                             const DashboardSnapshot& s,
                                             const DashboardFileCache& cache) {
    if (c.characterSelection.empty()) {
        return s.hasActivePlayer
            ? characterInputFromActivePlayer(s.activePlayer)
            : CharacterPaneInput{};
    }
    // Selection may be stored as either a bare stem ("Kai") or the
    // full basename ("Kai.d2s"). Try both to be forgiving.
    const std::string sel = c.characterSelection;
    auto it = cache.d2s.find(sel);
    if (it == cache.d2s.end()) {
        it = cache.d2s.find(sel + ".d2s");
    }
    if (it == cache.d2s.end()) return CharacterPaneInput{};
    return characterInputFromCache(it->first, it->second.character);
}

// Draw a compact two-color XP bar. The `anchorFrac` region is filled in
// white (progress at session start), the `[anchorFrac, nowFrac]` region
// in `kHighlightColor` (progress gained during the session), and the
// remainder is dim '-'. Bar total width is fixed at `barW` characters;
// callers should give it enough horizontal space (`filler()` after is
// fine).
Element renderExpBar(double anchorFrac, double nowFrac, int barW = 40) {
    barW = std::max(4, barW);
    anchorFrac = std::clamp(anchorFrac, 0.0, 1.0);
    nowFrac    = std::clamp(nowFrac,    0.0, 1.0);
    if (nowFrac < anchorFrac) nowFrac = anchorFrac;   // XP can't drop mid-level
    int anchorChars = static_cast<int>(std::round(anchorFrac * barW));
    int nowChars    = static_cast<int>(std::round(nowFrac    * barW));
    if (nowChars < anchorChars) nowChars = anchorChars;
    if (nowChars > barW)        nowChars = barW;
    const int gainChars  = nowChars - anchorChars;
    const int emptyChars = barW - nowChars;
    // U+2588 FULL BLOCK for filled cells; U+2591 LIGHT SHADE for empty
    // so the bar has visible extent even when there's no progress yet.
    auto repeat = [](int n, std::string_view glyph) {
        std::string out;
        out.reserve(static_cast<std::size_t>(n) * glyph.size());
        for (int i = 0; i < n; ++i) out.append(glyph);
        return out;
    };
    return hbox({
        text(repeat(anchorChars, "\xE2\x96\x88")) | color(Color::White),
        text(repeat(gainChars,   "\xE2\x96\x88")) | color(kHighlightColor),
        text(repeat(emptyChars,  "\xE2\x96\x91")) | dim,
    });
}

// Full renderer for PaneType::Character. Composes the ActivePlayer
// summary (name, class, level+difficulty, seed, badges) with the
// session block (duration, XP delta, two-color exp bar). The session
// portion uses the shared Session (`ui.session`); per-character
// sessions are tracked in a follow-up commit. A mismatch between the
// pane's target and the session's tracked player is called out in a
// warning line so the delta still renders but is annotated.
Element renderCharacterPane(const PaneConfig&        cfg,
                            const DashboardSnapshot& s,
                            const Session*           session,
                            const DashboardFileCache& cache,
                            bool                     focused,
                            bool                     startPinned) {
    // Title reflects mode: " Character " (auto) or
    // " Character  Kai " (pinned to Kai).
    std::string titleStr = " Character ";
    if (!cfg.characterSelection.empty()) {
        titleStr = " Character  " + cfg.characterSelection + " ";
    } else if (startPinned) {
        titleStr = " Character (custom start) ";
    }
    Element titleEl = text(titleStr);
    if (focused) titleEl = titleEl | inverted;

    const auto in = resolveCharacterPaneInput(cfg, s, cache);
    if (!in.present) {
        return window(titleEl, vbox({
            filler(),
            hbox({ filler(),
                   text(cfg.characterSelection.empty()
                       ? "(no .d2s files found)"
                       : ("(no cached data for '" + cfg.characterSelection + "')"))
                       | dim,
                   filler() }),
            filler(),
        }));
    }

    // ---- Identity row + badges ----
    Elements badges;
    if (in.hardcore) badges.push_back(text(" HC ") | inverted | bold);
    if (!in.died)    badges.push_back(text(" alive ") | inverted);
    Element badgeLine = badges.empty() ? filler() : hbox(std::move(badges));

    const std::string lvlLine = "Level " + std::to_string(in.level)
                              + "  " + std::string(difficultyName(in.difficulty))
                              + " A" + std::to_string(in.act);
    const bool tableStale = in.expForLevel > 0 && in.expInLevel > in.expForLevel;

    // ---- Session XP delta ----
    // Only meaningful when the session start-state tracks a real player
    // and the pane's target matches (otherwise the delta compares
    // apples/oranges).
    const SessionState* startState =
        session ? &session->startState : nullptr;
    std::int64_t xpDelta    = 0;
    std::int32_t levelDelta = 0;
    double       pctDelta   = 0.0;
    bool         deltaValid = false;
    bool         anchorMismatch = false;
    if (startState && startState->hasActivePlayer) {
        anchorMismatch = (startState->playerName != in.name);
        if (!anchorMismatch) {
            const std::uint64_t aCum = cumulativeXp(startState->level, startState->expInLevel);
            const std::uint64_t nCum = cumulativeXp(in.level, in.expInLevel);
            xpDelta = static_cast<std::int64_t>(nCum) - static_cast<std::int64_t>(aCum);
            levelDelta = static_cast<std::int32_t>(in.level)
                       - static_cast<std::int32_t>(startState->level);
            const std::uint64_t anchorFloor = experienceToReachLevel(startState->level);
            const std::uint64_t anchorCeil  = experienceToReachLevel(startState->level + 1);
            const std::uint64_t anchorSpan  = anchorCeil > anchorFloor
                                                ? anchorCeil - anchorFloor : 0;
            const double anchorFrac = (anchorSpan > 0)
                ? static_cast<double>(startState->expInLevel)
                  / static_cast<double>(anchorSpan) : 0.0;
            const double nowFrac = (in.expForLevel > 0)
                ? static_cast<double>(in.expInLevel)
                  / static_cast<double>(in.expForLevel) : 0.0;
            pctDelta = 100.0 * (static_cast<double>(levelDelta)
                                + nowFrac - anchorFrac);
            deltaValid = true;
        }
    }

    // ---- Exp bar ----
    // Anchor fraction only makes sense when the start-state tracks this
    // character AND we haven't leveled up since (levelDelta == 0).
    // Otherwise start the bar's "white" segment at zero and show all
    // current progress as the highlight-colored gain -- reads as
    // "everything you see in this bar was earned this session".
    const double nowFrac = (in.expForLevel > 0)
        ? std::clamp(static_cast<double>(in.expInLevel)
                     / static_cast<double>(in.expForLevel), 0.0, 1.0)
        : 0.0;
    double anchorFrac = 0.0;
    if (deltaValid && levelDelta == 0 && startState->expInLevel > 0) {
        const std::uint64_t aFloor = experienceToReachLevel(startState->level);
        const std::uint64_t aCeil  = experienceToReachLevel(startState->level + 1);
        if (aCeil > aFloor) {
            anchorFrac = std::clamp(static_cast<double>(startState->expInLevel)
                                    / static_cast<double>(aCeil - aFloor), 0.0, 1.0);
        }
    }

    // ---- Session duration ----
    std::string durStr = "";
    if (session && session->startEpoch != 0 && session->endEpoch != 0) {
        std::int64_t secs = session->endEpoch - session->startEpoch;
        if (secs < 0) secs = 0;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lldh %02lldm %02llds",
                      static_cast<long long>(secs / 3600),
                      static_cast<long long>((secs % 3600) / 60),
                      static_cast<long long>(secs % 60));
        durStr = buf;
    }

    // ---- Format delta strings ----
    char pctBuf[32];
    if (deltaValid) std::snprintf(pctBuf, sizeof(pctBuf), "%+.2f%%", pctDelta);
    else            std::snprintf(pctBuf, sizeof(pctBuf), "---");

    const std::string pctLbl = [&]{
        char b[24];
        std::snprintf(b, sizeof(b), "%.2f%%%s", in.expPercent,
                      tableStale ? " *" : "");
        return std::string(b);
    }();
    const std::string expValues = formatWithThousands(in.expInLevel) + " / "
                                + formatWithThousands(in.expForLevel);

    // ---- Compose ----
    Elements body;
    body.push_back(hbox({
        text(in.name) | bold,
        text("  "),
        text("(" + classString(in.characterClass) + ")") | dim,
        filler(),
        badgeLine,
    }));
    body.push_back(hbox({
        text(lvlLine),
        filler(),
        text("Seed " + std::to_string(in.mapSeed)) | dim,
    }));
    if (anchorMismatch) {
        body.push_back(text("* session start was " + startState->playerName
                            + "; delta not shown") | color(Color::Yellow));
    } else if (!deltaValid && startState && !startState->hasActivePlayer) {
        body.push_back(text("* session start not yet initialised") | dim);
    }
    body.push_back(separator());
    // Session summary line: duration + delta bundled together.
    {
        Elements segs;
        segs.push_back(text("Session  ") | bold);
        segs.push_back(text(durStr.empty() ? "--" : durStr));
        segs.push_back(text("   "));
        if (deltaValid) {
            segs.push_back(text(std::string(pctBuf)) | color(kHighlightColor));
            segs.push_back(text("  "));
            segs.push_back(text(formatSignedDelta(xpDelta)) | dim);
            if (levelDelta != 0) {
                segs.push_back(text("  "));
                segs.push_back(text("(+" + std::to_string(levelDelta) + " lvl)")
                               | color(kHighlightColor));
            }
        } else {
            segs.push_back(text("--") | dim);
        }
        body.push_back(hbox(std::move(segs)));
    }
    // XP progress row (percentage of the current level).
    body.push_back(hbox({
        text("Exp  ") | bold,
        text(pctLbl),
    }));
    // Two-color XP bar. Bar width is fixed so it fits inside even the
    // narrower top-row panes; extra pane width falls to the right as
    // filler.
    body.push_back(hbox({
        renderExpBar(anchorFrac, nowFrac, 40),
        filler(),
    }));
    body.push_back(text(expValues) | dim);
    body.push_back(text("total  " + formatWithThousands(in.experience)) | dim);
    if (tableStale) {
        body.push_back(text("* exp table stale for L" + std::to_string(in.level)
                            + "; % may be off") | dim);
    }

    return window(titleEl, vbox(std::move(body)) | vscroll_indicator | frame);
}

// -----------------------------------------------------------------------
// Session Info pane (configurable top-row, formerly "Session Loot"):
// session boundaries (start / end / duration) + new uniques, sets, and
// runes gained during the current play session. Diffs the snapshot's
// inventory against the session start-state's item keys (for uniques /
// sets) and against the start-state's rune stack map (for runes).
// -----------------------------------------------------------------------

// True when `code` is a rune base code -- r00..r99 shape. Runes actually
// stop at r33 today, but the predicate stays code-shape rather than
// value-bound so a future expansion doesn't silently drop matches.
bool isRuneCode(const std::string& code) {
    return code.size() == 3
        && code[0] == 'r'
        && code[1] >= '0' && code[1] <= '9'
        && code[2] >= '0' && code[2] <= '9';
}

// Format a positive elapsed span as "Xh MMm SSs". Zero renders as
// "0h 00m 00s". Negative inputs clamp to zero.
std::string formatElapsedHMS(std::int64_t seconds) {
    if (seconds < 0) seconds = 0;
    const std::int64_t h =  seconds / 3600;
    const std::int64_t m = (seconds % 3600) / 60;
    const std::int64_t s =  seconds % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lldh %02lldm %02llds",
                  static_cast<long long>(h),
                  static_cast<long long>(m),
                  static_cast<long long>(s));
    return buf;
}

Element renderSessionLootPane(const PaneConfig&        cfg,
                              const DashboardSnapshot& s,
                              const Session*           session,
                              const SessionRunStats&   runStats,
                              bool                     focused,
                              float                    scrollFrac) {
    // Title reflects custom-window state (read from the AppSession
    // singleton via the Session struct):
    //   " Session Info "                       -- start + end both auto
    //   " Session Info (custom start) "        -- user-fixed start
    //   " Session Info (custom start + end) "  -- user-fixed start + end
    // (end-alone is impossible by invariant; see AppSession comments.)
    const bool startCustom = session && session->startIsCustom;
    const bool endCustom   = session && session->endIsCustom;
    std::string titleStr = " Session Info ";
    if (startCustom && endCustom) titleStr = " Session Info (custom start + end) ";
    else if (startCustom)         titleStr = " Session Info (custom start) ";
    Element titleEl = text(titleStr);
    if (focused) titleEl = titleEl | inverted;

    if (!session) {
        return window(titleEl, vbox({
            filler(),
            hbox({ filler(),
                   text("Session not yet initialised.") | dim,
                   filler() }),
            filler(),
        }));
    }

    // ---- Uniques / Sets diff ----
    // Identified named items not present in the session start's key set.
    std::vector<const InventoryItem*> newUniques;
    std::vector<const InventoryItem*> newSets;
    for (const auto& it : s.inventory) {
        if (!it.identified) continue;
        if (it.fingerprint == 0) continue;
        if (it.quality != ItemQuality::Unique && it.quality != ItemQuality::Set)
            continue;
        if (session->startState.itemKeys.contains({it.fingerprint, it.quality})) continue;
        (it.quality == ItemQuality::Unique ? newUniques : newSets).push_back(&it);
    }

    // ---- Runes diff ----
    // Build "now" per-code counts by scanning inventory; subtract the
    // session start's stacks; keep only positive deltas. Look up
    // display names from the current inventory (one exemplar per code).
    struct RuneDelta {
        std::string  code;
        std::string  name;
        std::int32_t delta = 0;   // positive only
    };
    std::vector<RuneDelta> runeDeltas;
    if (cfg.sessionLootShowRunes) {
        std::unordered_map<std::string, std::uint32_t> nowStacks;
        std::unordered_map<std::string, std::string>   nameByCode;
        for (const auto& it : s.inventory) {
            if (!isRuneCode(it.code)) continue;
            // Stackable material-tab entries carry their pile size in
            // `stacks`; treat a zero stacks field as a single loose rune.
            const std::uint32_t qty = it.stacks > 0 ? it.stacks : 1u;
            nowStacks[it.code] += qty;
            // First occurrence wins; runes always share the same name
            // for a given code so any exemplar is fine.
            nameByCode.try_emplace(it.code, it.name);
        }
        for (const auto& [code, now] : nowStacks) {
            const auto it = session->startState.runeStacks.find(code);
            const std::uint32_t before =
                (it == session->startState.runeStacks.end()) ? 0u : it->second;
            if (now > before) {
                RuneDelta d;
                d.code  = code;
                d.name  = nameByCode.count(code) ? nameByCode[code] : code;
                d.delta = static_cast<std::int32_t>(now - before);
                runeDeltas.push_back(std::move(d));
            }
        }
        // Runes sort in tier order (r01 lowest, r33 highest) which is
        // also lexicographic on the 3-char code.
        std::sort(runeDeltas.begin(), runeDeltas.end(),
                  [](const RuneDelta& a, const RuneDelta& b) {
                      return a.code < b.code;
                  });
    }

    // ---- Render helpers ----
    auto renderItemList = [](std::string_view header,
                             ChronicleKind kind,
                             const std::vector<const InventoryItem*>& xs) {
        Elements rows;
        rows.push_back(hbox({
            text(std::string(header)) | bold | color(kHighlightColor),
            text("  "),
            text("(" + std::to_string(xs.size()) + ")") | dim,
        }));
        if (xs.empty()) {
            rows.push_back(text("  (none)") | dim);
        } else {
            for (const auto* it : xs) {
                rows.push_back(hbox({
                    text("  "),
                    text(it->name) | color(item_colors::forKind(kind)) | flex,
                    text("  "),
                    text(it->location) | dim,
                }));
            }
        }
        return vbox(std::move(rows));
    };

    Elements body;

    // ---- Session window: Start / End / Duration ----
    // Values from the session are already correctly clamped by
    // buildSession (which honors any custom end). A trailing
    // "(custom)" suffix flags each field the user set explicitly.
    body.push_back(hbox({
        text("Start ") | bold,
        text(formatWallDateTime(session->startEpoch)),
        text(startCustom ? "  (custom)" : "") | dim,
    }));
    body.push_back(hbox({
        text("End   ") | bold,
        text(formatWallDateTime(session->endEpoch)),
        text(endCustom ? "  (custom)" : "") | dim,
    }));
    {
        std::int64_t secs = 0;
        if (session->startEpoch != 0 && session->endEpoch != 0) {
            secs = session->endEpoch - session->startEpoch;
        }
        body.push_back(hbox({
            text("Time  ") | bold,
            text(formatElapsedHMS(secs)),
        }));
    }
    body.push_back(separator());

    body.push_back(renderItemList("New Uniques", ChronicleKind::Unique, newUniques));
    body.push_back(text(""));
    body.push_back(renderItemList("New Sets", ChronicleKind::Set, newSets));

    if (cfg.sessionLootShowRunes) {
        body.push_back(text(""));
        Elements runeRows;
        runeRows.push_back(hbox({
            text("New Runes") | bold | color(kHighlightColor),
            text("  "),
            text("(" + std::to_string(runeDeltas.size()) + ")") | dim,
        }));
        if (runeDeltas.empty()) {
            runeRows.push_back(text("  (none)") | dim);
        } else {
            for (const auto& d : runeDeltas) {
                runeRows.push_back(hbox({
                    text("  "),
                    text(d.name) | flex,
                    text("  "),
                    text("+" + std::to_string(d.delta)) | bold | color(kHighlightColor),
                }));
            }
        }
        body.push_back(vbox(std::move(runeRows)));
    }

    // ---- Runs section -----------------------------------------------
    // Per-character run counts + accumulated wall-clock duration inside
    // the session window. Populated by computeSessionRunStats on every
    // rebuild (see UiState::sessionRunStats).
    {
        body.push_back(text(""));
        auto formatRunCount = [](std::int32_t n, bool ip) {
            std::string out = std::to_string(n) + (n == 1 ? " run" : " runs");
            if (ip) out += " +1";
            return out;
        };
        Elements runRows;
        std::string headerCount = std::to_string(runStats.totalRuns)
                                     + (runStats.totalRuns == 1 ? " run" : " runs");
        if (runStats.anyInProgress) headerCount += " +1 in progress";
        runRows.push_back(hbox({
            text("Runs") | bold | color(kHighlightColor),
            text("  "),
            text("(" + headerCount + ")") | dim,
        }));
        if (runStats.perCharacter.empty()) {
            runRows.push_back(text("  (none this session)") | dim);
        } else {
            for (const auto& pc : runStats.perCharacter) {
                runRows.push_back(hbox({
                    text("  "),
                    text(pc.characterName) | flex,
                    text("  "),
                    text(formatRunCount(pc.runCount, pc.hasInProgress)),
                    text("  "),
                    text(formatElapsedHMS(pc.accumulatedSecs)) | dim,
                }));
            }
            if (runStats.perCharacter.size() > 1) {
                runRows.push_back(hbox({
                    text("  Total") | bold,
                    text("  ") | flex,
                    text(formatRunCount(runStats.totalRuns,
                                          runStats.anyInProgress)) | bold,
                    text("  "),
                    text(formatElapsedHMS(runStats.totalSecs)) | bold,
                }));
            }
        }
        body.push_back(vbox(std::move(runRows)));
    }

    return window(titleEl,
                  vbox(std::move(body))
                    | focusPositionRelative(0.f, scrollFrac)
                    | vscroll_indicator | yframe | flex);
}

// -----------------------------------------------------------------------
// Uber pane (configurable top-row): keys + torch + optional uber-boss
// drops + Ancients statues + optional per-class Hellfire Torch tally.
// -----------------------------------------------------------------------

Element renderUberPane(const PaneConfig&         cfg,
                       const DashboardSnapshot&  s,
                       const DashboardFileCache& cache,
                       bool                      focused) {
    Element titleEl = text(" Uber ");
    if (focused) titleEl = titleEl | inverted;

    // Compact "label ......  N" row. Right-aligned count column of
    // fixed width so a stack of rows lines up.
    auto row = [](const char* label, std::uint32_t n, int labelWidth = 22) {
        return hbox({
            text(label) | size(WIDTH, EQUAL, labelWidth),
            filler(),
            text(std::to_string(n)) | bold | align_right | size(WIDTH, EQUAL, 6),
        });
    };

    const auto& hf = s.hellfireTorch;
    const auto& ca = s.colossalAncients;

    Elements body;

    // ---- Keys + Torch (always visible) ----
    body.push_back(row("Key of Terror",       hf.keysTerror));
    body.push_back(row("Key of Hate",         hf.keysHate));
    body.push_back(row("Key of Destruction",  hf.keysDestruction));
    body.push_back(row("Hellfire Torch",      hf.torchesHellfire));

    // ---- Uber drops (optional) ----
    if (cfg.uberShowUbers) {
        body.push_back(separator());
        body.push_back(row("Diablo's Horn",     hf.diablosHorn));
        body.push_back(row("Mephisto's Brain",  hf.mephistosBrain));
        body.push_back(row("Baal's Eye",        hf.baalsEye));
    }

    // ---- Ancients (always visible) ----
    body.push_back(separator());
    body.push_back(row("Talic's Anguish",       ca.talicAnguish));
    body.push_back(row("Korlic's Pain",         ca.korlicPain));
    body.push_back(row("Madawc's Ire",          ca.madawcIre));
    body.push_back(row("Bul-Kathos' Nightmare", ca.bulKathosNightmare));
    body.push_back(row("Worusk's End",          ca.woruskEnd));

    // ---- Torch by class (optional) ----
    if (cfg.uberShowTorchByClass) {
        body.push_back(separator());
        body.push_back(text(" Torches by class ") | bold | color(kHighlightColor));

        // Aggregate per-character-owned torches by that character's class.
        // Iterate the file cache directly rather than walking snap.inventory
        // since D2sEntry.hellfire.torchesHellfire is already per-file.
        std::unordered_map<CharacterClass, std::uint32_t> byClass;
        std::uint32_t stashTorches = 0;
        for (const auto& [name, entry] : cache.d2s) {
            if (entry.hellfire.torchesHellfire == 0) continue;
            byClass[entry.character.characterClass] += entry.hellfire.torchesHellfire;
        }
        if (cache.stash) {
            stashTorches = cache.stash->hellfire.torchesHellfire;
        }

        // Stable render order: Amazon, Sorc, Nec, Pal, Barb, Druid, Assn,
        // Stash. Rows with zero counts are dimmed so the pane still
        // fits a compact height with 8 slots.
        struct Row { const char* label; std::uint32_t count; };
        std::vector<Row> rows{
            {"Amazon",      byClass[CharacterClass::Amazon]},
            {"Sorceress",   byClass[CharacterClass::Sorceress]},
            {"Necromancer", byClass[CharacterClass::Necromancer]},
            {"Paladin",     byClass[CharacterClass::Paladin]},
            {"Barbarian",   byClass[CharacterClass::Barbarian]},
            {"Druid",       byClass[CharacterClass::Druid]},
            {"Assassin",    byClass[CharacterClass::Assassin]},
            {"Stash",       stashTorches},
        };
        for (const auto& r : rows) {
            auto lineHbox = hbox({
                text(std::string(" ") + r.label) | size(WIDTH, EQUAL, 22),
                filler(),
                text(std::to_string(r.count)) | bold | align_right | size(WIDTH, EQUAL, 6),
            });
            body.push_back(r.count == 0 ? (lineHbox | dim) : lineHbox);
        }
    }

    return window(titleEl,
                  vbox(std::move(body)) | vscroll_indicator | yframe | flex);
}

// -----------------------------------------------------------------------
// Terror Zone pane (configurable top-row): the five Worldstone shard
// tallies from the current terror-zone rotation drops. No configurable
// options today; kept as its own pane type so it can slot into any
// layout position.
// -----------------------------------------------------------------------

Element renderTerrorZonePane(const DashboardSnapshot& s, bool focused) {
    Element titleEl = text(" Terror Zone ");
    if (focused) titleEl = titleEl | inverted;

    auto row = [](const char* label, std::uint32_t n) {
        return hbox({
            text(label) | size(WIDTH, EQUAL, 28),
            filler(),
            text(std::to_string(n)) | bold | align_right | size(WIDTH, EQUAL, 6),
        });
    };

    const auto& t = s.terrorZones;
    return window(titleEl, vbox({
        row("Western Worldstone Shard",  t.shardWestern),
        row("Eastern Worldstone Shard",  t.shardEastern),
        row("Southern Worldstone Shard", t.shardSouthern),
        row("Deep Worldstone Shard",     t.shardDeep),
        row("Northern Worldstone Shard", t.shardNorthern),
    }));
}

// -------------------------- chronicle rendering -----------------------------

std::string sortKeyLabel(PaneSortKey k) {
    switch (k) {
        case PaneSortKey::Name:  return "name";
        case PaneSortKey::Base:  return "base";
        case PaneSortKey::Owned: return "owned";
        case PaneSortKey::Tier:  return "tier";
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
            // Tier is handled by renderChronicleByTier's own grouping.
            // If we reach this comparator with Tier active (e.g. a
            // Sets pane loaded stale JSON), fall back to base-name
            // order so the flat list is still coherent.
            case PaneSortKey::Tier:  return lc(a->baseName)    < lc(b->baseName);
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

// ---- Inventory tree ---------------------------------------------------
// The Inventory pane groups items into a two-level tree:
//
//   * Shared Stash
//       Tab 1..5              (grid-tabs; canonical Base column)
//       Gems / Runes / Materials
//                             (material tab; count column instead of Base)
//   * Character-name(s)       (one node per .d2s file, alphabetical)
//       Equipped / Inventory / Stash / Belt / Cube
//       Merc / Corpse / Iron Golem
//                             (all standard sections, Base column)
//
// Sub-nodes with zero items are elided. Section-path strings (e.g.
// "Shared Stash/Runes", "Kai/Equipped") are the keys the pane uses to
// track expand/collapse state in `UiState::paneExpanded`.
// ------------------------------------------------------------------------

// Recognise gem base codes: three chars starting with 'g' (chipped /
// flawed / normal / flawless / perfect ruby / sapphire / amethyst /
// topaz / emerald / diamond) or 's' + 'k' (the skull variants sit in
// the same in-game bucket as the coloured gems).
[[nodiscard]] inline bool isGemCode(std::string_view code) noexcept {
    if (code.size() != 3) return false;
    if (code[0] == 'g') return true;
    return code[0] == 's' && code[1] == 'k';
}

// Ordered sub-section labels. Sections not present in the item pool
// are elided at build time; the arrays only fix the display order.
constexpr std::array<std::string_view, 8> kSharedSubOrder{{
    "Tab 1", "Tab 2", "Tab 3", "Tab 4", "Tab 5",
    "Gems",  "Runes", "Materials",
}};
constexpr std::array<std::string_view, 8> kCharSubOrder{{
    "Equipped",  "Inventory", "Stash",  "Belt", "Cube",
    "Merc",      "Corpse",    "Iron Golem",
}};

// Result of classifying one item into (top-level, sub-level).
struct InvClassification {
    std::string topLevel;
    std::string subLevel;
    bool        isCountSection = false;
};

// Map an item to its tree location. Shared-stash tab 6 splits by code
// (runes / gems / materials); tabs 1-5 stay as grid sub-nodes. Character
// files use the pre-computed `it.subLocation` and derive the top-level
// name from the filename stem (`Kai.d2s (merc)` -> `Kai`).
[[nodiscard]] InvClassification classifyInventoryItem(const InventoryItem& it) {
    InvClassification out;
    static constexpr std::string_view kStashPrefix = "stash tab ";
    if (it.location.size() >= kStashPrefix.size() &&
        it.location.compare(0, kStashPrefix.size(), kStashPrefix) == 0) {
        out.topLevel = "Shared Stash";
        // Parse the tab number after the prefix. Any non-digit / missing
        // number falls into the materials bucket rather than a bogus
        // "Tab ?" section.
        int tab = 0;
        for (std::size_t i = kStashPrefix.size(); i < it.location.size(); ++i) {
            const char ch = it.location[i];
            if (ch < '0' || ch > '9') break;
            tab = tab * 10 + (ch - '0');
        }
        if (tab >= 1 && tab <= 5) {
            out.subLevel = "Tab " + std::to_string(tab);
        } else {
            if      (isRuneCode(it.code)) out.subLevel = "Runes";
            else if (isGemCode(it.code))  out.subLevel = "Gems";
            else                          out.subLevel = "Materials";
            out.isCountSection = true;
        }
        return out;
    }
    // Character source: strip ".d2s" (and anything after) for the
    // top-level name. `it.subLocation` is populated at aggregation
    // time by characterItemSubLoc / merc / corpse / iron-golem
    // passes; fall back to "Inventory" if it's empty for any reason.
    const auto dot = it.location.find(".d2s");
    out.topLevel = dot == std::string::npos
        ? it.location
        : it.location.substr(0, dot);
    out.subLevel = it.subLocation.empty() ? "Inventory" : it.subLocation;
    return out;
}

struct InvSection {
    std::string label;
    std::string path;                    // "TopLevel/SubLabel"
    std::vector<const InventoryItem*> items;
    bool        isCountSection = false;
    std::uint32_t stackSum = 0;          // sum of stacks (loose = 1)
};
struct InvTopLevel {
    std::string label;
    std::string path;                    // = label (top-level is flat)
    std::vector<InvSection> sections;
    int         totalItems = 0;
};
using InvTree = std::vector<InvTopLevel>;

struct InvVisibleRow {
    enum class Kind : std::uint8_t { TopLevel, Section, Item };
    Kind        kind        = Kind::TopLevel;
    int         depth       = 0;         // 0 top, 1 section, 2 item
    std::string label;                   // header text (unused for items)
    std::string path;                    // section/top-level path (empty for items)
    std::string parentPath;              // path of enclosing header
    const InventoryItem* item = nullptr; // only set for items
    bool        expanded    = false;     // header only
    int         itemCount   = 0;         // header only
    std::uint32_t stackSum  = 0;         // count-section header only
    bool        isCountSection = false;
};

// Bucket filtered items into the tree structure, applying the canonical
// ordering and sorting items alphabetically within each section.
[[nodiscard]] InvTree buildInventoryTree(
        const std::vector<const InventoryItem*>& items) {
    // Preserve ordering deterministically by using std::map for both
    // levels of the outer bucketing. The canonical sub-order arrays
    // then re-arrange sections into the final order below.
    std::map<std::string, std::map<std::string, std::vector<const InventoryItem*>>> bucket;
    std::map<std::string, std::map<std::string, bool>> countFlag;
    for (auto* it : items) {
        auto c = classifyInventoryItem(*it);
        bucket[c.topLevel][c.subLevel].push_back(it);
        if (c.isCountSection) countFlag[c.topLevel][c.subLevel] = true;
    }
    for (auto& [top, subs] : bucket) {
        for (auto& [sub, sitems] : subs) {
            std::sort(sitems.begin(), sitems.end(),
                      [](const InventoryItem* a, const InventoryItem* b) {
                          return a->name < b->name;
                      });
        }
    }

    // Top-level order: Shared Stash first (if present), then characters
    // alphabetically (std::map iterates sorted, so we can just skip
    // Shared Stash on the first pass).
    std::vector<std::string> topOrder;
    if (bucket.count("Shared Stash")) topOrder.push_back("Shared Stash");
    for (const auto& [top, _] : bucket) {
        if (top != "Shared Stash") topOrder.push_back(top);
    }

    InvTree tree;
    for (const auto& top : topOrder) {
        InvTopLevel tl;
        tl.label = top;
        tl.path  = top;

        const auto& subs = bucket[top];
        const bool isShared = (top == "Shared Stash");
        const auto& order = isShared ? kSharedSubOrder : kCharSubOrder;

        std::set<std::string> emitted;
        auto pushSection = [&](std::string_view label) {
            const std::string key(label);
            auto it = subs.find(key);
            if (it == subs.end() || it->second.empty()) return;
            InvSection sec;
            sec.label = key;
            sec.path  = top + "/" + key;
            sec.items = it->second;
            sec.isCountSection = countFlag[top].count(key) > 0;
            for (auto* p : sec.items) sec.stackSum += p->stacks > 0 ? p->stacks : 1u;
            tl.sections.push_back(std::move(sec));
            emitted.insert(key);
        };
        for (auto label : order) pushSection(label);
        // Any sub-labels we don't recognise (defensive; shouldn't
        // happen unless a future patch adds a new bucket) slide in
        // after the canonical order in alphabetical order.
        for (const auto& [sub, sitems] : subs) {
            if (emitted.count(sub)) continue;
            if (sitems.empty()) continue;
            InvSection sec;
            sec.label = sub;
            sec.path  = top + "/" + sub;
            sec.items = sitems;
            sec.isCountSection = countFlag[top][sub];
            for (auto* p : sec.items) sec.stackSum += p->stacks > 0 ? p->stacks : 1u;
            tl.sections.push_back(std::move(sec));
        }
        for (const auto& sec : tl.sections) tl.totalItems += (int)sec.items.size();
        if (tl.totalItems > 0) tree.push_back(std::move(tl));
    }
    return tree;
}

// Flatten the tree into the ordered visible-row list, honouring the
// user's collapse state (`expandedPaths`). A non-empty search query
// force-expands everything so matched items are always shown; the
// user's expand set is preserved so a subsequent Esc-clear returns
// to the pre-search shape.
[[nodiscard]] std::vector<InvVisibleRow> emitInventoryRows(
        const InvTree& tree,
        const std::unordered_set<std::string>& expandedPaths,
        bool searchActive) {
    std::vector<InvVisibleRow> rows;
    for (const auto& tl : tree) {
        InvVisibleRow tr;
        tr.kind      = InvVisibleRow::Kind::TopLevel;
        tr.depth     = 0;
        tr.label     = tl.label;
        tr.path      = tl.path;
        tr.itemCount = tl.totalItems;
        tr.expanded  = searchActive || expandedPaths.count(tl.path) > 0;
        rows.push_back(tr);
        if (!tr.expanded) continue;
        for (const auto& sec : tl.sections) {
            InvVisibleRow sr;
            sr.kind        = InvVisibleRow::Kind::Section;
            sr.depth       = 1;
            sr.label       = sec.label;
            sr.path        = sec.path;
            sr.parentPath  = tl.path;
            sr.isCountSection = sec.isCountSection;
            sr.itemCount   = (int)sec.items.size();
            sr.stackSum    = sec.stackSum;
            sr.expanded    = searchActive || expandedPaths.count(sec.path) > 0;
            rows.push_back(sr);
            if (!sr.expanded) continue;
            for (auto* it : sec.items) {
                InvVisibleRow ir;
                ir.kind        = InvVisibleRow::Kind::Item;
                ir.depth       = 2;
                ir.item        = it;
                ir.parentPath  = sec.path;
                ir.isCountSection = sec.isCountSection;
                rows.push_back(ir);
            }
        }
    }
    return rows;
}

// Collect all section-and-top-level paths a tree exposes, used to
// implement the `+` "expand all" key without needing to walk the item
// pool separately.
[[nodiscard]] std::vector<std::string> collectExpandablePaths(const InvTree& tree) {
    std::vector<std::string> out;
    for (const auto& tl : tree) {
        out.push_back(tl.path);
        for (const auto& sec : tl.sections) out.push_back(sec.path);
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

// Render the Uniques "By Tier" grid: one row per tier family with three
// columns (Normal / Exceptional / Elite). Families with more than one
// unique in some tier expand into consecutive rows -- row i takes the
// i-th unique in each tier's bucket, or a blank cell when that bucket
// is shorter. Families are further bucketed by the item-class type of
// their Normal-tier base (Swords, Body Armor, ...); each item-class
// bucket gets a small header banner and a leading separator, but rows
// within a bucket are separated only by their natural whitespace so
// the grid stays scannable.
//
// The renderer skips filterForLeaf's per-row filter and applies
// ownership + search filters at family level (a family stays visible
// if at least one of its cells satisfies both filters), so the
// Normal/Ex/Elite alignment isn't lost when some cells fail the
// filter.
//
// Only called when c.category == UniquesAll and c.sortKey == Tier.
Element renderChronicleByTier(const PaneConfig& c, const DashboardSnapshot& s,
                              bool focused, bool searchMode) {
    // Family record: three ordered buckets (Normal / Exceptional /
    // Elite) plus a sort key derived from the Normal-tier base name
    // (fallback: Exceptional, then Elite) plus the family's item-class
    // slug (used to group families into headed sections).
    struct Family {
        std::array<std::vector<const ChronicleRow*>, 3> buckets{};
        std::string sortKey;   // lowercased Normal-tier base name (fallback below)
        std::string typeSlug;  // familyType of any member (all share it)
        int         level = 0; // familyLevel of any member (all share it)
    };
    auto lc = [](std::string t) {
        for (auto& ch : t) ch = std::tolower(static_cast<unsigned char>(ch));
        return t;
    };
    auto tierSlot = [](ChronicleTier t) -> int {
        switch (t) {
            case ChronicleTier::Normal:      return 0;
            case ChronicleTier::Exceptional: return 1;
            case ChronicleTier::Elite:       return 2;
            default:                          return -1;
        }
    };

    // Group: familyKey (fallback baseCode) -> Family.
    //
    // No-tier uniques (Rainbow Facets, Colossal Jewels, and any other
    // misc-based uniques with baseCode not in the tier map) also
    // appear in this grid: they land in slot 0 (Normal), keyed on the
    // display name so each variant becomes its own single-cell family
    // (Rainbow Facets share baseCode "jew" but must not stack into one
    // family). They're tagged with a sentinel typeSlug so the group
    // ranker below places them in a "Miscellaneous" section at the
    // end of the grid.
    static constexpr const char* kMiscTypeSlug = "_misc_";
    std::unordered_map<std::string, Family> byKey;
    for (const auto& r : s.chronicle) {
        if (r.kind != ChronicleKind::Unique) continue;
        int slot = tierSlot(r.tier);
        std::string key;
        bool isMisc = false;
        if (slot < 0) {
            // No tier -> misc. Force to Normal column, unique-per-name
            // family so tier variants don't merge (Rainbow Facets all
            // share "jew" but must render 8 rows, not 1).
            slot   = 0;
            isMisc = true;
            key    = "misc:" + r.displayName;
        } else {
            key = !r.familyKey.empty() ? r.familyKey : r.baseCode;
            if (key.empty()) continue;
        }
        auto& fam = byKey[key];
        fam.buckets[slot].push_back(&r);
        if (isMisc) {
            fam.typeSlug = kMiscTypeSlug;
        } else if (fam.typeSlug.empty()) {
            fam.typeSlug = r.familyType;
        }
        if (fam.level == 0) fam.level = r.familyLevel;
    }
    if (byKey.empty()) {
        Element titleEl = text(" " + paneTitle(c) + " ");
        if (focused) titleEl = titleEl | inverted;
        return window(titleEl,
            vbox({ text(" (no tier families)") | dim }) | flex);
    }

    // Stable-sort within each bucket by displayName and compute each
    // family's sortKey.
    for (auto& [key, fam] : byKey) {
        for (auto& bucket : fam.buckets) {
            std::stable_sort(bucket.begin(), bucket.end(),
                [&](const ChronicleRow* a, const ChronicleRow* b) {
                    return lc(a->displayName) < lc(b->displayName);
                });
        }
        for (int t = 0; t < 3; ++t) {
            if (!fam.buckets[t].empty()) {
                fam.sortKey = lc(fam.buckets[t].front()->baseName);
                break;
            }
        }
    }

    // Family-level filter: hide only when every non-blank cell fails.
    auto familySurvives = [&](const Family& fam) {
        const bool hasQuery = !c.searchQuery.empty();
        for (int t = 0; t < 3; ++t) {
            for (const auto* r : fam.buckets[t]) {
                if (r->discovered  && c.ownership == OwnershipFilter::RemainingOnly)  continue;
                if (!r->discovered && c.ownership == OwnershipFilter::DiscoveredOnly) continue;
                if (hasQuery) {
                    const bool hit = containsCI(r->displayName, c.searchQuery)
                                  || containsCI(r->baseName,    c.searchQuery);
                    if (!hit) continue;
                }
                return true;
            }
        }
        return false;
    };

    // Item-class banner + sort priority per `type` slug. Priority
    // orders armor (100s) before generic weapons (200s) before class-
    // restricted weapons (400s); alphabetic tiebreak by display name
    // for anything not in the table.
    struct GroupInfo { const char* name; int rank; };
    auto groupInfoFor = [](std::string_view slug) -> GroupInfo {
        // clang-format off
        if (slug == "tors") return {"Body Armor",          100};
        if (slug == "helm") return {"Helms",               110};
        if (slug == "pelt") return {"Druid Pelts",         111};
        if (slug == "phlm") return {"Barbarian Helms",     112};
        if (slug == "circ") return {"Circlets",            113};
        if (slug == "shie") return {"Shields",             120};
        if (slug == "head") return {"Necromancer Heads",   121};
        if (slug == "ashd") return {"Paladin Shields",     122};
        if (slug == "grim") return {"Warlock Tomes",      123};
        if (slug == "glov") return {"Gloves",              130};
        if (slug == "boot") return {"Boots",               140};
        if (slug == "belt") return {"Belts",               150};
        if (slug == "swor") return {"Swords",              200};
        if (slug == "axe")  return {"Axes",                210};
        if (slug == "mace") return {"Maces",               220};
        if (slug == "hamm") return {"Hammers",             221};
        if (slug == "club") return {"Clubs",               222};
        if (slug == "scep") return {"Scepters",            230};
        if (slug == "pole") return {"Polearms",            240};
        if (slug == "spea") return {"Spears",              241};
        if (slug == "jave") return {"Javelins",            250};
        if (slug == "knif") return {"Daggers",             260};
        if (slug == "taxe") return {"Throwing Axes",       261};
        if (slug == "tkni") return {"Throwing Knives",     262};
        if (slug == "tpot") return {"Throwing Potions",    263};
        if (slug == "staf") return {"Staves",              270};
        if (slug == "wand") return {"Wands",               271};
        if (slug == "bow")  return {"Bows",                280};
        if (slug == "xbow") return {"Crossbows",           281};
        if (slug == "h2h" || slug == "h2h2")
                            return {"Assassin Claws",      400};
        if (slug == "abow") return {"Amazon Bows",         410};
        if (slug == "ajav") return {"Amazon Javelins",     411};
        if (slug == "aspe") return {"Amazon Spears",       412};
        if (slug == "orb")  return {"Sorceress Orbs",      420};
        // Sentinel used above for uniques whose base has no tier
        // (jewels, misc charms without a tier map entry). Rank above
        // "Other" so they sit at the very end of the grid; ascending
        // sort therefore matches the user request "let them appear
        // at the end". Descending sort naturally flips them to the
        // top, which is the reasonable inverse.
        if (slug == kMiscTypeSlug) return {"Miscellaneous", 1000};
        return {"Other",                                   999};
        // clang-format on
    };

    // Assemble surviving families and sort them by (group rank, family
    // base name) so all Swords sit together, all Body Armor sits
    // together, etc.
    std::vector<const Family*> families;
    families.reserve(byKey.size());
    for (const auto& [key, fam] : byKey) {
        if (familySurvives(fam)) families.push_back(&fam);
    }
    auto groupNameOf = [&](const Family* f) {
        return groupInfoFor(f->typeSlug).name;
    };
    auto groupRankOf = [&](const Family* f) {
        return groupInfoFor(f->typeSlug).rank;
    };
    std::stable_sort(families.begin(), families.end(),
        [&](const Family* a, const Family* b) {
            const int ra = groupRankOf(a);
            const int rb = groupRankOf(b);
            if (ra != rb) return c.sortAsc ? ra < rb : ra > rb;
            const std::string na = groupNameOf(a);
            const std::string nb = groupNameOf(b);
            if (na != nb) return c.sortAsc ? na < nb : na > nb;
            // Primary within-group order: base item level (qlvl) so
            // Body Armor reads Quilted (1) -> Leather (3) -> ... ->
            // Ancient Armor (40). Base name is the tiebreaker so
            // families with equal levels (rare but possible) still
            // have a deterministic order.
            if (a->level != b->level)
                return c.sortAsc ? a->level < b->level
                                 : a->level > b->level;
            return c.sortAsc ? a->sortKey < b->sortKey
                             : a->sortKey > b->sortKey;
        });

    // Column widths: cap similar to measureChronicle. Discovered mark
    // ("✓ ") lives inside the cell string so cell width just accounts
    // for the widest per-cell text (which grows at info level 3 to
    // include "ItemName (BaseName)").
    const int infoLevel = std::clamp(c.infoLevel, 1, 3);
    auto cellTextFor = [&](const ChronicleRow* r) -> std::string {
        if (!r) return {};
        std::string s = r->discovered ? "\xE2\x9C\x93 " : "  ";
        s += r->displayName;
        if (infoLevel >= 3 && !r->baseName.empty()) {
            s += " (" + r->baseName + ")";
        }
        return s;
    };
    // Family-label column width (info level >=2): widest Normal-tier
    // base name (fallback Exceptional -> Elite when Normal missing).
    auto familyLabelFor = [](const Family* fam) -> std::string {
        for (int t = 0; t < 3; ++t) {
            if (!fam->buckets[t].empty()) return fam->buckets[t].front()->baseName;
        }
        return {};
    };
    std::array<int, 3> colW{4, 4, 4};
    for (const auto* fam : families) {
        for (int t = 0; t < 3; ++t) {
            for (const auto* r : fam->buckets[t]) {
                const int w = static_cast<int>(cellTextFor(r).size()) + 2;
                // Cap widens at higher info levels so "(BaseName)"
                // suffixes don't get truncated.
                const int cap = infoLevel >= 3 ? 56 : 36;
                colW[t] = std::min(cap, std::max(colW[t], w));
            }
        }
    }
    int famLabelW = 0;
    if (infoLevel >= 2) {
        famLabelW = 6;   // header text "Family" fallback minimum
        for (const auto* fam : families) {
            const int w = static_cast<int>(familyLabelFor(fam).size()) + 2;
            famLabelW = std::min(28, std::max(famLabelW, w));
        }
    }

    // Header row: [Family] Normal | Exceptional | Elite.
    Elements header;
    if (infoLevel >= 2) header.push_back(cellText("Family", famLabelW));
    header.push_back(cellText("Normal",      colW[0]));
    header.push_back(cellText("Exceptional", colW[1]));
    header.push_back(cellText("Elite",       colW[2]));

    // Per-group discovered/total counts. Counts every Unique in the
    // group regardless of the pane's ownership/search filters -- the
    // banner should show "how much of this armor type do I own", not
    // "how much of what's currently visible". Iterates byKey (which
    // holds ALL families) so the counts stay stable as the user
    // toggles filters.
    struct GroupStats { int discovered = 0; int total = 0; };
    std::unordered_map<std::string, GroupStats> statsByGroup;
    for (const auto& [key, fam] : byKey) {
        const std::string grp = groupInfoFor(fam.typeSlug).name;
        auto& st = statsByGroup[grp];
        for (int t = 0; t < 3; ++t) {
            for (const auto* r : fam.buckets[t]) {
                ++st.total;
                if (r->discovered) ++st.discovered;
            }
        }
    }

    // Flatten families into display rows. Each row is either a
    // group-header banner or a data row; the cursor only lands on
    // data rows. `familyLabel` is populated on the first data row of
    // each family (info level >=2 only) so the label doesn't repeat
    // on continuation rows for multi-unique families.
    struct DisplayRow {
        std::array<const ChronicleRow*, 3> cells{};
        std::string  header;         // non-empty when this is a group banner
        std::string  familyLabel;    // first data row of a family (info level >=2)
        bool         allOwned = false;  // whole family owned -> dim the label
    };
    std::vector<DisplayRow> display;
    display.reserve(families.size() * 2);
    std::string lastGroup;
    for (const auto* fam : families) {
        const std::string grp = groupNameOf(fam);
        if (grp != lastGroup) {
            DisplayRow banner;
            banner.header = grp;
            display.push_back(banner);
            lastGroup = grp;
        }
        std::size_t rowCount = 0;
        for (int t = 0; t < 3; ++t) rowCount = std::max(rowCount, fam->buckets[t].size());
        // Compute the "all owned" flag once per family for dimming.
        bool allOwned = true;
        bool anyRow = false;
        for (int t = 0; t < 3; ++t) {
            for (const auto* r : fam->buckets[t]) {
                anyRow = true;
                if (!r->discovered) { allOwned = false; break; }
            }
            if (!allOwned) break;
        }
        allOwned = allOwned && anyRow;
        for (std::size_t i = 0; i < rowCount; ++i) {
            DisplayRow dr;
            for (int t = 0; t < 3; ++t) {
                dr.cells[t] = i < fam->buckets[t].size() ? fam->buckets[t][i] : nullptr;
            }
            if (i == 0) {
                dr.familyLabel = familyLabelFor(fam);
                dr.allOwned    = allOwned;
            }
            display.push_back(dr);
        }
    }

    // Map absolute display-row index -> is-data-row so cursor
    // navigation skips banners. We store the count of data rows for
    // clamping; navigation logic elsewhere still moves by ±1 across
    // all display rows -- landing on a banner just doesn't highlight.
    const int shown = static_cast<int>(display.size());
    int dataRowsBefore = 0;
    for (const auto& dr : display) {
        if (dr.header.empty()) ++dataRowsBefore;
    }
    const int maxCursor = std::max(0, dataRowsBefore - 1);
    const int clampedDataCursor = std::clamp(c.cursor, 0, maxCursor);
    // Translate the data-row cursor into an absolute display-row index.
    int cursorRow = -1;
    {
        int dataIdx = 0;
        for (int i = 0; i < shown; ++i) {
            if (!display[static_cast<std::size_t>(i)].header.empty()) continue;
            if (dataIdx == clampedDataCursor) { cursorRow = i; break; }
            ++dataIdx;
        }
    }

    Elements body;
    body.reserve(static_cast<std::size_t>(shown + 2));
    body.push_back(hbox(std::move(header)) | bold);
    body.push_back(separator());
    bool firstBanner = true;
    for (int i = 0; i < shown; ++i) {
        const auto& dr = display[static_cast<std::size_t>(i)];
        if (!dr.header.empty()) {
            // The column-header row above already draws a separator,
            // so skip the extra one for the very first banner --
            // otherwise we get two horizontal lines back-to-back.
            if (!firstBanner) {
                body.push_back(separator() | dim);
            }
            // Banner styling: bold + cyan makes item-class rows read
            // as a section header instead of getting mistaken for a
            // family label like "Quilted Armor" (which is bold black).
            // The trailing "  N/M  (pct%)" is appended in dim so the
            // group name stays visually dominant.
            const auto& st  = statsByGroup[dr.header];
            const int   pct = st.total > 0
                ? static_cast<int>(std::round(100.0 * st.discovered / st.total))
                : 0;
            const std::string countTxt = "  " + std::to_string(st.discovered)
                                       + "/" + std::to_string(st.total)
                                       + "  (" + std::to_string(pct) + "%)";
            body.push_back(hbox({
                text(" " + dr.header) | bold | color(kHighlightColor),
                text(countTxt) | dim,
            }));
            firstBanner = false;
            continue;
        }
        Elements cells;
        if (infoLevel >= 2) {
            Element label = cellText(dr.familyLabel, famLabelW);
            if (dr.familyLabel.empty())    label = label;               // blank continuation row
            else if (dr.allOwned)          label = label | dim;
            else                           label = label | bold;
            cells.push_back(label);
        }
        for (int t = 0; t < 3; ++t) {
            const auto* r = dr.cells[t];
            const std::string txt = cellTextFor(r);
            // Color owned cells with the muted unique-gold and unowned
            // cells with full unique-gold so a row with mixed ownership
            // still lets the eye pick out the unowned tiers at a glance.
            Element cell = cellText(txt, colW[t]);
            if (r) cell = cell | color(item_colors::forKind(
                                          ChronicleKind::Unique, r->discovered));
            cells.push_back(cell);
        }
        Element row = hbox(std::move(cells));
        if (focused && i == cursorRow) row = row | inverted | ftxui::focus;
        body.push_back(row);
    }

    // Status + title mirror renderChronicleLeaf so panes feel consistent.
    std::string status = "sort: " + sortKeyLabel(c.sortKey)
                       + (c.sortAsc ? " asc" : " desc")
                       + " | show: " + ownershipLabel(c.ownership);
    if (!c.searchQuery.empty() && !searchMode) {
        status += " | q=\"" + c.searchQuery + "\"";
    }

    // Title stats: fraction of Normal+Exceptional+Elite Uniques
    // discovered (matches what the grid actually covers -- excludes
    // Misc-tier uniques).
    int categoryTotal = 0;
    int categoryDiscovered = 0;
    for (const auto& r : s.chronicle) {
        if (r.kind != ChronicleKind::Unique) continue;
        if (tierSlot(r.tier) < 0) continue;
        ++categoryTotal;
        if (r.discovered) ++categoryDiscovered;
    }
    const int categoryRemaining = categoryTotal - categoryDiscovered;
    const bool remainingLabel   = c.ownership == OwnershipFilter::RemainingOnly;
    const int numerator = remainingLabel ? categoryRemaining : categoryDiscovered;
    // Progress percentage is always "discovered / total" -- the label
    // (remaining vs discovered) only picks the numerator; the parenthetical
    // percentage stays anchored to progress so a "5/135 remaining" pane
    // reads as (96%) rather than (4%).
    const int pctDiscovered = categoryTotal > 0
        ? static_cast<int>(std::round(100.0 * categoryDiscovered / categoryTotal))
        : 0;

    std::string title = " " + paneTitle(c) + "  "
                      + std::to_string(numerator) + "/"
                      + std::to_string(categoryTotal) + " "
                      + (remainingLabel ? "remaining" : "discovered")
                      + " (" + std::to_string(pctDiscovered) + "%) ";
    Element titleEl = text(title);
    if (focused) titleEl = titleEl | inverted;

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

// Render a leaf as a Chronicle pane. `focused` controls the highlighted
// row + the inverted title.
Element renderChronicleLeaf(const PaneConfig& c, const DashboardSnapshot& s,
                             bool focused, bool searchMode) {
    // Uniques (all) + Tier sort => dispatch to the 3-column grid.
    if (c.category == ChronicleCategory::UniquesAll &&
        c.sortKey  == PaneSortKey::Tier) {
        return renderChronicleByTier(c, s, focused, searchMode);
    }
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
        // Type-colored row: canonical D2R hue per kind (gold uniques,
        // green sets, orange runewords), muted variant for discovered
        // rows so ownership is visible without collapsing every quality
        // to a single grey.
        Element row = hbox(std::move(cells))
                    | color(item_colors::forKind(kt.kind, r->discovered));
        if (focused && i == clamped) row = row | inverted | ftxui::focus;
        body.push_back(row);
    }

    // Status line above the frame.
    std::string status = "sort: " + sortKeyLabel(c.sortKey)
                       + (c.sortAsc ? " asc" : " desc")
                       + " | show: " + ownershipLabel(c.ownership);
    if (!c.searchQuery.empty() && !searchMode) {
        status += " | q=\"" + c.searchQuery + "\"";
    }

    // Title stats. Anchor the numerator/denominator to the whole category
    // (kind + tier), ignoring the ownership and search filters -- otherwise
    // "remaining only" gives a "0/shown" ratio that says nothing about
    // progress. Label matches the filter: `remaining` when hiding
    // discovered rows, `discovered` otherwise.
    int categoryTotal = 0;
    int categoryDiscovered = 0;
    for (const auto& r : s.chronicle) {
        if (r.kind != kt.kind)                                        continue;
        if (kt.tierMask != 0 && (kt.tierMask & tierBit(r.tier)) == 0) continue;
        ++categoryTotal;
        if (r.discovered) ++categoryDiscovered;
    }
    const int categoryRemaining = categoryTotal - categoryDiscovered;
    const bool remainingLabel   = c.ownership == OwnershipFilter::RemainingOnly;
    const int numerator = remainingLabel ? categoryRemaining : categoryDiscovered;
    // Progress percentage is always "discovered / total"; a
    // "5/135 remaining" pane still reads as (96%).
    const int pctDiscovered = categoryTotal > 0
        ? static_cast<int>(std::round(100.0 * categoryDiscovered / categoryTotal))
        : 0;

    std::string title = " " + paneTitle(c) + "  "
                      + std::to_string(numerator) + "/"
                      + std::to_string(categoryTotal) + " "
                      + (remainingLabel ? "remaining" : "discovered")
                      + " (" + std::to_string(pctDiscovered) + "%) ";
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

// Render a leaf as an Inventory pane. Groups items into a two-level
// tree (Shared Stash / character-file top-level nodes; Tab N or
// physical-location sub-nodes). Cursor navigates over visible rows
// (top-level headers + section headers + item rows); expand state
// lives in `expandedPaths` and is preserved across search mode
// (search just force-expands display without mutating the set).
Element renderInventoryLeaf(const PaneConfig& c, const DashboardSnapshot& s,
                             const std::unordered_set<std::string>& expandedPaths,
                             bool focused, bool searchMode) {
    const auto filtered = filterInventory(c, s);
    const InvTree tree  = buildInventoryTree(filtered);
    const bool searchActive = !c.searchQuery.empty();
    const auto rows = emitInventoryRows(tree, expandedPaths, searchActive);

    // Widths (per-pane content). `wName` is the primary column across
    // every visible row (headers + items); `wSecond` covers whichever
    // secondary text an item exposes -- baseName for standard
    // sections, count-string for Gems/Runes/Materials.
    int wName = 12, wSecond = 4;
    auto bump = [](int& cur, std::size_t n, int cap) {
        cur = std::min(cap, std::max(cur, static_cast<int>(n)));
    };
    for (const auto& r : rows) {
        if (r.kind != InvVisibleRow::Kind::Item) continue;
        bump(wName, r.item->name.size(), 40);
        if (r.isCountSection) {
            const std::uint32_t q = r.item->stacks > 0 ? r.item->stacks : 1u;
            bump(wSecond, std::to_string(q).size(), 8);
        } else {
            bump(wSecond, r.item->baseName.size(), 28);
        }
    }

    const int shown = static_cast<int>(rows.size());
    const int clamped = std::clamp(c.cursor, 0, std::max(0, shown - 1));

    // Format one visible row. Depth-based indent + expand-marker
    // (▶ collapsed / ▼ expanded) prefix each header; item rows use
    // four spaces of leading indent to line up beneath their
    // section's label text.
    auto renderRow = [&](const InvVisibleRow& r, bool isCursor) -> Element {
        Element el;
        switch (r.kind) {
            case InvVisibleRow::Kind::TopLevel: {
                const char* icon = r.expanded ? "\u25BC " : "\u25B6 ";
                el = hbox({
                    text(icon) | bold,
                    text(r.label) | bold,
                    text("  "),
                    text("(" + std::to_string(r.itemCount) + ")") | dim,
                });
                break;
            }
            case InvVisibleRow::Kind::Section: {
                const char* icon = r.expanded ? "\u25BC " : "\u25B6 ";
                std::string tail;
                if (r.isCountSection) {
                    tail = "  " + std::to_string(r.itemCount) + " codes, "
                         + std::to_string(r.stackSum) + " total";
                } else {
                    tail = "  " + std::to_string(r.itemCount) + " items";
                }
                el = hbox({
                    text("  "),
                    text(icon),
                    text(r.label),
                    text(tail) | dim,
                });
                break;
            }
            case InvVisibleRow::Kind::Item: {
                const auto* it = r.item;
                const auto rowColor = isRuneCode(it->code)
                    ? item_colors::runewordFull()
                    : item_colors::forQuality(it->quality);
                std::string secondText;
                if (r.isCountSection) {
                    const std::uint32_t q = it->stacks > 0 ? it->stacks : 1u;
                    secondText = std::to_string(q);
                } else {
                    secondText = it->baseName;
                }
                Element second = r.isCountSection
                    ? (text(secondText) | align_right
                                        | size(WIDTH, EQUAL, wSecond + kCellPad))
                    : cellText(secondText, wSecond);
                el = hbox({
                    text("      "),   // four-space indent + one cell pad
                    cellText(it->name, wName),
                    second,
                }) | color(rowColor);
                break;
            }
        }
        if (isCursor) el = el | inverted | ftxui::focus;
        return el;
    };

    Elements body;
    body.reserve(static_cast<std::size_t>(shown + 1));
    if (rows.empty()) {
        body.push_back(text("  (no items match)") | dim);
    } else {
        for (int i = 0; i < shown; ++i) {
            body.push_back(renderRow(rows[static_cast<std::size_t>(i)],
                                      focused && i == clamped));
        }
    }

    std::string title = " Inventory  " + std::to_string(filtered.size()) + " items ";
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
        status += "  |  [/] search  [+/-] expand-all/collapse-all";
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
        }) | color(item_colors::forKind(r->kind));
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

// ---- Backups pane ----------------------------------------------------------

namespace {

std::string_view backupStateShortLabel(BackupDb::State s) {
    // User-facing labels. `BackupDb::State::SaveAndExit` names the
    // in-game action; the community-aligned display term for the
    // resulting backup is "run end" (see docs/session-logic.md).
    switch (s) {
        case BackupDb::State::Deleted:     return "deleted";
        case BackupDb::State::SaveAndExit: return "run end";
        case BackupDb::State::Autosave:    return "auto";
        case BackupDb::State::Startup:     return "startup";
        case BackupDb::State::Other:       return "other";
    }
    return "?";
}

// Local-timezone-friendly formatter: "YYYY-MM-DD HH:MM:SS". We format in
// the machine's local zone because the user is comparing against wall-
// clock events they lived through, not against UTC events they never
// see.
// -- formatWallDateTime / formatWallDate are now defined in the top
// anonymous namespace above so top-row renderers (Character,
// SessionLoot) can format timestamps too. Kept as a single definition
// per TU; this stub block is intentionally empty.

bool isSharedStashName(std::string_view name) {
    if (name.size() < 4) return false;
    const auto suffix = name.substr(name.size() - 4);
    return (suffix == ".d2i" || suffix == ".D2I");
}

// User-facing label for a backup row:
//   Kai.d2s                          -> "Kai"
//   ModernSharedStashSoftCoreV2.d2i  -> "Modern Shared Stash"
//   SharedStashHardCoreV2.d2i        -> "Shared Stash (Hardcore)"
//   ...
// Softcore is the default in D2R, so we don't bother tagging it in the
// display -- only hardcore stashes carry the "(Hardcore)" qualifier.
std::string backupDisplayName(std::string_view filename) {
    // Character: strip the .d2s / .D2S suffix.
    auto endsIn = [&](std::string_view suf) {
        if (filename.size() < suf.size()) return false;
        const auto off = filename.size() - suf.size();
        for (std::size_t i = 0; i < suf.size(); ++i) {
            const char a = static_cast<char>(
                std::tolower(static_cast<unsigned char>(filename[off + i])));
            const char b = static_cast<char>(
                std::tolower(static_cast<unsigned char>(suf[i])));
            if (a != b) return false;
        }
        return true;
    };
    if (endsIn(".d2s")) return std::string(filename.substr(0, filename.size() - 4));
    // The four known shared-stash variants. Softcore is unmarked; the
    // "Modern" era (2.4+ / RotW, 6 tabs + chronicle) is distinct from
    // the legacy pre-2.4 layout (3 tabs, no chronicle).
    if (filename == "SharedStashSoftCoreV2.d2i")       return "Shared Stash";
    if (filename == "SharedStashHardCoreV2.d2i")       return "Shared Stash (Hardcore)";
    if (filename == "ModernSharedStashSoftCoreV2.d2i") return "Modern Shared Stash";
    if (filename == "ModernSharedStashHardCoreV2.d2i") return "Modern Shared Stash (Hardcore)";
    if (endsIn(".d2i")) return "Shared Stash";
    return std::string(filename);
}

} // namespace

Element renderBackupsSummary(const PaneConfig& config, BackupDb* db,
                             bool focused, int paneWidth,
                             int daysRetention, int sessionsRetention,
                             std::string_view characterFilter) {
    Element titleEl = text(" Backups ");
    if (focused) titleEl = titleEl | inverted;

    if (!db) {
        return window(titleEl, vbox({
            text("The backup DB is not available for this session.") | dim,
            text("(--print mode or DB init failed; see stderr on startup.)") | dim,
        }));
    }

    // Pull the whole summary set. It's one row per known filename and is
    // small (dozens of rows at most), so no pagination is warranted.
    std::vector<BackupDb::FileSummary> sums;
    try {
        sums = db->summariseFiles();
    } catch (const std::exception& ex) {
        return window(titleEl, vbox({
            text("Backup DB query failed:"),
            text(std::string("  ") + ex.what()) | dim,
        }));
    }

    // Partition: stash (.d2i) rows pinned at top, characters below.
    // When `characterFilter` is set (from the layout's first Character
    // pane), keep only the matching character; stash is always shown
    // since it's account-wide.
    std::vector<const BackupDb::FileSummary*> stash;
    std::vector<const BackupDb::FileSummary*> chars;
    stash.reserve(sums.size());
    chars.reserve(sums.size());
    for (const auto& fs : sums) {
        if (isSharedStashName(fs.filename)) {
            stash.push_back(&fs);
        } else if (characterFilter.empty() || fs.filename == characterFilter) {
            chars.push_back(&fs);
        }
    }

    // Adaptive date column: include the wall-clock time when the pane
    // is wide enough. Thresholds account for the window border + a
    // reasonable minimum for the name column.
    //   ~"YYYY-MM-DD HH:MM:SS" == 19 chars; leave at least 20 for names.
    //   ~"YYYY-MM-DD"          == 10 chars; leave at least 12 for names.
    const bool showTime = paneWidth >= (20 + 19 + 3);   // ~42 columns
    auto formatDate = [&](std::int64_t d) {
        return showTime ? formatWallDateTime(d) : formatWallDate(d);
    };

    auto rowFor = [&](const BackupDb::FileSummary& fs, bool selected) {
        auto row = hbox({
            text(backupDisplayName(fs.filename)) | flex,
            text(" "),
            text(formatDate(fs.lastDate)),
        });
        if (selected) row = row | inverted;
        return row;
    };

    // Selection cursor: PaneConfig::cursor indexes into (stash + chars).
    const int totalRows = static_cast<int>(stash.size() + chars.size());
    const int cursor = totalRows == 0 ? 0
        : std::clamp(config.cursor, 0, totalRows - 1);

    // Column header, matching the two-column layout.
    Element header = hbox({
        text("Name") | bold | flex,
        text(" "),
        text(showTime ? "Last Save" : "Last") | bold,
    });

    std::vector<Element> body;
    body.push_back(header);
    body.push_back(separator());
    int i = 0;

    // Shared stash rows (no explicit header -- the naming is
    // self-documenting).
    for (const auto* fs : stash) {
        body.push_back(rowFor(*fs, i == cursor));
        ++i;
    }
    // Blank line separator between stash and characters.
    if (!stash.empty() && !chars.empty()) {
        body.push_back(text(""));
    }
    if (chars.empty() && stash.empty()) {
        body.push_back(text("  (no backups yet)") | dim);
    }
    for (const auto* fs : chars) {
        body.push_back(rowFor(*fs, i == cursor));
        ++i;
    }

    // Footer: hint the user how to drill in / edit retention.
    body.push_back(filler());
    const std::string retLine =
        "retention: " + std::to_string(daysRetention) + " days OR last "
        + std::to_string(sessionsRetention) + " runs/char";
    body.push_back(hbox({
        text(retLine) | dim,
        filler(),
        text("[Enter] detail  [E] retention  ") | dim,
    }));

    return window(titleEl, vbox(std::move(body)) | yframe);
}

Element renderBackupsDetail(const PaneConfig& config, BackupDb* db,
                            const UiState& ui,
                            bool focused, int pageHeight) {
    Element titleEl = text(" Backups  " + config.selectedBackupFile + " ");
    if (focused) titleEl = titleEl | inverted;

    if (!db) {
        return window(titleEl, text("(backup DB unavailable)") | dim);
    }
    if (config.selectedBackupFile.empty()) {
        return window(titleEl, vbox({
            text("No file selected.") | dim,
            text("Press [Esc] to return to the summary view.") | dim,
        }));
    }

    std::vector<BackupDb::HistoryRow> hist;
    try {
        hist = db->historyFor(config.selectedBackupFile, 2048);
    } catch (const std::exception& ex) {
        return window(titleEl, vbox({
            text("History query failed:"),
            text(std::string("  ") + ex.what()) | dim,
        }));
    }
    if (hist.empty()) {
        return window(titleEl, vbox({
            text("No history for this file.") | dim,
            text("Press [Esc] to return to the summary view.") | dim,
        }));
    }

    // Reset the exp% cache when the detail view switches to a
    // different file. Only .d2s backups carry a character; other file
    // types show "-" in the exp column and are never parsed.
    if (ui.backupsExpCacheFilename != config.selectedBackupFile) {
        ui.backupsExpCacheFilename = config.selectedBackupFile;
        ui.backupsExpCache.clear();
    }
    const bool isCharacterFile =
        config.selectedBackupFile.size() >= 4 &&
        (config.selectedBackupFile.compare(
             config.selectedBackupFile.size() - 4, 4, ".d2s") == 0 ||
         config.selectedBackupFile.compare(
             config.selectedBackupFile.size() - 4, 4, ".D2S") == 0);

    auto expPercentFor = [&](std::int64_t rowDate, std::int64_t sizeBytes,
                              BackupDb::State st)
        -> std::optional<double> {
        if (!isCharacterFile) return std::nullopt;
        if (st == BackupDb::State::Deleted) return std::nullopt;
        if (sizeBytes <= 0) return std::nullopt;
        auto it = ui.backupsExpCache.find(rowDate);
        if (it != ui.backupsExpCache.end()) return it->second;

        std::optional<double> pct;
        try {
            auto row = db->at(config.selectedBackupFile, rowDate);
            if (row && !row->data.empty()) {
                const auto ch = parseCharacter(row->data);
                const std::uint32_t lvl = ch.attributes.level
                    ? ch.attributes.level
                    : static_cast<std::uint32_t>(ch.level);
                const std::uint64_t curFloor  = experienceToReachLevel(lvl);
                const std::uint64_t nextFloor = experienceToReachLevel(lvl + 1);
                const std::uint64_t exp       = ch.attributes.experience;
                if (nextFloor > curFloor && exp >= curFloor) {
                    const std::uint64_t inLvl = exp - curFloor;
                    const std::uint64_t span  = nextFloor - curFloor;
                    double p = 100.0 * static_cast<double>(inLvl)
                                       / static_cast<double>(span);
                    if (p < 0.0)   p = 0.0;
                    if (p > 100.0) p = 100.0;
                    pct = p;
                }
            }
        } catch (const std::exception&) {
            // Parse failure -- cache the negative result so we don't
            // retry every render.
        }
        ui.backupsExpCache.emplace(rowDate, pct);
        return pct;
    };

    auto formatExp = [](const std::optional<double>& pct) -> std::string {
        if (!pct) return "-";
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%5.1f%%", *pct);
        return buf;
    };

    auto formatChecksum = [&](const std::optional<std::uint32_t>& c) -> std::string {
        if (!c) return "-";
        char b[16];
        std::snprintf(b, sizeof(b), "%08x", *c);
        return b;
    };

    auto formatDuration = [](std::int64_t secs) -> std::string {
        if (secs < 0) secs = 0;
        char b[32];
        std::snprintf(b, sizeof(b), "%lldh %02lldm %02llds",
                      static_cast<long long>(secs / 3600),
                      static_cast<long long>((secs % 3600) / 60),
                      static_cast<long long>(secs % 60));
        return b;
    };

    // ---- Build visible-row list -------------------------------------
    //
    // Collapsed mode: group `hist` into Runs (via groupRunsForFile),
    // reverse to newest-first, and emit one Run summary row per Run;
    // if a Run is currently expanded (ui.expandedRunEndEpoch matches
    // its endEpoch) the run's autosaves are unfolded beneath it,
    // newest-first. Startup / Deleted / non-run-signal rows are
    // omitted in this mode -- the user can toggle collapse off to see
    // the full history.
    //
    // Raw mode: one row per hist entry, matching the pre-run-collapse
    // behavior. A dim divider is drawn above each SaveAndExit row so
    // runs are still visually demarcated.
    struct VRow {
        std::int64_t                  date        = 0;   // for cursor / exp cache
        std::string                   whenStr;
        std::string                   stateLabel;
        std::optional<double>         exp;
        std::optional<std::uint32_t>  checksum;
        std::string                   extra;             // e.g. "▶  5 autosaves"
        bool                          isDivider   = false;
        bool                          isRunSummary= false;
        std::int64_t                  runEndEpoch = 0;   // for expand toggling
        bool                          dimAutosave = false;
    };
    std::vector<VRow> vrows;
    if (config.backupsRunCollapse) {
        auto runs = d2r::groupRunsForFile(config.selectedBackupFile, hist, 0);
        // Runs come out chronological oldest-first; the detail view is
        // newest-first.
        std::reverse(runs.begin(), runs.end());
        // Build a lookup of hist row -> checksum/sizeBytes so we can
        // display fields on unfolded autosave rows too.
        std::unordered_map<std::int64_t, const BackupDb::HistoryRow*> byDate;
        for (const auto& r : hist) byDate[r.date] = &r;
        for (const auto& r : runs) {
            VRow v;
            v.isRunSummary  = true;
            v.runEndEpoch   = r.endEpoch;
            v.date          = r.inProgress
                                 ? (r.autosaveDates.empty() ? 0 : r.autosaveDates.back())
                                 : r.endEpoch;
            const bool expanded = ui.expandedRunEndEpoch
                                    && *ui.expandedRunEndEpoch == r.endEpoch
                                    && !r.inProgress;
            if (r.inProgress) {
                v.whenStr    = "(in progress)";
                v.stateLabel = "-";
                v.extra      = "~" + std::to_string(r.autosaveCount)
                                 + " autosaves";
            } else {
                v.whenStr    = formatWallDateTime(r.endEpoch);
                v.stateLabel = std::string(expanded ? "v " : "> ") + "run end";
                const std::int64_t startForDur =
                    r.autosaveDates.empty() ? r.endEpoch
                                            : r.autosaveDates.front();
                v.extra = formatDuration(r.endEpoch - startForDur)
                            + "  " + std::to_string(r.autosaveCount)
                            + " autosaves";
                if (auto it = byDate.find(r.endEpoch); it != byDate.end()) {
                    v.exp      = expPercentFor(r.endEpoch,
                                                it->second->sizeBytes,
                                                it->second->state);
                    v.checksum = it->second->checksum;
                }
            }
            vrows.push_back(std::move(v));
            if (expanded) {
                // Autosaves oldest-first inside the Run; unfold newest-
                // first below the summary.
                for (auto it = r.autosaveDates.rbegin();
                     it != r.autosaveDates.rend(); ++it) {
                    VRow a;
                    a.date        = *it;
                    a.dimAutosave = true;
                    a.stateLabel  = "  auto";
                    a.whenStr     = formatWallDateTime(*it);
                    if (auto hi = byDate.find(*it); hi != byDate.end()) {
                        a.exp      = expPercentFor(*it,
                                                    hi->second->sizeBytes,
                                                    hi->second->state);
                        a.checksum = hi->second->checksum;
                    }
                    vrows.push_back(std::move(a));
                }
            }
        }
    } else {
        // Raw mode: mirror the legacy per-hist-row rendering.
        for (int i = 0; i < static_cast<int>(hist.size()); ++i) {
            const auto& r = hist[i];
            VRow v;
            v.date       = r.date;
            v.whenStr    = formatWallDateTime(r.date);
            v.stateLabel = std::string(backupStateShortLabel(r.state));
            v.exp        = expPercentFor(r.date, r.sizeBytes, r.state);
            v.checksum   = r.checksum;
            v.isDivider  = (r.state == BackupDb::State::SaveAndExit && i > 0);
            vrows.push_back(std::move(v));
        }
    }

    const int nRows = static_cast<int>(vrows.size());
    const int cursor = nRows > 0
        ? std::clamp(config.cursor, 0, nRows - 1)
        : 0;

    constexpr int kDateW  = 20;
    constexpr int kStateW = 14;
    constexpr int kExpW   = 8;
    constexpr int kSumW   = 12;
    auto pad = [](std::string s, int w) {
        if (static_cast<int>(s.size()) > w) return s.substr(0, w);
        s.append(w - s.size(), ' ');
        return s;
    };

    Element header = hbox({
        text(pad("when",     kDateW))  | bold,
        text(pad("state",    kStateW)) | bold,
        text(pad("exp %",    kExpW))   | bold,
        text(pad("checksum", kSumW))   | bold,
        text(" ")                       | bold,
    });

    std::vector<Element> body;
    body.push_back(header);
    body.push_back(separator());

    // Window rows around the cursor to fit the pane height. Reserve
    // rows for the header + separator + footer.
    const int reserved = 4;
    const int visible = std::max(1, pageHeight - reserved);
    int start = std::max(0, cursor - visible / 2);
    if (start + visible > nRows) start = std::max(0, nRows - visible);
    const int end = std::min(nRows, start + visible);

    for (int i = start; i < end; ++i) {
        const auto& v = vrows[i];
        if (v.isDivider && i > start) {
            body.push_back(separator() | dim);
        }
        Element rowEl = hbox({
            text(pad(v.whenStr,     kDateW)),
            text(pad(v.stateLabel,  kStateW)),
            text(pad(formatExp(v.exp), kExpW)),
            text(pad(formatChecksum(v.checksum), kSumW)),
            text(v.extra),
        });
        if (v.dimAutosave)   rowEl = rowEl | dim;
        if (i == cursor)     rowEl = rowEl | inverted;
        body.push_back(rowEl);
    }

    // Footer.
    body.push_back(filler());
    Element footer = config.backupsRunCollapse
        ? text("[Enter] expand run  [Esc] back  [R] recover  ") | dim
        : text("[Esc] back  [R] recover  ") | dim;
    body.push_back(hbox({
        text("  row " + std::to_string(cursor + 1) + "/" + std::to_string(nRows)) | dim,
        filler(),
        footer,
    }));

    return window(titleEl, vbox(std::move(body)));
}

Element renderBackupsLeaf(const PaneConfig& config, BackupDb* db,
                          const UiState& ui,
                          bool focused, int paneWidth, int pageHeight,
                          int daysRetention, int sessionsRetention,
                          std::string_view characterFilter) {
    return config.backupViewMode == BackupViewMode::Detail
        ? renderBackupsDetail(config, db, ui, focused, pageHeight)
        : renderBackupsSummary(config, db, focused, paneWidth,
                                daysRetention, sessionsRetention,
                                characterFilter);
}

// Returns the ordered filename list for the summary view (shared stash
// pinned at the top, characters below), or empty on DB failure. The
// order matches the rendered rows, so cursor N indexes into result[N].
// `characterFilter` mirrors renderBackupsSummary's filtering so the
// Enter-to-detail navigation lines up with what the user sees.
std::vector<std::string> backupsSummaryOrder(BackupDb* db,
                                             std::string_view characterFilter) {
    std::vector<std::string> out;
    if (!db) return out;
    std::vector<BackupDb::FileSummary> sums;
    try { sums = db->summariseFiles(); }
    catch (const std::exception&) { return out; }
    for (const auto& fs : sums) {
        if (isSharedStashName(fs.filename)) out.push_back(fs.filename);
    }
    for (const auto& fs : sums) {
        if (isSharedStashName(fs.filename)) continue;
        if (!characterFilter.empty() && fs.filename != characterFilter) continue;
        out.push_back(fs.filename);
    }
    return out;
}

// Determine which character (if any) the Backups pane should filter to.
// Walks the pane tree for the FIRST `PaneType::Character` leaf and
// resolves that pane's selection: an explicit basename (with .d2s
// appended if the user stored the bare stem), or the auto snapshot's
// active-player file when the selection is empty. Returns "" if no
// Character pane exists (backward-compatible: shows every backup).
std::string resolveBackupsCharacterFilter(const PaneNode& root,
                                          const DashboardSnapshot& s) {
    for (const auto* leaf : flattenLeaves(root)) {
        if (leaf->config.type != PaneType::Character) continue;
        const auto& sel = leaf->config.characterSelection;
        if (!sel.empty()) {
            const bool hasExt = sel.size() >= 4
                && (sel.compare(sel.size() - 4, 4, ".d2s") == 0
                 || sel.compare(sel.size() - 4, 4, ".D2S") == 0);
            return hasExt ? sel : (sel + ".d2s");
        }
        return s.hasActivePlayer ? s.activePlayer.file : std::string{};
    }
    return {};
}

// ---- BackupLog pane (ephemeral this-process ring buffer) -------------------

// Cap on the number of remembered backup events. Older entries are
// evicted from the front of the deque when a new event arrives. 200 is
// large enough to cover an evening of play (D2R writes on autosave +
// save-and-exit + occasional stash flushes) while keeping memory tiny.
inline constexpr std::size_t kBackupLogCap = 200;

Element renderBackupLogLeaf(const UiState& ui, bool focused) {
    Element titleEl = text(" Backup Actions ");
    if (focused) titleEl = titleEl | inverted;

    // Snapshot under the lock so the render doesn't race the watcher
    // thread's push_back. Copies are cheap (short strings).
    std::vector<UiState::BackupLogEntry> snap;
    {
        std::lock_guard<std::mutex> g(ui.backupLogMutex);
        snap.assign(ui.backupLog.begin(), ui.backupLog.end());
    }
    if (snap.empty()) {
        return window(titleEl, vbox({
            filler(),
            hbox({ filler(),
                   vbox({
                       text("No backup events yet this session.") | dim,
                       text("Save your character in D2R to see entries here.") | dim,
                   }),
                   filler() }),
            filler(),
        }));
    }
    // Newest first so the freshest event is always at the top of the
    // window without the user needing to scroll.
    std::reverse(snap.begin(), snap.end());
    Elements rows;
    rows.reserve(snap.size() + 2);
    rows.push_back(hbox({
        text("Time") | bold | size(WIDTH, EQUAL, 20),
        text("Name") | bold,
    }));
    rows.push_back(separator());
    for (const auto& e : snap) {
        rows.push_back(hbox({
            text(formatWallDateTime(e.when)) | size(WIDTH, EQUAL, 20),
            text(backupDisplayName(e.filename)) | flex,
        }));
    }
    Element footer = text(" " + std::to_string(snap.size()) + " event(s)  "
                          "[c] configure / clear ") | dim;
    return window(titleEl, vbox({
        vbox(std::move(rows)) | vscroll_indicator | frame | flex,
        separator(),
        std::move(footer),
    }));
}

// ---- Session pane (diff vs session start) ----------------------------------

namespace {

} // namespace

Element renderSessionLeaf(const DashboardSnapshot& now,
                          const Session*           session,
                          bool                     focused,
                          bool                     startPinned) {
    // Title reflects whether the start is user-fixed or tracking the
    // current play session automatically (auto = D2R launch heuristic
    // on the backup DB).
    Element titleEl = text(
        startPinned ? " Session (custom start) " : " Session ");
    if (focused) titleEl = titleEl | inverted;

    if (!session) {
        return window(titleEl, vbox({
            filler(),
            hbox({ filler(),
                   text("Session not yet initialised.") | dim,
                   filler() }),
            filler(),
        }));
    }

    const SessionState& startState = session->startState;
    Elements body;
    // Header line: character + session duration. Duration is the span
    // between session start and end (see buildSession for how those
    // are chosen). Auto-end sessions ride "now"; custom-end sessions
    // clamp to the user-fixed instant.
    Elements header;
    if (now.hasActivePlayer) {
        header.push_back(text(now.activePlayer.name) | bold);
        header.push_back(text("  "));
        header.push_back(text("(" + classString(now.activePlayer.characterClass) + ")") | dim);
    } else {
        header.push_back(text("(no active player)") | dim);
    }
    {
        std::int64_t secs = 0;
        if (session->startEpoch != 0 && session->endEpoch != 0) {
            secs = session->endEpoch - session->startEpoch;
            if (secs < 0) secs = 0;
        }
        const std::int64_t h =  secs / 3600;
        const std::int64_t m = (secs % 3600) / 60;
        const std::int64_t s =  secs % 60;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lldh %02lldm %02llds",
                      static_cast<long long>(h),
                      static_cast<long long>(m),
                      static_cast<long long>(s));
        header.push_back(text("   "));
        header.push_back(text(std::string(buf)) | dim);
    }
    body.push_back(hbox(std::move(header)));

    // Warn if the active-player identity changed since the session start
    // -- the XP delta only makes sense within a single character, so we
    // still show the numbers but flag the mismatch so the user knows
    // to reset the session from the pane config.
    if (startState.hasActivePlayer && now.hasActivePlayer &&
        startState.playerName != now.activePlayer.name) {
        body.push_back(text(
            "  * session start was " + startState.playerName +
            "; reset session for this character") | color(Color::Yellow));
    }
    body.push_back(separator());

    // XP delta. Compute in cumulative-XP space so a level-up isn't
    // interpreted as XP loss (expInLevel resets to 0 at each ding).
    std::int64_t xpDelta = 0;
    std::int32_t levelDelta = 0;
    double       pctDelta = 0.0;
    if (startState.hasActivePlayer && now.hasActivePlayer) {
        const auto& n = now.activePlayer;
        const std::uint64_t aCum = cumulativeXp(startState.level, startState.expInLevel);
        const std::uint64_t nCum = cumulativeXp(n.level,          n.expInLevel);
        xpDelta = static_cast<std::int64_t>(nCum)
                - static_cast<std::int64_t>(aCum);
        levelDelta = static_cast<std::int32_t>(n.level)
                   - static_cast<std::int32_t>(startState.level);
        // Percentage is expressed as "fraction of the XP bar filled
        // during the session", matching the in-game XP bar. A level-up
        // contributes 100% (the start's remaining bar + all subsequent
        // full bars + progress into the current bar). Late-game sessions
        // therefore read as "3.0%" rather than a cumulative-XP fraction
        // that trends toward zero as total XP grows.
        const std::uint64_t anchorFloor = experienceToReachLevel(startState.level);
        const std::uint64_t anchorCeil  = experienceToReachLevel(startState.level + 1);
        const std::uint64_t anchorSpan  = anchorCeil > anchorFloor
                                            ? anchorCeil - anchorFloor : 0;
        const double anchorFrac = (anchorSpan > 0)
            ? static_cast<double>(startState.expInLevel)
              / static_cast<double>(anchorSpan)
            : 0.0;
        const double nowFrac = (n.expForLevel > 0)
            ? static_cast<double>(n.expInLevel)
              / static_cast<double>(n.expForLevel)
            : 0.0;
        pctDelta = 100.0 * (static_cast<double>(levelDelta)
                            + nowFrac - anchorFrac);
    }

    char pctBuf[32];
    std::snprintf(pctBuf, sizeof(pctBuf), "%+.2f%%", pctDelta);
    body.push_back(hbox({
        text("Exp  ") | bold,
        text(std::string(pctBuf)),
        text("   "),
        text(formatSignedDelta(xpDelta)),
    }));
    body.push_back(hbox({
        text("Levels gained  ") | bold,
        text(formatSignedDelta(levelDelta)),
    }));
    body.push_back(separator());

    // New-item lists (uniques / sets / runes since the session start)
    // moved to PaneType::SessionLoot as part of the configurable-top-
    // row work. This bottom-row pane now focuses purely on XP + duration;
    // add a Session Info pane to see the loot rundown.
    body.push_back(text("Add a 'Session Info' pane to see new items,")
                   | dim);
    body.push_back(text("uniques, and rune drops for this session.")
                   | dim);

    Element footer = text(" reset session from [c] configure ") | dim;
    return window(titleEl, vbox({
        vbox(std::move(body)) | vscroll_indicator | frame | flex,
        separator(),
        std::move(footer),
    }));
}

// ---- Recovery modal --------------------------------------------------------

Element renderRecoveryModal(const UiState::RecoveryModal& m,
                            const std::string& primaryDir) {
    const std::string destLine =
        m.destChoice == 0
            ? std::string("[X] Restore in place to  ") + primaryDir
            : std::string("[ ] Restore in place to  ") + primaryDir;
    const std::string altLine =
        m.destChoice == 1
            ? std::string("[X] Restore to a folder  ") + m.altPathInput
            : std::string("[ ] Restore to a folder  ") + m.altPathInput;

    std::vector<Element> body = {
        text("Restore  " + m.filename) | bold,
        text("As of    " + formatWallDateTime(m.atUnix)) | dim,
        text(""),
        text("Destination:") | bold,
        text("  " + destLine),
        text("  " + altLine),
        text(""),
        text("A pre-recovery snapshot of the destination will be taken.") | dim,
    };
    if (!m.status.empty()) {
        body.push_back(text(""));
        body.push_back(text(m.status) | bold);
    }
    body.push_back(text(""));
    body.push_back(hbox({
        text("[Tab] switch destination  ") | dim,
        text("[type] edit alt path  ") | dim,
        text("[Enter] confirm  ") | dim,
        text("[Esc] cancel") | dim,
    }));
    return window(text(" Backup Recovery "), vbox(std::move(body)) | size(WIDTH, GREATER_THAN, 60));
}

Element renderRetentionModal(const UiState::RetentionModal& m,
                             BackupDb* db) {
    auto fieldBox = [&](std::string_view label, const std::string& buf, bool focused) {
        std::string content = "[ " + buf + " ]";
        auto e = hbox({ text(std::string(label) + "  ") | bold, text(content) });
        return focused ? (e | inverted) : e;
    };

    std::int64_t rowCount = 0;
    std::int64_t fileCount = 0;
    std::int64_t oldestDate = 0;
    if (db) {
        try {
            const auto sums = db->summariseFiles();
            fileCount = static_cast<std::int64_t>(sums.size());
            for (const auto& fs : sums) {
                rowCount += fs.backupCount;
                if (oldestDate == 0 || fs.lastDate < oldestDate) oldestDate = fs.lastDate;
            }
        } catch (const std::exception&) { /* stats optional */ }
    }
    // NB: `lastDate` is the newest date per file, not the oldest; we use
    // it as a rough "how deep does the DB reach" indicator without
    // adding another accessor for now. Precise oldest-row lookup can be
    // a follow-up when the retention editor grows a dry-run preview.

    std::vector<Element> body = {
        text("Backup retention policy") | bold,
        text("Keep everything within N days OR the last M runs per character.") | dim,
        text(""),
        fieldBox("days: ", m.daysBuf,     m.focused == 0),
        fieldBox("runs: ", m.sessionsBuf, m.focused == 1),
        text(""),
        text("Current DB: " + std::to_string(rowCount) + " row(s) across "
             + std::to_string(fileCount) + " file(s)") | dim,
    };
    if (!m.status.empty()) {
        body.push_back(text(""));
        body.push_back(text(m.status) | bold);
    }
    body.push_back(text(""));
    body.push_back(hbox({
        text("[Tab] switch field  ") | dim,
        text("[type / Backspace] edit  ") | dim,
        text("[Enter] save  ") | dim,
        text("[Esc] cancel") | dim,
    }));
    return window(text(" Retention "), vbox(std::move(body)) | size(WIDTH, GREATER_THAN, 60));
}

// Rendered on top of the pane layout when `ui.characterPickerVisible`.
// Vertical list with the current cursor row inverted and a fixed
// hint footer. The first entry is always "(auto -- newest saved)".
Element renderCharacterPickerModal(const UiState::CharacterPicker& p) {
    Element titleEl = text(" Pick character ");
    if (p.options.empty()) {
        return window(titleEl, vbox({
            text("No characters cached yet.") | dim,
            text("Press [Esc] to close.") | dim,
        }) | size(WIDTH, GREATER_THAN, 40));
    }
    const int n      = static_cast<int>(p.options.size());
    const int cursor = std::clamp(p.cursor, 0, n - 1);
    constexpr int kVisible = 20;
    int start = std::max(0, cursor - kVisible / 2);
    if (start + kVisible > n) start = std::max(0, n - kVisible);
    const int end = std::min(n, start + kVisible);

    Elements body;
    for (int i = start; i < end; ++i) {
        const auto& opt = p.options[i];
        const std::string label = opt.empty()
            ? std::string(" (auto -- newest saved) ")
            : std::string(" ") + opt + " ";
        Element row = text(label);
        if (i == cursor) row = row | inverted;
        else if (opt.empty()) row = row | dim;
        body.push_back(row);
    }
    body.push_back(separator());
    body.push_back(hbox({
        text(" " + std::to_string(cursor + 1) + "/" + std::to_string(n)) | dim,
        filler(),
        text("[Up/Down] navigate  [Enter] select  [Esc] cancel ") | dim,
    }));
    return window(titleEl, vbox(std::move(body)) | size(WIDTH, GREATER_THAN, 40));
}

// Free-form session-time input modal. Text field + accepted-format
// hint + optional error line. Used by the "start: manual..." and
// "end: manual..." menu actions so the user can pin any moment,
// not just backup-anchored ones.
Element renderSessionTimeInputModal(const UiState::SessionTimeInputModal& m) {
    Element titleEl = text(
        std::string(m.isEnd ? " Set session end (manual) "
                            : " Set session start (manual) "));
    // Text field: rendered as "[ buffer_ ]" with an inverse cursor
    // caret so the empty case still shows an obvious input target.
    Element inputEl = hbox({
        text("[ "),
        text(m.buffer),
        text("_") | inverted,
        text(" ]"),
    });
    Elements body = {
        hbox({ text("Time  ") | bold, std::move(inputEl) }),
        text(""),
        text("Accepted formats:") | bold,
        text("  now                (or empty)") | dim,
        text("  -5m, -1h30m, -45s  (offset from now)") | dim,
        text("  2026-07-29 15:30:00") | dim,
        text("  2026-07-29 15:30") | dim,
        text("  2026-07-29         (midnight)") | dim,
        text("  15:30, 15:30:00    (today)") | dim,
    };
    if (!m.status.empty()) {
        body.push_back(text(""));
        body.push_back(text(m.status) | bold | color(Color::Yellow));
    }
    body.push_back(text(""));
    body.push_back(text("[Enter] apply  [Esc] cancel") | dim);
    return window(titleEl, vbox(std::move(body)) | size(WIDTH, GREATER_THAN, 50));
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
        ClearBackupLog,
        ResetSession,
        SetSessionStart,
        SetSessionEnd,
        PickCharacter,
        ResetCharacterToAuto,
        ToggleUberShowUbers,
        ToggleUberShowTorchByClass,
        ToggleSessionLootShowRunes,
        ToggleBackupsRunCollapse,
        CyclePaneWeight,
        CycleInfoLevel,
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
//
// `appSession` is the current AppSession snapshot (start/end epochs +
// custom-flags); Session/SessionLoot/Character panes read it to label
// their start/end menu rows. Passed by value to avoid holding a lock
// across menu-build allocations.
std::vector<ConfigMenuItem> buildConfigMenu(const PaneConfig& c, bool canDelete,
                                             const AppSession& appSession) {
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
        // Info level only affects the Uniques (all) "By Tier" grid;
        // hide it elsewhere so the menu stays tight.
        if (c.category == ChronicleCategory::UniquesAll &&
            c.sortKey  == PaneSortKey::Tier) {
            items.push_back({"info level:  " + std::to_string(std::clamp(c.infoLevel, 1, 3))
                             + "  (cycle 1..3)",
                             ConfigMenuItem::CycleInfoLevel});
        }
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
    if (c.type == PaneType::BackupLog) {
        items.push_back({"clear backup-actions log", ConfigMenuItem::ClearBackupLog});
    }
    if (c.type == PaneType::Backups &&
        c.backupViewMode == BackupViewMode::Detail) {
        items.push_back({
            std::string("collapse into runs:   ")
              + (c.backupsRunCollapse ? "on" : "off"),
            ConfigMenuItem::ToggleBackupsRunCollapse});
    }
    // Session-window controls live on every pane that consumes the
    // session (Session, SessionLoot, Character). They edit the shared
    // AppSession singleton, not per-pane config. When start is auto
    // the label shows which auto source is currently effective
    // ("now" until a launch fires the callback; "D2R launch" after).
    if (c.type == PaneType::Session
     || c.type == PaneType::SessionLoot) {
        const bool startCustom = appSession.startIsCustom();
        if (startCustom) {
            items.push_back({
                "start: custom @ " + formatWallDateTime(appSession.startEpoch())
                    + "  --  [type any time...]",
                ConfigMenuItem::SetSessionStart});
        } else {
            items.push_back({
                "start: auto @ " + formatWallDateTime(appSession.startEpoch())
                    + "  --  [type any time...]",
                ConfigMenuItem::SetSessionStart});
        }
        // End control only surfaces when start is custom (invariant:
        // custom end requires custom start).
        if (startCustom) {
            if (appSession.endIsCustom()) {
                items.push_back({
                    "end:   custom @ " + formatWallDateTime(appSession.endEpoch())
                        + "  --  [type any time...]",
                    ConfigMenuItem::SetSessionEnd});
            } else {
                items.push_back({
                    "end:   auto (last save)  --  [type any time...]",
                    ConfigMenuItem::SetSessionEnd});
            }
        }
        items.push_back({"reset session (clears custom start + end)",
                          ConfigMenuItem::ResetSession});
    }
    if (c.type == PaneType::Character) {
        // Character selection picker + auto-reset. Mirrors the session-
        // anchor menu structure: one pick-a-target row that opens the
        // modal, plus a shortcut to snap back to auto tracking.
        const std::string current = c.characterSelection.empty()
            ? std::string("auto (newest saved)")
            : c.characterSelection;
        items.push_back({"character:   " + current + "  --  [pick...]",
                          ConfigMenuItem::PickCharacter});
        if (!c.characterSelection.empty()) {
            items.push_back({"reset character to auto (newest saved)",
                              ConfigMenuItem::ResetCharacterToAuto});
        }
    }
    if (c.type == PaneType::Uber) {
        items.push_back({
            std::string("show uber drops:      ")
              + (c.uberShowUbers ? "on" : "off"),
            ConfigMenuItem::ToggleUberShowUbers});
        items.push_back({
            std::string("show torch by class:  ")
              + (c.uberShowTorchByClass ? "on" : "off"),
            ConfigMenuItem::ToggleUberShowTorchByClass});
    }
    if (c.type == PaneType::SessionLoot) {
        items.push_back({
            std::string("show new runes:       ")
              + (c.sessionLootShowRunes ? "on" : "off"),
            ConfigMenuItem::ToggleSessionLootShowRunes});
    }
    // Size weight is universal -- affects width in a vertical (side-
    // by-side) parent and height in a horizontal (stacked) parent.
    // Cycled 1 -> 2 -> 3 -> 4 -> 1 so the user can push a pane out to
    // twice / triple / quadruple the neighbors' allotment.
    items.push_back({"size weight: " + std::to_string(std::max(1, c.paneWeight))
                     + "  (cycle 1..4)",
                     ConfigMenuItem::CyclePaneWeight});
    items.push_back({"split vertical   (side-by-side)", ConfigMenuItem::SplitVertical});
    items.push_back({"split horizontal (stacked)",       ConfigMenuItem::SplitHorizontal});
    if (canDelete) {
        items.push_back({"delete pane (sibling absorbs slot)", ConfigMenuItem::DeletePane});
    }
    items.push_back({"close config menu (Esc)", ConfigMenuItem::Close});
    return items;
}

Element renderConfigMenu(const UiState& ui, const PaneConfig& c, bool canDelete) {
    // Snapshot the AppSession under the mutex before building menu
    // labels (the launch-burst callback can mutate it from the watcher
    // thread; snapshotMutex is `mutable`).
    AppSession appSessionCopy;
    {
        std::lock_guard g(ui.snapshotMutex);
        appSessionCopy = ui.appSession;
    }
    const auto items = buildConfigMenu(c, canDelete, appSessionCopy);
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
// A leaf contributes `paneWeight` (>=1; the user-adjustable size
// weight from PaneConfig). A vertical split (side-by-side) sums its
// children; a horizontal split (stacked) takes the max because both
// children share the same horizontal band.
int widthShare(const PaneNode& n) {
    if (!n.isSplit) return std::max(1, n.config.paneWeight);
    if (n.direction == SplitDirection::Vertical)
        return widthShare(*n.a) + widthShare(*n.b);
    return std::max(widthShare(*n.a), widthShare(*n.b));
}

// Vertical counterpart to `widthShare`. A leaf contributes
// `paneWeight`; a horizontal split (stacked) sums; a vertical split
// (side-by-side) takes the max because both children share the same
// vertical band.
int heightShare(const PaneNode& n) {
    if (!n.isSplit) return std::max(1, n.config.paneWeight);
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
    // Per-leaf scroll fraction lookup for panes that use
    // `focusPositionRelative` (Session Info, Runes). Absent entry
    // defaults to 0.f (viewport pinned at the top).
    auto paneScrollFrac = [&](const PaneNode* n) -> float {
        const auto it = ui.paneScrollFrac.find(n);
        return it == ui.paneScrollFrac.end() ? 0.f : it->second;
    };
    Element leafEl;
    switch (node.config.type) {
        case PaneType::Chronicle:
            leafEl = renderChronicleLeaf(node.config, s, focused,
                                          focused && ui.searchMode);
            break;
        case PaneType::Inventory: {
            static const std::unordered_set<std::string> kEmpty;
            const auto it = ui.paneExpanded.find(&node);
            const auto& expanded = it == ui.paneExpanded.end() ? kEmpty : it->second;
            leafEl = renderInventoryLeaf(node.config, s, expanded, focused,
                                          focused && ui.searchMode);
            break;
        }
        case PaneType::Reconcile:
            leafEl = renderReconcileLeaf(node.config, s, focused,
                                          focused && ui.searchMode);
            break;
        case PaneType::Backups: {
            const int days = ui.backupScheduler ? ui.backupScheduler->retention().days : 30;
            const int sess = ui.backupScheduler ? ui.backupScheduler->retention().sessionsPerFile : 100;
            // The Backups pane follows whichever character the first
            // Character pane in the layout resolves to (auto or pinned).
            // No Character pane -> no filter -> shows everything.
            const std::string filter = resolveBackupsCharacterFilter(ui.rootPane, s);
            leafEl = renderBackupsLeaf(node.config, ui.backupDb, ui, focused,
                                        width, height, days, sess, filter);
            break;
        }
        case PaneType::BackupLog:
            leafEl = renderBackupLogLeaf(ui, focused);
            break;
        case PaneType::Session:
            leafEl = renderSessionLeaf(s, ui.session.get(), focused,
                                        ui.session && ui.session->startIsCustom);
            break;
        case PaneType::Blank:
            leafEl = renderBlankLeaf(focused);
            break;
        case PaneType::Character:
            leafEl = renderCharacterPane(node.config, s,
                                          ui.session.get(),
                                          ui.fileCache, focused,
                                          ui.session && ui.session->startIsCustom);
            break;
        case PaneType::SessionLoot:
            leafEl = renderSessionLootPane(node.config, s,
                                            ui.session.get(),
                                            ui.sessionRunStats, focused,
                                            paneScrollFrac(&node));
            break;
        case PaneType::Uber:
            leafEl = renderUberPane(node.config, s, ui.fileCache, focused);
            break;
        case PaneType::TerrorZone:
            leafEl = renderTerrorZonePane(s, focused);
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
        text("  Up/Down     move cursor / scroll in focused pane"),
        text("  PgUp/PgDn   page cursor / scroll"),
        text("  Home/End    jump to first / last row"),
        text(""),
        text("Inventory pane (tree)") | bold,
        text("  Right       expand section (or step into first child)"),
        text("  Left        collapse section (or jump to parent)"),
        text("  +           expand all sections"),
        text("  -           collapse all sections"),
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
    line += std::to_string(s.inventory.size()) + " items indexed";
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
//
// `dir` picks direction: +1 advances to the next value (Enter / Right
// arrow); -1 rewinds to the previous value (Left arrow). Both wrap at
// the ends. Other magnitudes are treated as +1.
template <typename Enum>
Enum cycleEnum(Enum current, std::initializer_list<Enum> values, int dir = 1) {
    if (values.size() == 0) return current;
    auto* it = std::find(values.begin(), values.end(), current);
    const int n = static_cast<int>(values.size());
    int idx = (it == values.end()) ? 0 : static_cast<int>(it - values.begin());
    idx = ((idx + (dir < 0 ? -1 : 1)) % n + n) % n;
    return *(values.begin() + idx);
}

void cyclePaneType(PaneNode& leaf, int dir = 1) {
    leaf.config.type = cycleEnum(leaf.config.type, {
        PaneType::Blank,
        PaneType::Chronicle,
        PaneType::Inventory,
        PaneType::Reconcile,
        PaneType::Backups,
        PaneType::BackupLog,
        PaneType::Session,
        PaneType::Character,
        PaneType::SessionLoot,
        PaneType::Uber,
        PaneType::TerrorZone,
    }, dir);
    leaf.config.cursor = 0;
    // Reset the Backups sub-mode so a freshly-cycled-into pane always
    // opens on the summary view rather than orphaned detail.
    leaf.config.backupViewMode     = BackupViewMode::Summary;
    leaf.config.selectedBackupFile.clear();
}

void cycleCategory(PaneNode& leaf, int dir = 1) {
    leaf.config.category = cycleEnum(leaf.config.category, {
        ChronicleCategory::Sets,
        ChronicleCategory::UniquesNormal,
        ChronicleCategory::UniquesExceptional,
        ChronicleCategory::UniquesElite,
        ChronicleCategory::UniquesMisc,
        ChronicleCategory::UniquesAll,
    }, dir);
    // PaneSortKey::Tier is only meaningful for the Uniques (all) view
    // (three-tier grid). Snap back to Base if the user cycles away.
    if (leaf.config.sortKey == PaneSortKey::Tier &&
        leaf.config.category != ChronicleCategory::UniquesAll) {
        leaf.config.sortKey = PaneSortKey::Base;
    }
    leaf.config.cursor = 0;
}

void cycleSortKey(PaneConfig& c, int dir = 1) {
    // Tier sort renders a Normal/Exceptional/Elite grid and is only
    // meaningful for the Uniques (all) category. Omit it from the
    // cycle for every other pane so users can't get stuck on a
    // degenerate one-column layout.
    if (c.category == ChronicleCategory::UniquesAll) {
        c.sortKey = cycleEnum(c.sortKey, {
            PaneSortKey::Name, PaneSortKey::Base,
            PaneSortKey::Owned, PaneSortKey::Tier,
        }, dir);
    } else {
        c.sortKey = cycleEnum(c.sortKey, {
            PaneSortKey::Name, PaneSortKey::Base, PaneSortKey::Owned,
        }, dir);
    }
}

void cycleOwnership(PaneConfig& c, int dir = 1) {
    c.ownership = cycleEnum(c.ownership, {
        OwnershipFilter::All,
        OwnershipFilter::RemainingOnly,
        OwnershipFilter::DiscoveredOnly,
    }, dir);
}

void cycleReconcileKind(PaneConfig& c, int dir = 1) {
    c.reconcileKind = cycleEnum(c.reconcileKind, {
        ReconcileKindFilter::Both,
        ReconcileKindFilter::UniquesOnly,
        ReconcileKindFilter::SetsOnly,
    }, dir);
    c.cursor = 0;
}

int cyclei(int v, int mod) { if (mod <= 0) return 0; v %= mod; if (v < 0) v += mod; return v; }

} // namespace

// ============================================================================
// Driver
// ============================================================================

int runDashboard(const std::filesystem::path& savePath,
                 const std::string& referenceDbOverride,
                 const std::filesystem::path& exePath,
                 const DashboardOptions& options) {
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

#if D2R_HAVE_INOTIFY
    // Backup DB + scheduler. Failure to open is non-fatal (the dashboard
    // still works, just without automatic backups). The startup sweep
    // happens before we enter the FTXUI loop so its writes don't compete
    // with the first render. In --print mode we still open the DB (so
    // the Backups pane can render live rows) but skip the sweep -- the
    // schema apply is idempotent, so the on-disk state stays untouched
    // when nothing needs migrating.
    std::unique_ptr<BackupDb>        backupDb;
    std::unique_ptr<BackupScheduler> backupScheduler;
    try {
        backupDb = std::make_unique<BackupDb>(backupDbPath());
        // Retention: pick up the user's saved values from the config DB
        // (may be defaults if they've never touched it). Both the
        // startup sweep and every subsequent burst honour this.
        RetentionConfig retention;
        if (configDb) {
            const auto cfg = loadBackupRetention(configDb);
            retention.days            = cfg.days;
            retention.sessionsPerFile = cfg.sessions;
        }
        backupScheduler = std::make_unique<BackupScheduler>(
            *backupDb, savePath, retention);
        if (!options.printOnce) {
            backupScheduler->takeStartupSnapshot();
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
            "warning: backup DB unavailable (%s); no automatic backups this session\n",
            ex.what());
        backupScheduler.reset();
        backupDb.reset();
    }
#endif

    UiState ui;
    ui.watchedPath = savePath.string();
    ui.rootPane = configDb ? loadPaneTree(configDb) : makeDefaultLayout();
    if (options.printOnce) {
        ui.printSizeOverride =
            ftxui::Dimensions{options.printWidth, options.printHeight};
    }
#if D2R_HAVE_INOTIFY
    ui.backupDb = backupDb.get();   // may be nullptr; renderers cope
    ui.backupScheduler = backupScheduler.get();
#endif

    // Build a Session snapshot for the pane renderers. Reads the
    // effective start/end epoch from `ui.appSession` (character-
    // agnostic singleton). The start-state is the per-file overlay
    // of the account at `startEpoch`; the end side is always the
    // live snapshot (`endEpoch` only bounds the DISPLAYED window).
    //
    // Uniform per-file semantic: every file (active player, other
    // characters, shared stash) contributes its state at
    // `startEpoch`. A file with no backup at/before that date
    // didn't exist yet at session start and contributes nothing --
    // its current items get stripped from `temp` so they correctly
    // count as "new" now.
    auto buildSession =
        [&](const std::shared_ptr<const DashboardSnapshot>& current)
        -> std::shared_ptr<const Session> {
        // Snapshot the session-window inputs under the mutex. `endEpoch`
        // could momentarily be less than `startEpoch` when the user
        // sets an inverted custom window; collapse to a zero-duration
        // window in that case (renders as "0h 00m 00s").
        std::int64_t startEpoch;
        std::int64_t endEpoch;
        bool         startIsCustom;
        bool         endIsCustom;
        {
            std::lock_guard g(ui.snapshotMutex);
            startEpoch    = ui.appSession.startEpoch();
            endEpoch      = ui.appSession.endEpoch();
            startIsCustom = ui.appSession.startIsCustom();
            endIsCustom   = ui.appSession.endIsCustom();
        }
        if (endEpoch < startEpoch) endEpoch = startEpoch;

        // Fallback used when we can't build a proper session -- treats
        // the current snapshot's active player + inventory as the
        // start-state (trivial 0-delta diff; correct since we have no
        // historical reference).
        auto sessionFromCurrent = [&]() -> std::shared_ptr<const Session> {
            auto out = std::make_shared<Session>();
            out->startEpoch    = startEpoch;
            out->endEpoch      = endEpoch;
            out->startIsCustom = startIsCustom;
            out->endIsCustom   = endIsCustom;
            if (current) out->startState = makeSessionStateFromSnapshot(*current);
            return out;
        };
        if (!current || !current->hasActivePlayer) return sessionFromCurrent();
#if D2R_HAVE_INOTIFY
        if (!backupDb) return sessionFromCurrent();
        const auto& filename = current->activePlayer.file;
        if (filename.empty()) return sessionFromCurrent();

        // Step 1: character-side row at the session start. Missing
        // row (nullopt) means the active player didn't exist yet at
        // start -- fine, step 4 strips its items from `temp`.
        std::optional<BackupDb::Row> row;
        try { row = backupDb->at(filename, startEpoch); }
        catch (const std::exception&) { row.reset(); }
        const std::int64_t characterDate =
            (row && !row->data.empty()) ? row->date : 0;

        // Step 2: stash-side row. Three-tier lookup preserved from the
        // pre-singleton implementation:
        //   1. Newest stash row at/before startEpoch (strict).
        //   2. Auto-mode safety net: oldest non-empty stash on record
        //      (so a session boundary that predates the stash's first
        //      backup still yields a defensible start-state rather
        //      than an empty stash).
        //   3. Nothing -> stash cleared in step 4.
        // Custom-start mode uses strict semantic only (tier 1 or
        // nothing) since the user's intent is "session started at
        // this exact instant; anything before doesn't count".
        std::optional<BackupDb::Row> stashRow;
        std::int64_t                 stashDate = 0;
        std::string                  stashFile;
        if (!stashPath.empty()) {
            stashFile = stashPath.filename().string();
            if (!stashFile.empty()) {
                try { stashRow = backupDb->at(stashFile, startEpoch); }
                catch (const std::exception&) {}
                if (stashRow && !stashRow->data.empty()) {
                    stashDate = stashRow->date;
                } else if (!startIsCustom) {
                    stashRow.reset();
                    std::vector<BackupDb::HistoryRow> stashHist;
                    try { stashHist = backupDb->historyFor(stashFile, 1000); }
                    catch (const std::exception&) {}
                    for (auto it = stashHist.rbegin(); it != stashHist.rend(); ++it) {
                        if (it->state == BackupDb::State::Deleted) continue;
                        if (it->sizeBytes <= 0) continue;
                        try { stashRow = backupDb->at(stashFile, it->date); }
                        catch (const std::exception&) { stashRow.reset(); }
                        if (stashRow && !stashRow->data.empty()) {
                            stashDate = it->date;
                            break;
                        }
                        stashRow.reset();
                    }
                }
            }
        }

        // Step 3: cache check. The key excludes endEpoch because the
        // start-state's item pool is invariant under end drift; a new
        // save landing (which advances endEpoch) still hits the cache.
        // We clone-and-patch endEpoch on such hits so the pane's
        // duration display stays live during autosave bursts without
        // paying for the deep byte-parse in the miss path.
        const UiState::SessionCacheKey key{
            characterDate,
            stashDate,
            startEpoch,
        };
        if (ui.session && ui.sessionCacheKey == key) {
            if (ui.session->endEpoch     == endEpoch &&
                ui.session->endIsCustom  == endIsCustom &&
                ui.session->startIsCustom == startIsCustom) {
                return ui.session;
            }
            auto patched = std::make_shared<Session>(*ui.session);
            patched->endEpoch      = endEpoch;
            patched->startIsCustom = startIsCustom;
            patched->endIsCustom   = endIsCustom;
            return patched;
        }

        // Step 4: miss path -- build a fresh start-state. Start from a
        // shallow-ish copy of `current` so items owned by OTHER
        // characters (SetHolderFour, UniqueArmors, etc. -- .d2s files
        // that live in the same save dir) stay in the start-state's
        // item pool. Otherwise every such item would flag as "new" in
        // the diff, because they'd be present in `now.inventory` but
        // absent from an empty start-state.
        //
        // Optimization: files whose in-file timestamp is <= startEpoch
        // haven't been touched since the start, so their current cache
        // entry already reflects the start state -- skip the historical
        // parse entirely. Typically the vast majority of character
        // files.
        DashboardSnapshot temp = *current;
        const auto lookupU32 = startEpoch > 0
            ? static_cast<std::uint32_t>(startEpoch) : 0u;
        auto stripCharacterItems = [&](const std::string& prefix) {
            temp.inventory.erase(
                std::remove_if(temp.inventory.begin(), temp.inventory.end(),
                    [&](const InventoryItem& inv) {
                        return inv.location.size() >= prefix.size() &&
                               inv.location.compare(0, prefix.size(), prefix) == 0;
                    }),
                temp.inventory.end());
        };
        for (const auto& [otherName, otherEntry] : ui.fileCache.d2s) {
            if (otherName == filename) continue;   // active player handled below
            if (lookupU32 != 0 && otherEntry.character.timestamp <= lookupU32) {
                continue;   // unchanged since start; current entry is correct
            }
            std::optional<BackupDb::Row> otherRow;
            try { otherRow = backupDb->at(otherName, startEpoch); }
            catch (const std::exception&) {}
            if (otherRow && !otherRow->data.empty()) {
                (void)overrideActivePlayerFromBytes(temp, db, otherRow->data, otherName);
            } else {
                stripCharacterItems(otherName);
            }
        }
        if (row && !row->data.empty()) {
            if (!overrideActivePlayerFromBytes(temp, db, row->data, filename)) {
                return sessionFromCurrent();
            }
        } else {
            stripCharacterItems(filename);
        }
        if (stashRow && !stashRow->data.empty()) {
            (void)overrideSharedStashFromBytes(temp, db, stashRow->data);
        } else if (!stashFile.empty()) {
            clearSharedStashInSnapshot(temp);
        }

        auto out = std::make_shared<Session>();
        out->startEpoch    = startEpoch;
        out->endEpoch      = endEpoch;
        out->startIsCustom = startIsCustom;
        out->endIsCustom   = endIsCustom;
        out->startState = makeSessionStateFromSnapshot(temp);
        ui.sessionCacheKey = key;
        return out;
#else
        return sessionFromCurrent();
#endif
    };
    // Refresh `autoEndEpoch` from the newest backup date on record
    // across ALL files. Called at boot and after every rebuild(). Uses
    // SELECT max(date) which is O(rows) linear across the file×date
    // index -- cheap in practice. Falls back to the current wall clock
    // when the DB is empty (fresh install) so the singleton always has
    // a valid value.
    auto refreshAutoEndEpoch = [&]() {
        std::int64_t newest = 0;
#if D2R_HAVE_INOTIFY
        if (backupDb) {
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "SELECT MAX(date) FROM backup";
            if (sqlite3_prepare_v2(backupDb->raw(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW &&
                    sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
                    newest = sqlite3_column_int64(stmt, 0);
                }
                sqlite3_finalize(stmt);
            }
        }
#endif
        if (newest <= 0) newest = static_cast<std::int64_t>(std::time(nullptr));
        std::lock_guard g(ui.snapshotMutex);
        ui.appSession.autoEndEpoch = newest;
    };

    {
        // Seed the session singleton. `autoStartEpoch` starts at
        // dashboard boot -- the launch-burst callback replaces it if
        // D2R starts while we're running. `autoEndEpoch` is refreshed
        // from the DB immediately below.
        {
            std::lock_guard g(ui.snapshotMutex);
            ui.appSession.autoStartEpoch = static_cast<std::int64_t>(std::time(nullptr));
        }
        // Cold start: full scan populates the file cache. Every
        // subsequent rebuild uses the cache; only files inotify says
        // changed get re-parsed.
        refreshDashboardCacheFromDirectory(db, savePath, stashPath, ui.fileCache);
        refreshAutoEndEpoch();
        auto snap = std::make_shared<DashboardSnapshot>(
            aggregateDashboardSnapshot(db, ui.fileCache));
        auto session = buildSession(snap);
        // Fresh per-character run stats for the current session
        // window. Cheap enough to redo on every rebuild; not part of
        // buildSession's cache since counts drift with each save.
        SessionRunStats runStats;
        {
            std::lock_guard g(ui.snapshotMutex);
            runStats = computeSessionRunStats(backupDb.get(), ui.fileCache,
                                                ui.appSession.startEpoch(),
                                                ui.appSession.endEpoch());
        }
        std::lock_guard g(ui.snapshotMutex);
        ui.snapshot        = std::move(snap);
        ui.session         = std::move(session);
        ui.sessionRunStats = std::move(runStats);
    }

    auto screen = ScreenInteractive::Fullscreen();

#if D2R_HAVE_INOTIFY
    // Feed the BackupLog pane every time the scheduler persists a
    // successful backup (or tombstone). The callback runs on the
    // watcher thread; we hold `backupLogMutex` only for the push and
    // then poke the UI so the pane redraws on the next frame.
    if (backupScheduler) {
        backupScheduler->setInsertCallback(
            [&](std::string_view name, std::int64_t whenUnix, BackupDb::State) {
                {
                    std::lock_guard<std::mutex> g(ui.backupLogMutex);
                    ui.backupLog.push_back({std::string(name), whenUnix});
                    while (ui.backupLog.size() > kBackupLogCap) {
                        ui.backupLog.pop_front();
                    }
                }
                screen.PostEvent(Event::Custom);
            });
        // Launch-burst callback: shift the session's autoStartEpoch to
        // the observed launch time. Custom overrides on the singleton
        // are sticky (startEpoch() returns the custom value when set),
        // so this is a no-op for users who've fixed a custom start.
        backupScheduler->setLaunchCallback(
            [&](std::int64_t whenUnix) {
                {
                    std::lock_guard<std::mutex> g(ui.snapshotMutex);
                    ui.appSession.autoStartEpoch = whenUnix;
                }
                screen.PostEvent(Event::Custom);
            });
    }
#endif

    auto currentSnapshot = [&]() {
        std::lock_guard g(ui.snapshotMutex);
        return ui.snapshot;
    };
    // Resolve the current Backups pane character filter (same helper the
    // renderer uses at draw time). The Enter-to-detail navigation and
    // the shownRows count query both need this so cursor indexing lines
    // up with what the user sees in the summary.
    auto backupsCharFilter = [&]() -> std::string {
        auto snap = currentSnapshot();
        if (!snap) return {};
        return resolveBackupsCharacterFilter(ui.rootPane, *snap);
    };
    auto rebuild = [&]() {
        // Drain the watcher's pending-change list. If non-empty, only
        // the named files get re-parsed -- the other 50+ character
        // files stay in the cache. Empty list means "manual [r] or
        // best-effort recovery" and we fall back to a full rescan.
        std::vector<std::string> changed;
        {
            std::lock_guard g(ui.pendingChangesMutex);
            changed = std::move(ui.pendingChangedFiles);
            ui.pendingChangedFiles.clear();
        }
        if (changed.empty()) {
            refreshDashboardCacheFromDirectory(db, savePath, stashPath, ui.fileCache);
        } else {
            refreshDashboardCacheFromChanges(db, savePath, stashPath,
                                              ui.fileCache, changed);
        }
        auto snap = std::make_shared<DashboardSnapshot>(
            aggregateDashboardSnapshot(db, ui.fileCache));
        // Advance `autoEndEpoch` to the newest backup date on record
        // (buildSession reads it via the singleton).
        refreshAutoEndEpoch();
        auto session = buildSession(snap);
        SessionRunStats runStats;
        {
            std::lock_guard g(ui.snapshotMutex);
            runStats = computeSessionRunStats(backupDb.get(), ui.fileCache,
                                                ui.appSession.startEpoch(),
                                                ui.appSession.endEpoch());
        }
        std::lock_guard g(ui.snapshotMutex);
        ui.snapshot        = std::move(snap);
        ui.session         = std::move(session);
        ui.sessionRunStats = std::move(runStats);
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

        int leafIdx = 0;
        // The pane tree gets every row except the status footer. The
        // top row (Character / Uber / TerrorZone / SessionLoot) is now
        // a real horizontal split at the root, so paneWeight biases
        // its 12/40 share of the terminal height instead of us carving
        // out a fixed 12 rows.
        const auto dims = ui.printSizeOverride.value_or(ftxui::Terminal::Size());
        const int paneWidth  = std::max(1, dims.dimx);
        const int paneHeight = std::max(1, dims.dimy - 1);
        Element panes = renderPane(ui.rootPane, ui, s, leafIdx,
                                    paneWidth, paneHeight);

        std::string globalHint;
        if      (ui.configMode) globalHint = " Up/Down  Enter  Esc ";
        else if (ui.searchMode) globalHint = " type... Enter apply | Esc cancel ";
        else                    globalHint = " [Tab] focus  [c] config  [/] search  [q] quit  [?] help ";

        Element root = vbox({
            panes,
            hbox({ renderStatus(ui, s) | flex, text(globalHint) | dim }),
        });

        // `clear_under` paints the modal opaquely so the panes behind it
        // don't bleed through the borders/whitespace of the popup.
        if (ui.helpVisible) {
            root = dbox({ root, renderHelpModal() | clear_under | center });
        }
        if (ui.recoveryModalVisible) {
            root = dbox({ root,
                renderRecoveryModal(ui.recoveryModal, ui.watchedPath)
                    | clear_under | center });
        }
        if (ui.retentionModalVisible) {
            root = dbox({ root,
                renderRetentionModal(ui.retentionModal, ui.backupDb)
                    | clear_under | center });
        }
        if (ui.characterPickerVisible) {
            root = dbox({ root,
                renderCharacterPickerModal(ui.characterPicker)
                    | clear_under | center });
        }
        if (ui.sessionTimeInputVisible) {
            root = dbox({ root,
                renderSessionTimeInputModal(ui.sessionTimeInputModal)
                    | clear_under | center });
        }
        return root;
    });

    // Print-once mode: render the layout to a fixed-size Screen, dump
    // it as ANSI to stdout, and exit. No event loop, no watcher, no
    // side effects.
    if (options.printOnce) {
        auto element = layout->Render();
        auto screen  = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(options.printWidth),
            ftxui::Dimension::Fixed(options.printHeight));
        ftxui::Render(screen, element);
        std::fputs(screen.ToString().c_str(), stdout);
        std::fputc('\n', stdout);
        if (configDb) closeDashboardConfigDb(configDb);
        return 0;
    }

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

        // ---- RECOVERY modal ----
        if (ui.recoveryModalVisible) {            auto& m = ui.recoveryModal;
            if (e == Event::Escape) {
                ui.recoveryModalVisible = false;
                return true;
            }
            if (e == Event::Tab) {
                m.destChoice = m.destChoice == 0 ? 1 : 0;
                return true;
            }
            if (e == Event::Backspace && m.destChoice == 1 && !m.altPathInput.empty()) {
                m.altPathInput.pop_back();
                return true;
            }
            // Only accept printable characters into the alt path input.
            if (m.destChoice == 1 && e.is_character()) {
                const auto& s = e.character();
                if (!s.empty() && static_cast<unsigned char>(s[0]) >= 32) {
                    m.altPathInput += s;
                }
                return true;
            }
            if (e == Event::Return) {
                if (!ui.backupDb || !ui.backupScheduler) {
                    m.status = "error: backup DB unavailable";
                    return true;
                }
                std::filesystem::path dest = m.destChoice == 0
                    ? std::filesystem::path(ui.watchedPath)
                    : std::filesystem::path(m.altPathInput);
                if (dest.empty()) {
                    m.status = "error: destination path is empty";
                    return true;
                }
                d2r::RecoverySpec spec;
                spec.destDir               = dest;
                spec.filename              = m.filename;
                spec.atUnix                = m.atUnix;
                spec.preRecoveryBackup     = true;
                spec.allowTombstoneRestore = (m.destChoice == 0);
                try {
                    const auto rep = d2r::recoverFile(
                        *ui.backupDb, *ui.backupScheduler, spec);
                    if (!rep.restored && !rep.wasTombstone) {
                        m.status = "no backup exists at or before that moment";
                        return true;
                    }
                    // Success -- close the modal and refresh the snapshot
                    // so the pane (and any Chronicle/Inventory that read
                    // the destination) shows the restored state.
                    ui.recoveryModalVisible = false;
                    rebuild();
                } catch (const std::exception& ex) {
                    m.status = std::string("error: ") + ex.what();
                }
                return true;
            }
            return true;   // swallow all other events while modal is up
        }

        // ---- RETENTION modal ----
        if (ui.retentionModalVisible) {
            auto& m = ui.retentionModal;
            if (e == Event::Escape) {
                ui.retentionModalVisible = false;
                return true;
            }
            if (e == Event::Tab) {
                m.focused = m.focused == 0 ? 1 : 0;
                return true;
            }
            auto& buf = m.focused == 0 ? m.daysBuf : m.sessionsBuf;
            if (e == Event::Backspace) {
                if (!buf.empty()) buf.pop_back();
                return true;
            }
            if (e.is_character()) {
                const auto& s = e.character();
                if (!s.empty() && s[0] >= '0' && s[0] <= '9' && buf.size() < 8) {
                    buf += s;
                }
                return true;
            }
            if (e == Event::Return) {
                // Parse + validate.
                int days = m.daysBuf.empty()     ? 0 : std::atoi(m.daysBuf.c_str());
                int sess = m.sessionsBuf.empty() ? 0 : std::atoi(m.sessionsBuf.c_str());
                if (days < 0 || sess < 0) {
                    m.status = "error: values must be non-negative";
                    return true;
                }
                if (configDb) {
                    try {
                        d2r::saveBackupRetention(configDb, {days, sess});
                    } catch (const std::exception& ex) {
                        m.status = std::string("error: ") + ex.what();
                        return true;
                    }
                }
                if (ui.backupScheduler) {
                    ui.backupScheduler->setRetention({days, sess});
                }
                ui.retentionModalVisible = false;
                return true;
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

        // ---- CHARACTER PICKER modal ----
        // Same shape as the character picker, keyed off the
        // pane's characterSelection field. options[0] is always the
        // empty string == "auto (newest saved)".
        if (ui.characterPickerVisible) {
            auto& p = ui.characterPicker;
            const int n = static_cast<int>(p.options.size());
            if (e == Event::Escape) {
                ui.characterPickerVisible = false;
                p.target = nullptr;
                p.options.clear();
                return true;
            }
            if (n == 0) return true;
            if (e == Event::ArrowUp)   { p.cursor = std::max(0, p.cursor - 1); return true; }
            if (e == Event::ArrowDown) { p.cursor = std::min(n - 1, p.cursor + 1); return true; }
            if (e == Event::PageUp)    { p.cursor = std::max(0, p.cursor - 10); return true; }
            if (e == Event::PageDown)  { p.cursor = std::min(n - 1, p.cursor + 10); return true; }
            if (e == Event::Home)      { p.cursor = 0; return true; }
            if (e == Event::End)       { p.cursor = n - 1; return true; }
            if (e == Event::Return) {
                if (p.target) {
                    p.target->config.characterSelection = p.options[p.cursor];
                }
                ui.characterPickerVisible = false;
                p.target = nullptr;
                p.options.clear();
                persistLayout();
                // No anchor rebuild required -- the character selection
                // only changes what renderCharacterPane resolves to;
                // the anchor cache continues to auto-track the newest
                // save. Per-character anchors are a follow-up.
                return true;
            }
            return true;
        }

        // ---- SESSION TIME INPUT modal (free-form manual entry) ----
        if (ui.sessionTimeInputVisible) {
            auto& m = ui.sessionTimeInputModal;
            if (e == Event::Escape) {
                ui.sessionTimeInputVisible = false;
                m.target = nullptr;
                m.buffer.clear();
                m.status.clear();
                m.isEnd = false;
                return true;
            }
            if (e == Event::Backspace) {
                if (!m.buffer.empty()) m.buffer.pop_back();
                m.status.clear();
                return true;
            }
            if (e.is_character()) {
                const auto& ch = e.character();
                // Cap length so a runaway paste can't render past the
                // modal; any accepted format easily fits in 40 chars.
                if (!ch.empty() && m.buffer.size() < 40) {
                    m.buffer += ch;
                    m.status.clear();
                }
                return true;
            }
            if (e == Event::Return) {
                const auto parsed = d2r::parseUserDateTime(
                    m.buffer, static_cast<std::int64_t>(std::time(nullptr)));
                if (!parsed) {
                    m.status = "error: could not parse '" + m.buffer + "'";
                    return true;
                }
                // Write to the AppSession singleton. `setCustomEnd`
                // silently ignores end-without-start (invariant).
                {
                    std::lock_guard<std::mutex> g(ui.snapshotMutex);
                    if (m.isEnd) ui.appSession.setCustomEnd(*parsed);
                    else         ui.appSession.setCustomStart(*parsed);
                }
                ui.sessionTimeInputVisible = false;
                m.target = nullptr;
                m.buffer.clear();
                m.status.clear();
                m.isEnd = false;
                rebuild();
                return true;
            }
            return true;   // swallow everything else while modal is up
        }

        // ---- CONFIG mode ----
        if (ui.configMode) {
            auto* leaf = focusedLeaf();
            if (!leaf) { ui.configMode = false; return true; }
            const bool canDelete = findParent(ui.rootPane, leaf) != nullptr;
            AppSession appSessionCopy;
            {
                std::lock_guard<std::mutex> g(ui.snapshotMutex);
                appSessionCopy = ui.appSession;
            }
            const auto items = buildConfigMenu(leaf->config, canDelete, appSessionCopy);
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
            // Left/Right shortcut the highlighted item's cycler in
            // reverse / forward. Non-cyclable actions (splits, delete,
            // close, one-shots like ClearBackupLog) require Enter.
            auto applyCyclable = [&](const ConfigMenuItem& item, int dir) -> bool {
                switch (item.kind) {
                    case ConfigMenuItem::CycleType:      cyclePaneType(*leaf, dir); return true;
                    case ConfigMenuItem::CycleCategory:  cycleCategory(*leaf, dir); return true;
                    case ConfigMenuItem::CycleSort:      cycleSortKey(leaf->config, dir); return true;
                    case ConfigMenuItem::ToggleSortDir:  leaf->config.sortAsc = !leaf->config.sortAsc; return true;
                    case ConfigMenuItem::CycleOwnership: cycleOwnership(leaf->config, dir); return true;
                    case ConfigMenuItem::CycleReconcileKind: cycleReconcileKind(leaf->config, dir); return true;
                    case ConfigMenuItem::ToggleQuality:
                        leaf->config.inventoryQualityMask ^= qualityBit(item.quality);
                        leaf->config.cursor = 0;
                        return true;
                    case ConfigMenuItem::CyclePaneWeight: {
                        // 1..4 wrap in either direction.
                        int w = std::max(1, leaf->config.paneWeight);
                        w = ((w - 1 + (dir < 0 ? -1 : 1)) % 4 + 4) % 4 + 1;
                        leaf->config.paneWeight = w;
                        return true;
                    }
                    case ConfigMenuItem::CycleInfoLevel: {
                        int lvl = std::clamp(leaf->config.infoLevel, 1, 3);
                        lvl = ((lvl - 1 + (dir < 0 ? -1 : 1)) % 3 + 3) % 3 + 1;
                        leaf->config.infoLevel = lvl;
                        return true;
                    }
                    default: return false;
                }
            };
            if (e == Event::ArrowLeft || e == Event::ArrowRight) {
                if (n > 0) {
                    if (applyCyclable(items[ui.configMenu],
                                       e == Event::ArrowLeft ? -1 : 1)) {
                        persistLayout();
                    }
                }
                return true;
            }
            if (e == Event::Return) {
                const auto& item = items[ui.configMenu];
                if (applyCyclable(item, 1)) {
                    persistLayout();
                    return true;
                }
                const auto action = item.kind;
                switch (action) {
                    // Cyclables handled above; the remaining actions
                    // are one-shots that only respond to Enter.
                    case ConfigMenuItem::CycleType:
                    case ConfigMenuItem::CycleCategory:
                    case ConfigMenuItem::CycleSort:
                    case ConfigMenuItem::ToggleSortDir:
                    case ConfigMenuItem::CycleOwnership:
                    case ConfigMenuItem::CycleReconcileKind:
                    case ConfigMenuItem::ToggleQuality:
                    case ConfigMenuItem::CyclePaneWeight:
                    case ConfigMenuItem::CycleInfoLevel:
                        break;
                    case ConfigMenuItem::ClearBackupLog: {
                        std::lock_guard<std::mutex> g(ui.backupLogMutex);
                        ui.backupLog.clear();
                        ui.configMode = false;
                        break;
                    }
                    case ConfigMenuItem::ResetSession: {
                        // Clear the singleton's custom start + end so\n                        // both sides revert to auto. Rebuild the shared\n                        // Session on the current snapshot immediately\n                        // so panes reflect the change without waiting\n                        // for the next save event.
                        std::shared_ptr<const DashboardSnapshot> snapNow;
                        {
                            std::lock_guard<std::mutex> g(ui.snapshotMutex);
                            ui.appSession.clearCustom();
                            snapNow = ui.snapshot;
                        }
                        auto rebuilt = buildSession(snapNow);
                        SessionRunStats runStats;
                        {
                            std::lock_guard<std::mutex> g(ui.snapshotMutex);
                            runStats = computeSessionRunStats(
                                backupDb.get(), ui.fileCache,
                                ui.appSession.startEpoch(),
                                ui.appSession.endEpoch());
                        }
                        {
                            std::lock_guard<std::mutex> g(ui.snapshotMutex);
                            ui.session         = std::move(rebuilt);
                            ui.sessionRunStats = std::move(runStats);
                        }
                        ui.configMode = false;
                        break;
                    }
                    case ConfigMenuItem::SetSessionStart:
                    case ConfigMenuItem::SetSessionEnd: {
                        // Open the free-form time entry modal. Enter
                        // handler parses via d2r::parseUserDateTime and
                        // writes the resulting epoch into the
                        // AppSession singleton (`customStartEpoch` or
                        // `customEndEpoch`); the invariant "custom end
                        // implies custom start" is enforced by
                        // AppSession::setCustomEnd + only surfacing
                        // the End menu item when start is already
                        // custom.
                        const bool isEnd = (action == ConfigMenuItem::SetSessionEnd);
                        ui.sessionTimeInputModal.target = nullptr;   // singleton target
                        ui.sessionTimeInputModal.isEnd  = isEnd;
                        ui.sessionTimeInputModal.status.clear();
                        // Prefill the buffer with the current custom
                        // value (if any) so tweaking a small offset is
                        // a couple of keys.
                        std::int64_t seed = 0;
                        {
                            std::lock_guard<std::mutex> g(ui.snapshotMutex);
                            const auto& s = ui.appSession;
                            if (isEnd && s.customEndEpoch)      seed = *s.customEndEpoch;
                            else if (!isEnd && s.customStartEpoch) seed = *s.customStartEpoch;
                        }
                        if (seed > 0) {
                            ui.sessionTimeInputModal.buffer = formatWallDateTime(seed);
                        } else {
                            ui.sessionTimeInputModal.buffer.clear();
                        }
                        ui.sessionTimeInputVisible = true;
                        ui.configMode              = false;
                        break;
                    }
                    case ConfigMenuItem::PickCharacter: {
                        // Build the option list from the file cache:
                        // sorted basenames, prepended with "" == auto.
                        std::vector<std::string> opts;
                        opts.push_back("");
                        for (const auto& [name, _] : ui.fileCache.d2s) opts.push_back(name);
                        std::sort(opts.begin() + 1, opts.end());
                        int cursor = 0;
                        if (!leaf->config.characterSelection.empty()) {
                            for (int i = 0; i < (int)opts.size(); ++i) {
                                if (opts[i] == leaf->config.characterSelection) {
                                    cursor = i;
                                    break;
                                }
                            }
                        }
                        ui.characterPicker.target  = leaf;
                        ui.characterPicker.options = std::move(opts);
                        ui.characterPicker.cursor  = cursor;
                        ui.characterPickerVisible  = true;
                        ui.configMode              = false;
                        break;
                    }
                    case ConfigMenuItem::ResetCharacterToAuto: {
                        leaf->config.characterSelection.clear();
                        ui.configMode = false;
                        persistLayout();
                        break;
                    }
                    case ConfigMenuItem::ToggleUberShowUbers: {
                        leaf->config.uberShowUbers = !leaf->config.uberShowUbers;
                        persistLayout();
                        break;
                    }
                    case ConfigMenuItem::ToggleUberShowTorchByClass: {
                        leaf->config.uberShowTorchByClass = !leaf->config.uberShowTorchByClass;
                        persistLayout();
                        break;
                    }
                    case ConfigMenuItem::ToggleSessionLootShowRunes: {
                        leaf->config.sessionLootShowRunes = !leaf->config.sessionLootShowRunes;
                        persistLayout();
                        break;
                    }
                    case ConfigMenuItem::ToggleBackupsRunCollapse: {
                        leaf->config.backupsRunCollapse = !leaf->config.backupsRunCollapse;
                        // Reset cursor + expand state when the view
                        // structure changes; row indices no longer map.
                        leaf->config.cursor = 0;
                        ui.expandedRunEndEpoch.reset();
                        persistLayout();
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

        // Session Info pane isn't row-cursor driven; it scrolls via a
        // per-leaf fraction that feeds focusPositionRelative inside
        // the pane's yframe. Handle arrows here so the shownRows path
        // below (which only knows about cursor-driven panes) never
        // sees the event.
        if (leaf->config.type == PaneType::SessionLoot) {
            auto& frac = ui.paneScrollFrac[leaf];
            auto nudge = [&](float delta) {
                frac = std::clamp(frac + delta, 0.f, 1.f);
            };
            if (e == Event::ArrowUp)   { nudge(-0.05f); return true; }
            if (e == Event::ArrowDown) { nudge(+0.05f); return true; }
            if (e == Event::PageUp)    { nudge(-0.25f); return true; }
            if (e == Event::PageDown)  { nudge(+0.25f); return true; }
            if (e == Event::Home)      { frac = 0.f;    return true; }
            if (e == Event::End)       { frac = 1.f;    return true; }
        }

        // Inventory pane: tree navigation with Left/Right and bulk
        // expand/collapse with +/-. Everything happens against the
        // currently-visible row list so cursor jumps and expand
        // toggles stay in sync with the render.
        if (leaf->config.type == PaneType::Inventory) {
            auto rebuildRows = [&]() {
                auto snap = currentSnapshot();
                const auto filtered = filterInventory(leaf->config, *snap);
                const auto tree     = buildInventoryTree(filtered);
                static const std::unordered_set<std::string> kEmpty;
                const auto pit = ui.paneExpanded.find(leaf);
                const auto& expanded = pit == ui.paneExpanded.end() ? kEmpty : pit->second;
                const bool searchActive = !leaf->config.searchQuery.empty();
                return std::pair{tree, emitInventoryRows(tree, expanded, searchActive)};
            };
            auto& expandSet = ui.paneExpanded[leaf];
            // + expands every top-level and section path present.
            if (e == Event::Character('+') || e == Event::Character('=')) {
                auto [tree, _rows] = rebuildRows();
                for (const auto& p : collectExpandablePaths(tree)) {
                    expandSet.insert(p);
                }
                return true;
            }
            // - collapses everything.
            if (e == Event::Character('-') || e == Event::Character('_')) {
                expandSet.clear();
                leaf->config.cursor = 0;
                return true;
            }
            if (e == Event::ArrowRight) {
                auto [_tree, rows] = rebuildRows();
                if (rows.empty()) return true;
                const int cur = std::clamp(leaf->config.cursor,
                                            0, (int)rows.size() - 1);
                const auto& row = rows[cur];
                if (row.kind == InvVisibleRow::Kind::Item) return true;
                if (!row.expanded) {
                    expandSet.insert(row.path);
                } else if (cur + 1 < (int)rows.size()) {
                    // Already expanded -> step cursor into the first
                    // child of this header.
                    leaf->config.cursor = cur + 1;
                }
                return true;
            }
            if (e == Event::ArrowLeft) {
                auto [_tree, rows] = rebuildRows();
                if (rows.empty()) return true;
                const int cur = std::clamp(leaf->config.cursor,
                                            0, (int)rows.size() - 1);
                const auto& row = rows[cur];
                // Item -> jump cursor to enclosing section header.
                if (row.kind == InvVisibleRow::Kind::Item) {
                    for (int i = cur - 1; i >= 0; --i) {
                        if (rows[i].path == row.parentPath) {
                            leaf->config.cursor = i;
                            break;
                        }
                    }
                    return true;
                }
                // Section header -> collapse if expanded; jump to
                // enclosing top-level if already collapsed.
                if (row.expanded) {
                    expandSet.erase(row.path);
                    return true;
                }
                if (!row.parentPath.empty()) {
                    for (int i = cur - 1; i >= 0; --i) {
                        if (rows[i].path == row.parentPath) {
                            leaf->config.cursor = i;
                            break;
                        }
                    }
                    return true;
                }
                // Top-level, already collapsed -> collapse remains
                // no-op (nothing above it in the tree).
                return true;
            }
        }

        // Backups pane: Enter drills from summary into detail for the
        // filename at the cursor; Escape from detail returns to summary.
        // Both reset the cursor so the drill-in lands at the newest row.
        if (leaf->config.type == PaneType::Backups) {
            if (e == Event::Return &&
                leaf->config.backupViewMode == BackupViewMode::Summary) {
                const auto order = backupsSummaryOrder(ui.backupDb, backupsCharFilter());
                if (!order.empty()) {
                    const int idx = std::clamp(leaf->config.cursor,
                                                0, (int)order.size() - 1);
                    leaf->config.selectedBackupFile = order[idx];
                    leaf->config.backupViewMode     = BackupViewMode::Detail;
                    leaf->config.cursor             = 0;
                }
                return true;
            }
            // Enter in collapsed detail view toggles expand on the Run
            // row under the cursor. Ignored on Autosave sub-rows and
            // when the cursor sits on an in-progress run (no run-end
            // epoch to key on).
            if (e == Event::Return &&
                leaf->config.backupViewMode == BackupViewMode::Detail &&
                leaf->config.backupsRunCollapse && ui.backupDb) {
                std::vector<BackupDb::HistoryRow> hist;
                try {
                    hist = ui.backupDb->historyFor(
                        leaf->config.selectedBackupFile, 2048);
                } catch (const std::exception&) {}
                auto runs = d2r::groupRunsForFile(
                    leaf->config.selectedBackupFile, hist, 0);
                std::reverse(runs.begin(), runs.end());
                int vi = 0;
                std::optional<std::int64_t> toggleEpoch;
                for (const auto& r : runs) {
                    if (vi == leaf->config.cursor) {
                        if (!r.inProgress) toggleEpoch = r.endEpoch;
                        break;
                    }
                    ++vi;
                    const bool expanded = ui.expandedRunEndEpoch
                                            && *ui.expandedRunEndEpoch == r.endEpoch
                                            && !r.inProgress;
                    if (expanded) vi += static_cast<int>(r.autosaveDates.size());
                }
                if (toggleEpoch) {
                    if (ui.expandedRunEndEpoch
                     && *ui.expandedRunEndEpoch == *toggleEpoch) {
                        ui.expandedRunEndEpoch.reset();
                    } else {
                        ui.expandedRunEndEpoch = *toggleEpoch;
                    }
                }
                return true;
            }
            if (e == Event::Escape &&
                leaf->config.backupViewMode == BackupViewMode::Detail) {
                leaf->config.backupViewMode = BackupViewMode::Summary;
                leaf->config.cursor         = 0;
                return true;
            }
            // R opens the recovery modal for the row currently under the
            // cursor in the detail view. In collapsed mode, the cursor
            // indexes visual rows (Run summaries + unfolded autosaves);
            // we walk the same grouping the renderer uses to resolve
            // the underlying backup date.
            if ((e == Event::Character('R') || e == Event::Character('r')) &&
                leaf->config.backupViewMode == BackupViewMode::Detail &&
                ui.backupDb) {
                try {
                    const auto hist = ui.backupDb->historyFor(
                        leaf->config.selectedBackupFile, 2048);
                    if (!hist.empty()) {
                        std::int64_t rowDate = 0;
                        if (leaf->config.backupsRunCollapse) {
                            auto runs = d2r::groupRunsForFile(
                                leaf->config.selectedBackupFile, hist, 0);
                            std::reverse(runs.begin(), runs.end());
                            int vi = 0;
                            for (const auto& r : runs) {
                                if (vi == leaf->config.cursor) {
                                    rowDate = r.inProgress
                                                ? (r.autosaveDates.empty()
                                                    ? 0
                                                    : r.autosaveDates.back())
                                                : r.endEpoch;
                                    break;
                                }
                                ++vi;
                                const bool expanded = ui.expandedRunEndEpoch
                                                        && *ui.expandedRunEndEpoch == r.endEpoch
                                                        && !r.inProgress;
                                if (expanded) {
                                    // Autosaves unfold newest-first.
                                    for (auto it = r.autosaveDates.rbegin();
                                         it != r.autosaveDates.rend(); ++it) {
                                        if (vi == leaf->config.cursor) {
                                            rowDate = *it;
                                            break;
                                        }
                                        ++vi;
                                    }
                                    if (rowDate) break;
                                }
                            }
                        } else {
                            const int idx = std::clamp(leaf->config.cursor,
                                                        0, (int)hist.size() - 1);
                            rowDate = hist[idx].date;
                        }
                        if (rowDate > 0) {
                            ui.recoveryModal = UiState::RecoveryModal{};
                            ui.recoveryModal.filename = leaf->config.selectedBackupFile;
                            ui.recoveryModal.atUnix   = rowDate;
                            ui.recoveryModalVisible   = true;
                        }
                    }
                } catch (const std::exception&) {
                    // Ignore -- next render shows an empty history view.
                }
                return true;
            }
            // E on the summary opens the retention editor. Populated
            // from the live scheduler config so the buffers show what's
            // currently in effect.
            if ((e == Event::Character('E') || e == Event::Character('e')) &&
                leaf->config.backupViewMode == BackupViewMode::Summary) {
                ui.retentionModal = UiState::RetentionModal{};
                if (ui.backupScheduler) {
                    const auto cfg = ui.backupScheduler->retention();
                    ui.retentionModal.daysBuf     = std::to_string(cfg.days);
                    ui.retentionModal.sessionsBuf = std::to_string(cfg.sessionsPerFile);
                }
                ui.retentionModalVisible = true;
                return true;
            }
        }

        const auto shownRows = [&]() -> int {
            auto snap = currentSnapshot();
            if (leaf->config.type == PaneType::Chronicle)
                return (int)filterForLeaf(leaf->config, *snap).size();
            if (leaf->config.type == PaneType::Inventory) {
                // Inventory renders a tree; visible rows include
                // top-level and section headers plus items. Compute
                // exactly what the render would show given the
                // current expand set + search state so cursor bounds
                // stay in sync.
                const auto filtered = filterInventory(leaf->config, *snap);
                const auto tree     = buildInventoryTree(filtered);
                static const std::unordered_set<std::string> kEmpty;
                const auto pit = ui.paneExpanded.find(leaf);
                const auto& expanded = pit == ui.paneExpanded.end() ? kEmpty : pit->second;
                const bool searchActive = !leaf->config.searchQuery.empty();
                return (int)emitInventoryRows(tree, expanded, searchActive).size();
            }
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
            if (leaf->config.type == PaneType::Backups) {
                if (!ui.backupDb) return 0;
                if (leaf->config.backupViewMode == BackupViewMode::Detail) {
                    try {
                        auto hist = ui.backupDb->historyFor(
                            leaf->config.selectedBackupFile, 2048);
                        if (!leaf->config.backupsRunCollapse) {
                            return static_cast<int>(hist.size());
                        }
                        // Collapsed mode: rows = runs + (if expanded)
                        // unfolded autosaves for the current expand target.
                        auto runs = d2r::groupRunsForFile(
                            leaf->config.selectedBackupFile, hist, 0);
                        int total = static_cast<int>(runs.size());
                        if (ui.expandedRunEndEpoch) {
                            for (const auto& r : runs) {
                                if (!r.inProgress &&
                                    r.endEpoch == *ui.expandedRunEndEpoch) {
                                    total += static_cast<int>(
                                        r.autosaveDates.size());
                                    break;
                                }
                            }
                        }
                        return total;
                    } catch (const std::exception&) { return 0; }
                }
                return (int)backupsSummaryOrder(ui.backupDb, backupsCharFilter()).size();
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
            auto* schedRaw = backupScheduler.get();
            watcherThread = std::thread([w = watcher.get(), &ui, postRebuild, schedRaw] {
                while (!ui.shutdown.load(std::memory_order_relaxed)) {
                    auto trig = w->waitForChange();
                    if (!trig) break;   // shutdown() or spurious signal
                    if (ui.shutdown.load()) break;
                    // Persist bytes before the rebuild so the backup and
                    // the visible snapshot come from the same on-disk
                    // state.
                    if (schedRaw && trig->kind == DirectoryWatcher::Trigger::Kind::Primary) {
                        try {
                            schedRaw->handleWatcherEvents(trig->files);
                        } catch (const std::exception& ex) {
                            std::fprintf(stderr, "[backup] watcher-handler failed: %s\n",
                                         ex.what());
                        }
                    }
                    // Forward the changed-file basenames to the main
                    // thread. rebuild() drains this list and only
                    // re-parses the named files -- massive win vs
                    // re-parsing every .d2s in the save dir on every
                    // autosave burst (see refreshDashboardCacheFromChanges).
                    if (trig->kind == DirectoryWatcher::Trigger::Kind::Primary
                        && !trig->files.empty()) {
                        std::lock_guard g(ui.pendingChangesMutex);
                        ui.pendingChangedFiles.reserve(
                            ui.pendingChangedFiles.size() + trig->files.size());
                        for (const auto& f : trig->files) {
                            ui.pendingChangedFiles.push_back(f.name);
                        }
                    }
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
