// Thin RAII wrapper over sqlite3 for the D2R reference database.
//
// The reference DB is a build artefact produced by concatenating
// data/sql/*.sql and feeding the result to `sqlite3`. See CMakeLists.txt.
// This wrapper is only compiled when D2R_HAVE_SQLITE is defined.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

struct sqlite3;
struct sqlite3_stmt;

namespace d2r {

class DbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Statement {
public:
    Statement() = default;
    Statement(sqlite3* db, std::string_view sql);
    ~Statement();

    Statement(const Statement&)            = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    void bind(int index, int value);
    void bind(int index, std::int64_t value);
    void bind(int index, std::string_view text);
    void bindNull(int index);
    void reset();

    // Advance to the next row. Returns true if a row is available.
    bool step();

    // Column accessors (0-indexed). Use only after step() returned true.
    [[nodiscard]] std::int64_t columnInt64(int col) const;
    [[nodiscard]] std::string  columnText(int col) const;
    [[nodiscard]] bool         columnIsNull(int col) const;

    [[nodiscard]] sqlite3_stmt* raw() const noexcept { return stmt_; }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

// Row shapes for the ItemParser lookups.
struct ArmorRow {
    std::string name;
    std::string type;
    std::string type2;
    std::int32_t reqStr = 0;
    std::int32_t reqDex = 0;
    std::int32_t reqLvl = 0;
    std::int32_t invWidth = 1;
    std::int32_t invHeight = 1;
    bool questDiffCheck = false;
};

struct WeaponRow {
    std::string name;
    std::string type;
    std::string type2;
    std::int32_t reqStr = 0;
    std::int32_t reqDex = 0;
    std::int32_t reqLvl = 0;
    std::int32_t invWidth = 1;
    std::int32_t invHeight = 1;
    std::int32_t maxStack = 0;   // 0 = not stackable
    bool twoHanded = false;
    bool thrown = false;
    bool questDiffCheck = false;
};

struct MiscRow {
    std::string name;
    std::string type;
    std::string type2;
    std::int32_t reqStr = 0;
    std::int32_t reqDex = 0;
    std::int32_t reqLvl = 0;
    std::int32_t invWidth = 1;
    std::int32_t invHeight = 1;
    std::int32_t maxStack = 0;
    bool advancedStashStackable = false;
    bool questDiffCheck = false;
};

struct StatCostRow {
    std::int32_t saveBits      = 0;
    std::int32_t saveAdd       = 0;
    std::int32_t saveParamBits = -1;
    std::string  stat;
};

struct UniqueItemRow {
    std::string index;   // display name (e.g. "Windforce")
    std::string code;    // base item code
    std::int32_t lvl = 0;
    std::int32_t lvlReq = 0;
};

struct SetItemRow {
    std::string index;   // display name (e.g. "Sander's Superstition")
    std::string setName; // parent set (e.g. "Sander's Folly")
    std::string code;    // base item code
};

class RefDb {
public:
    // Open the DB read-only. Throws DbError on failure.
    explicit RefDb(const std::filesystem::path& dbPath);
    ~RefDb();

    RefDb(const RefDb&)            = delete;
    RefDb& operator=(const RefDb&) = delete;
    RefDb(RefDb&& other) noexcept;
    RefDb& operator=(RefDb&& other) noexcept;

    // Convenience: SELECT COUNT(*) FROM <table>. Throws if table missing.
    [[nodiscard]] std::int64_t countRows(std::string_view table);

    // Prepare a fresh statement. Caller owns the returned Statement.
    [[nodiscard]] Statement prepare(std::string_view sql);

    // Populate the item-parser lookup caches (idempotent). Call before any of
    // the lookup*(...) methods below.
    void loadItemTables();

    // Look up rows for the item parser. Returns nullptr when the key is missing.
    [[nodiscard]] const ArmorRow*      lookupArmor(std::string_view code)  const noexcept;
    [[nodiscard]] const WeaponRow*     lookupWeapon(std::string_view code) const noexcept;
    [[nodiscard]] const MiscRow*       lookupMisc(std::string_view code)   const noexcept;
    [[nodiscard]] const StatCostRow*   lookupStatCost(std::uint16_t id)    const noexcept;
    [[nodiscard]] const UniqueItemRow* lookupUnique(std::uint16_t id)      const noexcept;
    [[nodiscard]] const SetItemRow*    lookupSetItem(std::uint16_t id)     const noexcept;

    [[nodiscard]] sqlite3* raw() const noexcept { return db_; }

private:
    sqlite3* db_ = nullptr;

    bool itemTablesLoaded_ = false;
    std::unordered_map<std::string, ArmorRow>       armorByCode_;
    std::unordered_map<std::string, WeaponRow>      weaponByCode_;
    std::unordered_map<std::string, MiscRow>        miscByCode_;
    std::unordered_map<std::uint16_t, StatCostRow>  statCostById_;
    std::unordered_map<std::uint16_t, UniqueItemRow> uniqueById_;
    std::unordered_map<std::uint16_t, SetItemRow>   setItemById_;
};

// Locate the reference DB by:
//   1. explicit override
//   2. $D2R_REFERENCE_DB env var
//   3. <exeDir>/reference.sqlite
//   4. compile-time default (D2R_REFERENCE_DB_DEFAULT)
[[nodiscard]] std::optional<std::filesystem::path> findReferenceDb(
    const std::filesystem::path& exePath,
    std::string_view override = {});

} // namespace d2r
