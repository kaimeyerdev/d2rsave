#pragma once

// SQL emitter matching the on-disk layout of data/sql/*.sql byte-for-byte.
// The whole point of this module is that regenerating a table against the same
// input produces a diff that is either empty or explicable (e.g. row-count
// change from an upstream data refresh).

#include "d2r/ExcelTable.hpp"

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace d2r {

// Emit a full `DROP / CREATE / BEGIN / INSERTs / COMMIT` block for an excel
// table. `attribution` is prepended verbatim followed by a blank line; pass
// an empty string to suppress it.
void writeExcelSql(std::ostream& out,
                   std::string_view tableName,
                   const ExcelTable& table,
                   std::string_view attribution);

// Attribution comment for tables sourced from a D2 excel .txt table (armor,
// weapons, misc, ...). `sourceFile` is embedded as "(<name>.txt)".
std::string excelAttribution(std::string_view sourceFile);

// Attribution comment for tables sourced from a D2R CASC JSON string table
// (item-names, item-runes, ...). `sourceFile` names the CASC path relative to
// data/local/lng/strings/.
std::string jsonAttribution(std::string_view sourceFile);

// Localised D2R string-table row: one key + one value per locale. The locale
// column order is fixed at 13 entries in the order matching the CREATE TABLE
// in writeStringTableSql().
struct StringTableRow {
    std::string key;
    // en_us, de_de, es_es, es_mx, fr_fr, it_it, ja_jp, ko_kr, pl_pl, pt_br,
    // ru_ru, zh_cn, zh_tw
    std::vector<std::string> locales;
};

// The 13 D2R locale column names in the wide-schema order.
const std::vector<std::string>& stringTableLocaleColumns();

// Emit a full `DROP / CREATE / BEGIN / INSERTs / COMMIT` block for a locale-
// wide string table. Rows with duplicate keys are handled by `INSERT OR
// REPLACE` so the last row wins (matches existing 16_item_names.sql).
void writeStringTableSql(std::ostream& out,
                         std::string_view tableName,
                         const std::vector<StringTableRow>& rows,
                         std::string_view attribution);

} // namespace d2r
