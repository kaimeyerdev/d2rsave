#include "d2r/StringTable.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <stdexcept>

namespace d2r {

std::vector<StringTableRow> parseStringTable(std::string_view json) {
    // Keys in the JSON, matching stringTableLocaleColumns() 1:1.
    static const std::array<const char*, 13> kJsonLocaleKeys = {
        "enUS", "deDE", "esES", "esMX", "frFR", "itIT", "jaJP",
        "koKR", "plPL", "ptBR", "ruRU", "zhCN", "zhTW"
    };

    const auto parsed = nlohmann::json::parse(json);
    if (!parsed.is_array()) {
        throw std::runtime_error("string table JSON root must be an array");
    }

    std::vector<StringTableRow> rows;
    rows.reserve(parsed.size());

    for (const auto& entry : parsed) {
        if (!entry.is_object()) {
            continue;
        }
        const auto keyIt = entry.find("Key");
        if (keyIt == entry.end() || !keyIt->is_string()) {
            continue;
        }

        StringTableRow row;
        row.key = keyIt->get<std::string>();
        row.locales.reserve(kJsonLocaleKeys.size());
        for (const char* jsonKey : kJsonLocaleKeys) {
            const auto it = entry.find(jsonKey);
            if (it != entry.end() && it->is_string()) {
                row.locales.emplace_back(it->get<std::string>());
            } else {
                row.locales.emplace_back();
            }
        }
        rows.push_back(std::move(row));
    }

    return rows;
}

} // namespace d2r
