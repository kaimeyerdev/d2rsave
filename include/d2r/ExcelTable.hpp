#pragma once

// Minimal reader for D2R excel-style text tables (tab-separated, first row is
// column headers). Values are kept as strings; empty cells stay as empty
// strings. Callers decide how to render them (SqlEmitter treats "" as NULL).

#include <string>
#include <string_view>
#include <vector>

namespace d2r {

struct ExcelTable {
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
};

// Parse the contents of an excel .txt file.
//
// Behaviour:
//   * First non-empty line is treated as the header row.
//   * Both LF and CRLF line endings are accepted.
//   * A trailing final newline is ignored.
//   * Rows whose first column equals "Expansion" are dropped (they mark the
//     classic/expansion boundary in D2's data and are not real records).
//   * Rows shorter than the header are padded with empty strings; rows longer
//     are truncated to the header length.
ExcelTable parseExcelTable(std::string_view text);

} // namespace d2r
