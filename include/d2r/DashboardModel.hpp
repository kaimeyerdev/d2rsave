// Data model for the interactive TUI dashboard. `buildSnapshot` walks the
// save directory + shared stash once and returns a plain-data snapshot for
// the FTXUI layer to render. UI-free; safe to unit-test without ftxui.
//
// Only compiled when D2R_HAVE_SQLITE is defined (all data-source code
// already requires the reference DB).

#pragma once

#include "d2r/BackupDb.hpp"
#include "d2r/Character.hpp"
#include "d2r/Item.hpp"
#include "d2r/SharedStash.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace d2r {

class RefDb;

// Active-player summary: the .d2s with the highest `Character.timestamp`.
struct ActivePlayer {
    std::string       file;               // e.g. "Kai.d2s"
    std::string       name;
    CharacterClass    characterClass = CharacterClass::Unknown;
    std::uint32_t     level          = 0;
    std::uint32_t     experience     = 0;
    double            expPercent     = 0.0;   // 0.0..100.0 into current level
    std::uint64_t     expInLevel     = 0;
    std::uint64_t     expForLevel    = 0;     // exp needed to complete the current level
    std::uint32_t     timestamp      = 0;
    bool              hardcore       = false;
    bool              died           = false;
    std::uint8_t      difficulty     = 0;     // 0=Normal, 1=Nightmare, 2=Hell (highest active)
    std::uint8_t      act            = 0;     // 1..5 in the highest active difficulty
    std::uint32_t     mapSeed        = 0;     // char header map-seed, usable with `d2rsave set-seed`
};

// Hellfire Torch side-quest counts. `torchesHellfire` counts the actual
// unique Hellfire Torch (cm2 large charm, uniqueId 400); stackable inputs
// (keys) are summed by stack size, not item count. The three uber-boss
// drops (Diablo's Horn / Mephisto's Brain / Baal's Eye) are the outputs
// from running the ubers; the Uber pane groups them alongside the keys.
struct HellfireTorchQuest {
    std::uint32_t keysTerror       = 0;   // pk1 (stackable)
    std::uint32_t keysHate         = 0;   // pk2 (stackable)
    std::uint32_t keysDestruction  = 0;   // pk3 (stackable)
    std::uint32_t torchesHellfire  = 0;   // unique cm2
    std::uint32_t diablosHorn      = 0;   // dhn (uber drop)
    std::uint32_t mephistosBrain   = 0;   // mbr (uber drop)
    std::uint32_t baalsEye         = 0;   // bey (uber drop)
};

// Colossal Ancients side-quest materials. Codes ua1..ua5 correspond to the
// five statue-like drops referenced by item_names as
// "Talic's Anguish" (ua1), "Korlic's Pain" (ua2), "Madawc's Ire" (ua3),
// "Bul-Kathos' Nightmare" (ua4), "Worusk's End" (ua5). All are stackable.
// `colossalJewels` counts magic-quality Colossal Jewels (base code cjw)
// separately below the bar; the rite's related output.
struct ColossalAncientsQuest {
    std::uint32_t talicAnguish       = 0;   // ua1
    std::uint32_t madawcIre          = 0;   // ua3
    std::uint32_t korlicPain         = 0;   // ua2
    std::uint32_t bulKathosNightmare = 0;   // ua4
    std::uint32_t woruskEnd          = 0;   // ua5
    std::uint32_t colossalJewels     = 0;   // cjw
};

// Terror Zone rotation. Current/next zone names are not derivable from
// save data; v1 leaves them nullopt. Worldstone-shard tallies (stackable)
// come from misc codes xa1..xa5, summed by stack size.
struct TerrorZones {
    std::optional<std::string> currentZone;
    std::optional<std::string> nextZone;
    std::uint32_t shardWestern   = 0;   // xa1
    std::uint32_t shardEastern   = 0;   // xa2
    std::uint32_t shardSouthern  = 0;   // xa3
    std::uint32_t shardDeep      = 0;   // xa4
    std::uint32_t shardNorthern  = 0;   // xa5
};

// One row in the Chronicle table.
enum class ChronicleKind : std::uint8_t { Unique, Set, Runeword };

enum class ChronicleTier : std::uint8_t {
    None = 0, Normal = 1, Exceptional = 2, Elite = 3, Misc = 4
};

struct ChronicleRow {
    ChronicleKind kind        = ChronicleKind::Unique;
    std::uint32_t id          = 0;
    std::string   displayName;   // e.g. "Windforce" or "Sander's Superstition"
    std::string   baseName;      // base item type (e.g. "Hydra Bow")
    ChronicleTier tier         = ChronicleTier::None;
    // `discovered` is true when the account's chronicle records this item
    // (directly or via a sunder-charm sibling id). `location` is set when
    // an owned copy currently exists in inventory or stash.
    bool          discovered   = false;
    std::string   location;
    std::string   setName;       // populated for Set rows (parent set)
    // Base item code (3-char, e.g. "crs" for Crystal Sword). Populated
    // for Unique rows; empty for Sets/Runewords.
    std::string   baseCode;
    // Family key = the code of this base's Normal-tier sibling
    // (armor/weapons `normcode`). Every unique in the Crystal Sword /
    // Dimensional Blade / Phase Blade family shares the same key,
    // which the Uniques "By Tier" view uses to align tier siblings on
    // the same row. Empty for Misc-tier uniques (rings, amulets,
    // jewels, charms) and for Sets/Runewords.
    std::string   familyKey;
    // Item-class slug of the family's Normal-tier base (armor/weapons
    // `type` column, e.g. "swor", "tors", "abow"). The By-Tier view
    // maps this to a human-readable header (Swords, Body Armor, ...)
    // and groups families under it. Empty for Misc-tier / non-Unique
    // rows.
    std::string   familyType;
    // Base item level (armor/weapons/misc `level` column) of the
    // family's Normal-tier sibling -- the canonical "power tier" for
    // the family. Every member of the Crystal Sword / Dimensional
    // Blade / Phase Blade family reports the same value (Crystal
    // Sword's level, 11). The By-Tier view sorts families within
    // each item-class banner by this value so Body Armor reads
    // Quilted (1) -> Leather (3) -> ... -> Ancient Armor (40). 0
    // for Misc-tier / non-Unique rows.
    int           familyLevel = 0;
};

// One item found in inventory / stash. Used by the global inventory-search
// modal. Compact copy; not a full `Item` clone.
struct InventoryItem {
    std::string      name;         // primary display name
    std::string      baseName;     // base item type (may equal name)
    std::string      location;     // e.g. "Kai.d2s" or "stash tab 3"
    // Base item code (3 chars, e.g. "r15" for Hel Rune, "pk1" for Key
    // of Terror). Used by the Session Loot pane to bucket runes by
    // code without a name-string dance; empty when the item didn't
    // come from a base-code source (defensive; the aggregator always
    // fills this today).
    std::string      code;
    ItemQuality      quality  = ItemQuality::None;
    // Per-instance identity read from item bytes. Populated for magic-
    // and-better items (D2R stores it in the extended block); left at 0
    // for stackables, gems, runes, etc. Used by the Session pane to
    // diff owned items against the start-of-session anchor (the first
    // save of the current-or-just-ended play session; sessions end with
    // an S&E backup).
    std::uint32_t    fingerprint = 0;
    bool             identified  = false;
};

// One entry in the reconcile diff. Emitted for every unique/set that is
// either owned but not discovered OR discovered but not owned (i.e. the
// two flags disagree). `location` is set when the item is currently
// held; `chronicledAtMinutes` is set when the account's chronicle
// recorded it (0 means "no timestamp available").
struct ReconcileEntry {
    ChronicleKind kind         = ChronicleKind::Unique;
    std::uint32_t id           = 0;
    std::string   displayName;
    std::string   baseName;
    std::string   location;
    std::uint32_t chronicledAtMinutes = 0;
    bool          owned        = false;
    bool          discovered   = false;
};

struct DashboardSnapshot {
    ActivePlayer                 activePlayer;
    HellfireTorchQuest           hellfireTorch;
    ColossalAncientsQuest        colossalAncients;
    TerrorZones                  terrorZones;
    std::vector<ChronicleRow>    chronicle;      // uniques + sets (+ runewords when enabled)
    std::vector<InventoryItem>   inventory;      // every parsed item across all sources

    // Reconcile: every unique / set item where `owned` != `discovered`.
    // Sunder-charm sibling ids share their flags so a Renewed variant
    // counts as owning + discovering its Latent pair (and vice versa).
    std::vector<ReconcileEntry>  reconcile;

    // Diagnostic: last-refresh wall-clock (seconds since epoch).
    std::uint64_t                refreshedAtEpoch = 0;

    // Whether the active-player slot was populated at all.
    bool                         hasActivePlayer = false;
};

// Walk `saveDir` for .d2s files and read `stashPath` (may be empty when the
// stash is absent). All exceptions are caught internally and logged to the
// snapshot as an inline diagnostic string; the function itself does not
// throw. Requires `db.loadItemTables()` to have been called.
[[nodiscard]] DashboardSnapshot buildSnapshot(RefDb& db,
                                              const std::filesystem::path& saveDir,
                                              const std::filesystem::path& stashPath);

// ---------------------------------------------------------------------------
// Incremental snapshot pipeline (dashboard hot path).
//
// The one-shot `buildSnapshot` above re-parses every .d2s + the .d2i on
// each call, which is 100-150ms per rebuild against a real save dir --
// painful during autosave bursts (gem-combines fire ~10 events in a few
// seconds). The dashboard instead keeps a persistent `DashboardFileCache`
// and, when inotify reports a change, invalidates only the affected file
// entries. Aggregation into a fresh DashboardSnapshot then costs only
// what changed since the previous frame + the (cheap) chronicle SQL
// pass. RefDb lookup tables (tier map + collectable ids) are memoised
// on the cache so they load once per session, not once per rebuild.
// ---------------------------------------------------------------------------
struct DashboardFileCache {
    struct D2sEntry {
        std::filesystem::file_time_type mtime{};
        std::uintmax_t                  size = 0;
        Character                       character{};
        // Every item this .d2s contributes to snap.inventory. Locations
        // already stamped (base filename, " (merc)", " (corpse)",
        // " (iron golem)").
        std::vector<InventoryItem>      items;
        HellfireTorchQuest              hellfire{};
        ColossalAncientsQuest           colossal{};
        TerrorZones                     terror{};
        // Chronicle-column ownership: only ids present in the collectable
        // catalogs (uniqueitems.spawnable, setitems.disablechronicle).
        // First-seen location per id, preserved via ordered emission at
        // aggregation time.
        std::vector<std::pair<std::uint32_t, std::string>> chronUniqueLocs;
        std::vector<std::pair<std::uint32_t, std::string>> chronSetLocs;
        // Reconcile ownership: unfiltered so off-catalog owns surface too.
        std::vector<std::pair<std::uint32_t, std::string>> ownedUniqueLocs;
        std::vector<std::pair<std::uint32_t, std::string>> ownedSetLocs;
    };
    struct D2iEntry {
        std::filesystem::file_time_type mtime{};
        std::uintmax_t                  size = 0;
        // Stash-tab items (with "stash tab N" locations).
        std::vector<InventoryItem>      items;
        HellfireTorchQuest              hellfire{};
        ColossalAncientsQuest           colossal{};
        TerrorZones                     terror{};
        // Chronicle-tab discovery ids (the account has "collected" these).
        std::unordered_set<std::uint32_t> foundUniqueIds;
        std::unordered_set<std::uint32_t> foundSetIds;
        // Full parsed chronicle (reconcile uses per-entry timestamps).
        ChronicleTab                    chron{};
        // Stash-tab ownership for reconcile.
        std::vector<std::pair<std::uint32_t, std::string>> ownedUniqueLocs;
        std::vector<std::pair<std::uint32_t, std::string>> ownedSetLocs;
    };

    // Character .d2s files, keyed by basename (e.g. "Kai.d2s").
    std::unordered_map<std::string, D2sEntry> d2s;
    // Shared stash (single entry). Present only when the stash file exists.
    std::optional<D2iEntry>                   stash;

    // Memoised RefDb lookup tables. Populated on the first refresh; never
    // invalidated (the reference DB is process-lifetime read-only).
    std::optional<std::unordered_map<std::string, ChronicleTier>> tierByCode;
    // Base code -> Normal-tier sibling code (armor/weapons `normcode`).
    // Consumed by the "By Tier" Uniques view to group tier families.
    // Absent entries (e.g. misc codes) mean "no family".
    std::optional<std::unordered_map<std::string, std::string>>   familyByCode;
    // Base code -> `type` slug (armor/weapons `type` column, e.g.
    // "swor", "tors", "abow"). The "By Tier" Uniques view uses the
    // family key's type to bucket families into item-class sections
    // (Swords, Body Armor, ...). Absent for misc codes.
    std::optional<std::unordered_map<std::string, std::string>>   typeByCode;
    // Base code -> `level` (armor/weapons/misc `level` column). Used
    // by the "By Tier" Uniques view to sort families within each
    // item-class section by base power (Quilted -> ... -> Ancient
    // Armor). Populated for every armor/weapons/misc row (0 means
    // "unranked").
    std::optional<std::unordered_map<std::string, int>>           levelByCode;
    std::optional<std::unordered_set<std::uint32_t>>              collectableUniqueIds;
    std::optional<std::unordered_set<std::uint32_t>>              collectableSetIds;
};

// Populate / refresh the cache from a full directory walk. Invalidates
// missing files (drops them from the map). Idempotent and safe to call
// on an empty or partially-populated cache. Used at dashboard startup
// and on manual `[r]` refresh.
void refreshDashboardCacheFromDirectory(
    RefDb&                          db,
    const std::filesystem::path&    saveDir,
    const std::filesystem::path&    stashPath,
    DashboardFileCache&             cache);

// Targeted refresh for the inotify hot path: re-parse only the named
// files (basename form, e.g. "Kai.d2s"). Files that no longer exist
// are removed from the cache. Files not matching the tracked set
// (non-.d2s that aren't the stash) are silently ignored so callers can
// forward the raw watcher payload without pre-filtering.
void refreshDashboardCacheFromChanges(
    RefDb&                          db,
    const std::filesystem::path&    saveDir,
    const std::filesystem::path&    stashPath,
    DashboardFileCache&             cache,
    std::span<const std::string>    changedBasenames);

// Aggregate the current cache state into a fresh snapshot. Pure w.r.t.
// the cache. Runs the chronicle + reconcile SQL passes internally.
[[nodiscard]] DashboardSnapshot aggregateDashboardSnapshot(
    RefDb&                          db,
    DashboardFileCache&             cache);

// Replace the active-player character portion of `snap` in place with
// data parsed from a raw .d2s byte buffer. Used by the Session pane to
// build a start-of-session anchor from a chosen backup row (in auto
// mode: the first save after the S&E that opened the current session;
// in pinned mode: the user-picked row):
// the shared stash + chronicle + quests carry over from the live
// snapshot, while `activePlayer.*` and every inventory item whose
// `location` starts with `filename` (base + " (merc)" / " (corpse)"
// / " (iron golem)") is replaced with what the .d2s bytes contain.
// Returns true iff parsing succeeded; the snapshot is left untouched
// on failure. Requires `db.loadItemTables()` to have been called.
[[nodiscard]] bool overrideActivePlayerFromBytes(
    DashboardSnapshot&           snap,
    RefDb&                       db,
    std::span<const std::byte>   characterBytes,
    std::string_view             filename);

// Replace the shared-stash items in `snap.inventory` (locations
// starting with "stash tab ") with items parsed from a raw .d2i byte
// buffer. Companion to `overrideActivePlayerFromBytes` -- together
// they build a Session anchor that fully reflects a past moment in
// time. Returns true iff parsing succeeded; snapshot untouched on
// failure. Requires `db.loadItemTables()` to have been called.
[[nodiscard]] bool overrideSharedStashFromBytes(
    DashboardSnapshot&           snap,
    RefDb&                       db,
    std::span<const std::byte>   stashBytes);

// Remove every shared-stash item (location starting with
// "stash tab ") from `snap.inventory`, leaving character-side items
// untouched. Used by the Session anchor when no historical stash
// backup covers the pinned moment -- treating the anchor's stash as
// empty is more honest than silently substituting the current stash
// (which would zero-out the stash side of the diff).
void clearSharedStashInSnapshot(DashboardSnapshot& snap);

// ---------------------------------------------------------------------------
// Session: a play session is a `[startEpoch, endEpoch]` time window with
// pre-computed diff-side state at the start (and optionally at the end
// too, when the user has fixed the end to a specific past moment).
//
// Historically the "anchor" was stored as a full DashboardSnapshot
// (deep-copied from the live snapshot then overlaid with backup bytes).
// That deep copy carries chronicle + reconcile + quests + per-item name
// strings the Session pane never reads, and forces the renderer to
// rebuild an identified-uniques/sets hash set on every frame. Rapid
// autosave bursts (e.g. combining gems fires ~10 .d2s writes in a few
// seconds) made both costs painful.
//
// SessionState keeps only what the renderer actually consumes AND
// pre-computes the identified-item lookup set once, at build time.
// Consecutive rebuilds where the window inputs are unchanged can reuse
// the same shared_ptr without any byte parsing.
// ---------------------------------------------------------------------------
struct SessionItemKey {
    std::uint32_t fingerprint = 0;
    ItemQuality   quality     = ItemQuality::None;
    bool operator==(const SessionItemKey& o) const noexcept {
        return fingerprint == o.fingerprint && quality == o.quality;
    }
};
struct SessionItemKeyHash {
    std::size_t operator()(const SessionItemKey& k) const noexcept {
        return std::hash<std::uint64_t>{}(
            (static_cast<std::uint64_t>(k.fingerprint) << 8) |
             static_cast<std::uint64_t>(k.quality));
    }
};

// SessionState = the diff-relevant slice of a DashboardSnapshot at one
// moment in time (start-of-session, or a user-fixed end-of-session).
struct SessionState {
    bool           hasActivePlayer = false;
    std::string    playerName;
    CharacterClass playerClass     = CharacterClass::Unknown;
    std::uint32_t  level           = 0;
    std::uint64_t  expInLevel      = 0;
    // Identified Unique / Set items observed in the account at this
    // moment. The renderer's diff loop probes contains() per item.
    std::unordered_set<SessionItemKey, SessionItemKeyHash> itemKeys;
    // Rune counts per base code (r01..r33). One entry per rune
    // instance; the Session Loot pane subtracts these from the end
    // side to surface newly-picked-up runes.
    std::unordered_map<std::string, std::uint32_t> runeStacks;
};

// Session = the [start, end] window + pre-computed diff-side state at
// the start. The end side of the diff is always the live snapshot;
// `endEpoch` only bounds what the pane DISPLAYS (start/end timestamps,
// elapsed duration). Auto-end sessions set `endEpoch` to "now"; user-
// fixed end sessions clamp `endEpoch` to a past instant.
//
// `startIsCustom` / `endIsCustom` mirror the singleton
// `AppSession::startIsCustom()` at build time so pane renderers can
// vary their titles without needing to read the singleton themselves.
struct Session {
    std::int64_t startEpoch    = 0;
    std::int64_t endEpoch      = 0;
    bool         startIsCustom = false;
    bool         endIsCustom   = false;
    SessionState startState;
};

// AppSession: the application-wide session singleton. Character-
// agnostic; not persisted across dashboard restarts. Both endpoints
// have an "auto" default and a nullable user override.
//
// Semantics:
//   * `autoStartEpoch` seeds at dashboard-boot wall-clock time. When a
//     D2R launch burst is detected in-flight, the launch callback
//     replaces it with the burst's timestamp.
//   * `autoEndEpoch` tracks the newest backup date across all files;
//     the ftxui layer refreshes it after each `rebuild()`.
//   * `customStartEpoch` / `customEndEpoch` are user-set overrides.
//     nullopt = auto. Sticky: not cleared by later auto-detection.
//   * Invariant: `customEndEpoch.has_value()` implies
//     `customStartEpoch.has_value()`. `clearCustom()` and
//     `setCustomStart(nullopt)` enforce it.
struct AppSession {
    std::optional<std::int64_t> customStartEpoch;
    std::optional<std::int64_t> customEndEpoch;
    std::int64_t                autoStartEpoch = 0;
    std::int64_t                autoEndEpoch   = 0;

    [[nodiscard]] std::int64_t startEpoch() const noexcept {
        return customStartEpoch.value_or(autoStartEpoch);
    }
    [[nodiscard]] std::int64_t endEpoch() const noexcept {
        return customEndEpoch.value_or(autoEndEpoch);
    }
    // True iff the user has fixed the start (either directly or via a
    // now-cleared invariant chain).
    [[nodiscard]] bool startIsCustom() const noexcept {
        return customStartEpoch.has_value();
    }
    [[nodiscard]] bool endIsCustom() const noexcept {
        return customEndEpoch.has_value();
    }
    void clearCustom() noexcept {
        customStartEpoch.reset();
        customEndEpoch.reset();
    }
    // Setting a custom end without a custom start silently clears the
    // end to preserve the invariant.
    void setCustomEnd(std::optional<std::int64_t> v) noexcept {
        customEndEpoch = customStartEpoch.has_value() ? v : std::nullopt;
    }
    // Clearing a custom start also clears the end.
    void setCustomStart(std::optional<std::int64_t> v) noexcept {
        customStartEpoch = v;
        if (!customStartEpoch.has_value()) customEndEpoch.reset();
    }
};

// Extract a SessionState from a fully-populated DashboardSnapshot.
// Used by the ftxui layer after overlaying historical bytes onto a
// working DashboardSnapshot (for the start side and, when the user
// fixed the end, for the end side too).
[[nodiscard]] SessionState makeSessionStateFromSnapshot(
    const DashboardSnapshot& snap);

// Experience needed to reach `level` from a fresh character. Levels
// outside [1..99] clamp to the boundary. Values are the standard D2/D2R
// experience table (public knowledge, matches game/D2R experience.txt).
[[nodiscard]] std::uint64_t experienceToReachLevel(std::uint32_t level) noexcept;

// ---------------------------------------------------------------------------
// Run: the interval between two adjacent SaveAndExit backups for one
// character's `.d2s`. The community-aligned term for what the game
// calls "Save & Exit" is "run end", so a Run's `endEpoch` is the date
// of its closing SaveAndExit backup and its `startEpoch` is the date
// of the previous SaveAndExit (or the caller-supplied `sessionStart`
// when the previous SaveAndExit lies outside the session window).
// A Run with no closing SaveAndExit on record is `inProgress = true`
// -- the user is either mid-play or the dashboard's DB was rotated
// before the run ended.
//
// Runs are strictly per-file. The Session concept
// (`AppSession` in the ftxui layer) is character-agnostic and only
// bounds `sessionStart`; a Session collects whichever Runs from
// whichever files happen to close inside its window.
// ---------------------------------------------------------------------------
struct Run {
    std::string   characterFile;    // e.g. "Kai.d2s"
    // Left bound: previous SaveAndExit's date OR the session start
    // (whichever is later). Never earlier than `sessionStart`.
    std::int64_t  startEpoch  = 0;
    // Right bound: this run's SaveAndExit date. 0 when `inProgress`.
    std::int64_t  endEpoch    = 0;
    bool          inProgress  = false;
    std::int32_t  autosaveCount = 0;
    // Dates of the Autosave / Other rows inside the run, chronological
    // oldest-first. Phase C (Backups pane run-collapse) uses these to
    // key an on-demand fetch of the full HistoryRow bytes for the
    // expand-view; keeping only dates here keeps Run cheap.
    std::vector<std::int64_t> autosaveDates;
};

// Group `historyRows` (as returned by BackupDb::historyFor, i.e. newest-
// first) into Runs for a single file.
//
// Rows with `date < sessionStart` are skipped (session-clip). Passing
// `sessionStart = 0` disables clipping (used by the Backups pane which
// wants the file's full history).
//
// Algorithm: walk chronologically OLDEST-first (i.e. reverse of the
// input) so a Run naturally accumulates its autosaves before its
// closing SaveAndExit lands. Returned Runs are in chronological
// order, oldest-first; the renderer reverses the vector for a
// newest-first display.
[[nodiscard]] std::vector<Run> groupRunsForFile(
    std::string_view                            characterFile,
    std::span<const BackupDb::HistoryRow>       historyRows,
    std::int64_t                                sessionStart);

} // namespace d2r
