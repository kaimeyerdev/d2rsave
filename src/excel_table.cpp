#include "d2r/ExcelTable.hpp"

namespace d2r {

namespace {

std::vector<std::string> splitTabs(std::string_view line) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\t') {
            out.emplace_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    out.emplace_back(line.substr(start));
    return out;
}

} // namespace

ExcelTable parseExcelTable(std::string_view text) {
    ExcelTable table;

    bool haveHeader = false;
    std::size_t lineStart = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const bool eol = (i == text.size()) || (text[i] == '\n');
        if (!eol) {
            continue;
        }
        std::size_t lineEnd = i;
        if (lineEnd > lineStart && text[lineEnd - 1] == '\r') {
            --lineEnd;
        }
        std::string_view line = text.substr(lineStart, lineEnd - lineStart);
        lineStart = i + 1;

        if (line.empty()) {
            continue;
        }

        auto cells = splitTabs(line);

        if (!haveHeader) {
            table.columns = std::move(cells);
            haveHeader = true;
            continue;
        }

        if (!cells.empty() && cells.front() == "Expansion") {
            continue;
        }

        if (cells.size() < table.columns.size()) {
            cells.resize(table.columns.size());
        } else if (cells.size() > table.columns.size()) {
            cells.resize(table.columns.size());
        }
        table.rows.push_back(std::move(cells));
    }

    return table;
}

} // namespace d2r
