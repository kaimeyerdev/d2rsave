#include "d2r/SqlEmitter.hpp"

#include <cctype>
#include <sstream>

namespace d2r {

namespace {

// Normalise a raw excel column header to the on-disk SQL convention:
//   * strip a leading '*',
//   * replace ' ', '.', '-', '/' with '_',
//   * lowercase all letters,
//   * prefix with "col_" if the result starts with a digit.
// This preserves byte-compat with the hand-committed data/sql/*.sql files.
std::string normaliseColumn(std::string_view raw) {
    std::string out;
    out.reserve(raw.size() + 4);
    std::size_t start = 0;
    while (start < raw.size() && raw[start] == '*') {
        ++start;
    }
    for (std::size_t i = start; i < raw.size(); ++i) {
        const char c = raw[i];
        switch (c) {
            case ' ':
            case '.':
            case '-':
            case '/':
                out.push_back('_');
                break;
            default:
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                break;
        }
    }
    if (!out.empty() && std::isdigit(static_cast<unsigned char>(out.front()))) {
        out.insert(0, "col_");
    }
    return out;
}

void writeSqlString(std::ostream& out, std::string_view value) {
    out.put('\'');
    for (char c : value) {
        out.put(c);
        if (c == '\'') {
            out.put('\'');
        }
    }
    out.put('\'');
}

void writeCellForInsert(std::ostream& out, std::string_view value) {
    if (value.empty()) {
        out << "NULL";
    } else {
        writeSqlString(out, value);
    }
}

void writeCreateTable(std::ostream& out,
                      std::string_view tableName,
                      const std::vector<std::string>& columns) {
    out << "DROP TABLE IF EXISTS " << tableName << ";\n";
    out << "CREATE TABLE " << tableName << " (\n";
    for (std::size_t i = 0; i < columns.size(); ++i) {
        out << "    \"" << normaliseColumn(columns[i]) << "\" TEXT";
        if (i + 1 < columns.size()) {
            out.put(',');
        }
        out.put('\n');
    }
    out << ");\n";
}

} // namespace

std::string excelAttribution(std::string_view sourceFile) {
    std::ostringstream ss;
    ss << "-- Diablo II and Diablo II: Resurrected are trademarks of Blizzard\n"
       << "-- Entertainment. This file contains structured data derived from D2\n"
       << "-- modding text tables (" << sourceFile
       << ") that themselves originate from Blizzard\n"
       << "-- game data. Distributed here with no warranty; you may not have\n"
       << "-- permission to use this file. Consult your local copyright law and/or\n"
       << "-- use at your own risk. See d2rsavegameparser/LICENSE for the upstream\n"
       << "-- notice.\n"
       << "--\n"
       << "-- Committed as the canonical seed for the reference SQLite DB;\n"
       << "-- edit this file directly or replace it from a fresh CASC / mod-\n"
       << "-- txt extract if you need to update the data.";
    return ss.str();
}

std::string jsonAttribution(std::string_view sourceFile) {
    std::ostringstream ss;
    ss << "-- Diablo II and Diablo II: Resurrected are trademarks of Blizzard\n"
       << "-- Entertainment. This file contains D2R display strings extracted\n"
       << "-- from the game's CASC storage (data/local/lng/strings/" << sourceFile
       << ")\n"
       << "-- via CascLib. Distributed here with no warranty; you may not have\n"
       << "-- permission to use this file. Consult your local copyright law and/or\n"
       << "-- use at your own risk. See d2rsavegameparser-examples/src/main/\n"
       << "-- resources/COPYRIGHT for the upstream notice from the paladijn examples\n"
       << "-- project.\n"
       << "--\n"
       << "-- Committed as the canonical seed for the reference SQLite DB;\n"
       << "-- regenerate with `cmake --build --target regen-refdb-sql` against a\n"
       << "-- fresh D2R install if the upstream data changes.";
    return ss.str();
}

void writeExcelSql(std::ostream& out,
                   std::string_view tableName,
                   const ExcelTable& table,
                   std::string_view attribution) {
    if (!attribution.empty()) {
        out << attribution << "\n\n";
    }

    writeCreateTable(out, tableName, table.columns);
    out << "\nBEGIN;\n";

    for (const auto& row : table.rows) {
        out << "INSERT INTO " << tableName << " VALUES (";
        for (std::size_t i = 0; i < row.size(); ++i) {
            writeCellForInsert(out, row[i]);
            if (i + 1 < row.size()) {
                out << ", ";
            }
        }
        out << ");\n";
    }

    out << "COMMIT;\n";
}

const std::vector<std::string>& stringTableLocaleColumns() {
    static const std::vector<std::string> cols = {
        "en_us", "de_de", "es_es", "es_mx", "fr_fr", "it_it", "ja_jp",
        "ko_kr", "pl_pl", "pt_br", "ru_ru", "zh_cn", "zh_tw"
    };
    return cols;
}

void writeStringTableSql(std::ostream& out,
                         std::string_view tableName,
                         const std::vector<StringTableRow>& rows,
                         std::string_view attribution) {
    if (!attribution.empty()) {
        out << attribution << "\n\n";
    }

    const auto& locales = stringTableLocaleColumns();

    out << "DROP TABLE IF EXISTS " << tableName << ";\n";
    out << "CREATE TABLE " << tableName << " (\n";
    out << "    \"key\" TEXT PRIMARY KEY";
    for (const auto& col : locales) {
        out << ",\n    \"" << col << "\" TEXT";
    }
    out << "\n);\n";

    out << "\nBEGIN;\n";
    for (const auto& row : rows) {
        out << "INSERT OR REPLACE INTO " << tableName << " VALUES (";
        writeCellForInsert(out, row.key);
        for (std::size_t i = 0; i < locales.size(); ++i) {
            out << ", ";
            const std::string* v = i < row.locales.size() ? &row.locales[i] : nullptr;
            writeCellForInsert(out, v ? std::string_view(*v) : std::string_view());
        }
        out << ");\n";
    }
    out << "COMMIT;\n";
}

} // namespace d2r
