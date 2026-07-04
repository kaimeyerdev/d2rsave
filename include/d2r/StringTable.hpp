#pragma once

// Parser for D2R JSON string tables (item-names.json, item-runes.json, ...).
// The JSON is a top-level array of objects like:
//   { "id": 1060, "Key": "qf1",
//     "enUS": "Khalim's Flail", "deDE": "...", "esES": "...", "esMX": "...",
//     "frFR": "...", "itIT": "...", "jaJP": "...", "koKR": "...",
//     "plPL": "...", "ptBR": "...", "ruRU": "...", "zhCN": "...", "zhTW": "..." }
// The parser flattens each object into a StringTableRow keyed by "Key", with
// the 13 locale strings in the fixed column order returned by
// stringTableLocaleColumns().

#include "d2r/SqlEmitter.hpp"

#include <string_view>
#include <vector>

namespace d2r {

std::vector<StringTableRow> parseStringTable(std::string_view json);

} // namespace d2r
