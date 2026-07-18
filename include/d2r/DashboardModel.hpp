// Data model for the interactive TUI dashboard. `buildSnapshot` walks the
// save directory + shared stash once and returns a plain-data snapshot for
// the FTXUI layer to render. UI-free; safe to unit-test without ftxui.
//
// Only compiled when D2R_HAVE_SQLITE is defined (all data-source code
// already requires the reference DB).

#pragma once

#include "d2r/Character.hpp"
#include "d2r/Item.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
// (keys) are summed by stack size, not item count.
struct HellfireTorchQuest {
    std::uint32_t keysTerror       = 0;   // pk1 (stackable)
    std::uint32_t keysHate         = 0;   // pk2 (stackable)
    std::uint32_t keysDestruction  = 0;   // pk3 (stackable)
    std::uint32_t torchesHellfire  = 0;   // unique cm2
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
};

// One item found in inventory / stash. Used by the global inventory-search
// modal. Compact copy; not a full `Item` clone.
struct InventoryItem {
    std::string      name;         // primary display name
    std::string      baseName;     // base item type (may equal name)
    std::string      location;     // e.g. "Kai.d2s" or "stash tab 3"
    ItemQuality      quality  = ItemQuality::None;
    // Per-instance identity read from item bytes. Populated for magic-
    // and-better items (D2R stores it in the extended block); left at 0
    // for stackables, gems, runes, etc. Used by the Session pane to
    // diff owned items against a start-of-session anchor.
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

// Replace the active-player character portion of `snap` in place with
// data parsed from a raw .d2s byte buffer. Used by the Session pane to
// build a "start of session" anchor from the latest SaveAndExit backup:
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

// Experience needed to reach `level` from a fresh character. Levels
// outside [1..99] clamp to the boundary. Values are the standard D2/D2R
// experience table (public knowledge, matches game/D2R experience.txt).
[[nodiscard]] std::uint64_t experienceToReachLevel(std::uint32_t level) noexcept;

} // namespace d2r
