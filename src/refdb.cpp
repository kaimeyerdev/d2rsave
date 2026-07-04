#include "d2r/RefDb.hpp"
#include "d2r/SunderCharms.hpp"

#include <sqlite3.h>

#include <charconv>
#include <cstdlib>
#include <fmt/format.h>
#include <utility>

namespace d2r {

// ---------- Statement --------------------------------------------------------

Statement::Statement(sqlite3* db, std::string_view sql) {
    if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr) != SQLITE_OK) {
        throw DbError(fmt::format("prepare failed: {}", sqlite3_errmsg(db)));
    }
}

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept : stmt_(other.stmt_) {
    other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_) sqlite3_finalize(stmt_);
        stmt_ = other.stmt_;
        other.stmt_ = nullptr;
    }
    return *this;
}

void Statement::bind(int index, int value) {
    if (sqlite3_bind_int(stmt_, index, value) != SQLITE_OK) {
        throw DbError("bind int failed");
    }
}
void Statement::bind(int index, std::int64_t value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) {
        throw DbError("bind int64 failed");
    }
}
void Statement::bind(int index, std::string_view text) {
    if (sqlite3_bind_text(stmt_, index, text.data(),
                          static_cast<int>(text.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        throw DbError("bind text failed");
    }
}
void Statement::bindNull(int index) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) {
        throw DbError("bind null failed");
    }
}
void Statement::reset() { sqlite3_reset(stmt_); sqlite3_clear_bindings(stmt_); }

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW)  return true;
    if (rc == SQLITE_DONE) return false;
    throw DbError(fmt::format("step failed: rc={}", rc));
}

std::int64_t Statement::columnInt64(int col) const {
    return sqlite3_column_int64(stmt_, col);
}

std::string Statement::columnText(int col) const {
    const auto* p   = sqlite3_column_text(stmt_, col);
    const auto  len = sqlite3_column_bytes(stmt_, col);
    if (!p) return {};
    return std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(len));
}

bool Statement::columnIsNull(int col) const {
    return sqlite3_column_type(stmt_, col) == SQLITE_NULL;
}

// ---------- RefDb ------------------------------------------------------------

RefDb::RefDb(const std::filesystem::path& dbPath) {
    if (sqlite3_open_v2(dbPath.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        const std::string msg = db_ ? sqlite3_errmsg(db_) : "unknown error";
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        throw DbError(fmt::format("open {}: {}", dbPath.string(), msg));
    }
}

RefDb::~RefDb() {
    if (db_) sqlite3_close(db_);
}

RefDb::RefDb(RefDb&& other) noexcept : db_(other.db_) { other.db_ = nullptr; }

RefDb& RefDb::operator=(RefDb&& other) noexcept {
    if (this != &other) {
        if (db_) sqlite3_close(db_);
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

std::int64_t RefDb::countRows(std::string_view table) {
    // Table name comes from trusted internal code, not user input; no injection risk.
    auto stmt = prepare(fmt::format("SELECT COUNT(*) FROM {}", table));
    if (!stmt.step()) throw DbError("COUNT returned no rows");
    return stmt.columnInt64(0);
}

Statement RefDb::prepare(std::string_view sql) {
    return Statement(db_, sql);
}

// ---------- Item-table caches ------------------------------------------------

namespace {

std::int32_t parseInt(std::string_view s, std::int32_t fallback = 0) noexcept {
    if (s.empty()) return fallback;
    std::int32_t out = 0;
    const char* first = s.data();
    const char* last  = s.data() + s.size();
    const auto rc = std::from_chars(first, last, out);
    return rc.ec == std::errc{} ? out : fallback;
}

bool truthy(std::string_view s) noexcept {
    return !s.empty() && s != "0";
}

} // namespace

void RefDb::loadItemTables() {
    if (itemTablesLoaded_) return;

    // item_names overlay (optional). Maps a mod-file 'index' key (e.g.
    // "Iros Torch") to the actual D2R display name (e.g. "Torch of Iro").
    // The table is only present when txt2sql was run with --names-json.
    std::unordered_map<std::string, std::string> nameOverlay;
    {
        auto st = prepare(
            "SELECT name FROM sqlite_master "
            "WHERE type='table' AND name='item_names'");
        if (st.step()) {
            auto load = prepare("SELECT \"key\", en_us FROM item_names "
                                "WHERE \"key\" != '' AND en_us IS NOT NULL");
            while (load.step()) {
                nameOverlay.emplace(load.columnText(0), load.columnText(1));
            }
        }
    }
    auto applyOverlay = [&](std::string& index) {
        auto it = nameOverlay.find(index);
        if (it != nameOverlay.end() && !it->second.empty()) {
            index = it->second;
        }
    };

    // armor
    {
        auto st = prepare(
            "SELECT code, name, type, type2, reqstr, reqdex, levelreq, "
            "invwidth, invheight, questdiffcheck FROM armor "
            "WHERE code IS NOT NULL AND code != ''");
        while (st.step()) {
            ArmorRow r;
            const auto code = st.columnText(0);
            r.name      = st.columnText(1);
            r.type      = st.columnText(2);
            r.type2     = st.columnText(3);
            r.reqStr    = parseInt(st.columnText(4));
            r.reqDex    = parseInt(st.columnText(5));
            r.reqLvl    = parseInt(st.columnText(6));
            r.invWidth  = parseInt(st.columnText(7), 1);
            r.invHeight = parseInt(st.columnText(8), 1);
            r.questDiffCheck = truthy(st.columnText(9));
            armorByCode_.emplace(code, std::move(r));
        }
    }

    // weapons
    {
        auto st = prepare(
            "SELECT code, name, type, type2, reqstr, reqdex, levelreq, "
            "invwidth, invheight, stackable, maxstack, col_2handed, "
            "questdiffcheck FROM weapons "
            "WHERE code IS NOT NULL AND code != ''");
        while (st.step()) {
            WeaponRow r;
            const auto code = st.columnText(0);
            r.name      = st.columnText(1);
            r.type      = st.columnText(2);
            r.type2     = st.columnText(3);
            r.reqStr    = parseInt(st.columnText(4));
            r.reqDex    = parseInt(st.columnText(5));
            r.reqLvl    = parseInt(st.columnText(6));
            r.invWidth  = parseInt(st.columnText(7), 1);
            r.invHeight = parseInt(st.columnText(8), 1);
            const bool stackable = truthy(st.columnText(9));
            r.maxStack  = stackable ? parseInt(st.columnText(10)) : 0;
            r.twoHanded = truthy(st.columnText(11));
            r.thrown    = false; // not consumed by parser flow
            r.questDiffCheck = truthy(st.columnText(12));
            weaponByCode_.emplace(code, std::move(r));
        }
    }

    // misc
    {
        auto st = prepare(
            "SELECT code, name, type, type2, reqstr, reqdex, levelreq, "
            "invwidth, invheight, stackable, maxstack, questdiffcheck, "
            "advancedstashstackable FROM misc "
            "WHERE code IS NOT NULL AND code != ''");
        while (st.step()) {
            MiscRow r;
            const auto code = st.columnText(0);
            r.name      = st.columnText(1);
            r.type      = st.columnText(2);
            r.type2     = st.columnText(3);
            r.reqStr    = parseInt(st.columnText(4));
            r.reqDex    = parseInt(st.columnText(5));
            r.reqLvl    = parseInt(st.columnText(6));
            r.invWidth  = parseInt(st.columnText(7), 1);
            r.invHeight = parseInt(st.columnText(8), 1);
            const bool stackable = truthy(st.columnText(9));
            r.maxStack  = stackable ? parseInt(st.columnText(10)) : 0;
            r.questDiffCheck = truthy(st.columnText(11));
            r.advancedStashStackable = truthy(st.columnText(12));
            miscByCode_.emplace(code, std::move(r));
        }
    }

    // itemstatcost
    {
        auto st = prepare(
            "SELECT id, stat, save_bits, save_add, save_param_bits "
            "FROM itemstatcost WHERE id IS NOT NULL AND id != ''");
        while (st.step()) {
            const auto id = static_cast<std::uint16_t>(parseInt(st.columnText(0)));
            StatCostRow r;
            r.stat          = st.columnText(1);
            r.saveBits      = parseInt(st.columnText(2));
            r.saveAdd       = parseInt(st.columnText(3));
            r.saveParamBits = st.columnIsNull(4) ? -1 : parseInt(st.columnText(4), -1);
            // Java normalises saveAdd == -1 to 0 (see ItemStatCost.java).
            if (r.saveAdd == -1) r.saveAdd = 0;
            statCostById_.emplace(id, std::move(r));
        }
    }

    // uniqueitems
    {
        auto st = prepare(
            "SELECT id, \"index\", code, lvl, lvl_req FROM uniqueitems "
            "WHERE id IS NOT NULL AND id != ''");
        while (st.step()) {
            const auto id = static_cast<std::uint16_t>(parseInt(st.columnText(0)));
            UniqueItemRow r;
            r.index  = st.columnText(1);
            r.code   = st.columnText(2);
            r.lvl    = parseInt(st.columnText(3));
            r.lvlReq = parseInt(st.columnText(4));
            // Preferred name source: D2R string-table export (item-names.json).
            // "Iros Torch" -> "Torch of Iro", etc.
            applyOverlay(r.index);
            uniqueById_.emplace(id, std::move(r));
        }
    }

    // setitems
    {
        auto st = prepare(
            "SELECT id, \"index\", \"set\", item FROM setitems "
            "WHERE id IS NOT NULL AND id != ''");
        while (st.step()) {
            const auto id = static_cast<std::uint16_t>(parseInt(st.columnText(0)));
            SetItemRow r;
            r.index   = st.columnText(1);
            r.setName = st.columnText(2);
            r.code    = st.columnText(3);
            applyOverlay(r.index);
            applyOverlay(r.setName);
            setItemById_.emplace(id, std::move(r));
        }
    }

    itemTablesLoaded_ = true;
}

const ArmorRow* RefDb::lookupArmor(std::string_view code) const noexcept {
    auto it = armorByCode_.find(std::string(code));
    return it == armorByCode_.end() ? nullptr : &it->second;
}
const WeaponRow* RefDb::lookupWeapon(std::string_view code) const noexcept {
    auto it = weaponByCode_.find(std::string(code));
    return it == weaponByCode_.end() ? nullptr : &it->second;
}
const MiscRow* RefDb::lookupMisc(std::string_view code) const noexcept {
    auto it = miscByCode_.find(std::string(code));
    return it == miscByCode_.end() ? nullptr : &it->second;
}
const StatCostRow* RefDb::lookupStatCost(std::uint16_t id) const noexcept {
    auto it = statCostById_.find(id);
    return it == statCostById_.end() ? nullptr : &it->second;
}
const UniqueItemRow* RefDb::lookupUnique(std::uint16_t id) const noexcept {
    auto it = uniqueById_.find(id);
    return it == uniqueById_.end() ? nullptr : &it->second;
}
const SetItemRow* RefDb::lookupSetItem(std::uint16_t id) const noexcept {
    auto it = setItemById_.find(id);
    return it == setItemById_.end() ? nullptr : &it->second;
}

// ---------- findReferenceDb -------------------------------------------------

std::optional<std::filesystem::path> findReferenceDb(
    const std::filesystem::path& exePath, std::string_view override) {
    namespace fs = std::filesystem;
    auto exists = [](const fs::path& p) {
        std::error_code ec;
        return fs::exists(p, ec) && !fs::is_directory(p, ec);
    };

    if (!override.empty()) {
        fs::path p{override};
        return exists(p) ? std::optional<fs::path>(p) : std::nullopt;
    }
    if (const char* env = std::getenv("D2R_REFERENCE_DB")) {
        fs::path p{env};
        if (exists(p)) return p;
    }
    if (!exePath.empty()) {
        const auto sibling = exePath.parent_path() / "reference.sqlite";
        if (exists(sibling)) return sibling;
    }
#ifdef D2R_REFERENCE_DB_DEFAULT
    {
        fs::path p{D2R_REFERENCE_DB_DEFAULT};
        if (exists(p)) return p;
    }
#endif
    return std::nullopt;
}

} // namespace d2r
