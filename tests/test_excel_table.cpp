// Unit tests for the excel .txt parser.

#include "d2r/ExcelTable.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("parseExcelTable reads a minimal table", "[excel]") {
    std::string_view input =
        "name\tcode\treqstr\n"
        "Cap\tcap\t0\n"
        "Skull Cap\tskp\t15\n";
    const auto t = d2r::parseExcelTable(input);
    REQUIRE(t.columns == std::vector<std::string>{"name", "code", "reqstr"});
    REQUIRE(t.rows.size() == 2);
    REQUIRE(t.rows[0] == std::vector<std::string>{"Cap", "cap", "0"});
    REQUIRE(t.rows[1] == std::vector<std::string>{"Skull Cap", "skp", "15"});
}

TEST_CASE("parseExcelTable tolerates CRLF and trailing newline", "[excel]") {
    std::string_view input =
        "a\tb\r\n"
        "1\t2\r\n"
        "3\t4\r\n";
    const auto t = d2r::parseExcelTable(input);
    REQUIRE(t.columns == std::vector<std::string>{"a", "b"});
    REQUIRE(t.rows.size() == 2);
    REQUIRE(t.rows[0] == std::vector<std::string>{"1", "2"});
    REQUIRE(t.rows[1] == std::vector<std::string>{"3", "4"});
}

TEST_CASE("parseExcelTable skips Expansion marker rows", "[excel]") {
    std::string_view input =
        "name\tcode\n"
        "Cap\tcap\n"
        "Expansion\t\n"
        "Diadem\tdr9\n";
    const auto t = d2r::parseExcelTable(input);
    REQUIRE(t.rows.size() == 2);
    REQUIRE(t.rows[0][0] == "Cap");
    REQUIRE(t.rows[1][0] == "Diadem");
}

TEST_CASE("parseExcelTable pads short rows and truncates long rows", "[excel]") {
    std::string_view input =
        "a\tb\tc\n"
        "1\t2\n"                // short
        "3\t4\t5\t6\n";         // long
    const auto t = d2r::parseExcelTable(input);
    REQUIRE(t.rows.size() == 2);
    REQUIRE(t.rows[0] == std::vector<std::string>{"1", "2", ""});
    REQUIRE(t.rows[1] == std::vector<std::string>{"3", "4", "5"});
}

TEST_CASE("parseExcelTable keeps empty cells as empty strings", "[excel]") {
    std::string_view input =
        "a\tb\tc\n"
        "1\t\t3\n";
    const auto t = d2r::parseExcelTable(input);
    REQUIRE(t.rows.size() == 1);
    REQUIRE(t.rows[0] == std::vector<std::string>{"1", "", "3"});
}
