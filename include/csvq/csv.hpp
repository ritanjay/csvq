// csv.hpp - RFC 4180 CSV reading and writing.
#pragma once

#include <istream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace csvq {

using Row = std::vector<std::string>;

/// An in-memory table: a header row plus data rows of equal width.
struct Table {
    Row header;
    std::vector<Row> rows;

    /// Index of a column by name, or std::nullopt if it does not exist.
    std::optional<std::size_t> column_index(const std::string& name) const;

    /// Like column_index but throws a readable error for unknown columns.
    std::size_t require_column(const std::string& name) const;
};

/// Thrown for malformed input (unterminated quotes, ragged rows, ...).
class ParseError : public std::runtime_error {
   public:
    ParseError(const std::string& msg, std::size_t line)
        : std::runtime_error("line " + std::to_string(line) + ": " + msg), line_(line) {}
    std::size_t line() const noexcept { return line_; }

   private:
    std::size_t line_;
};

/// Parse CSV text. Handles quoted fields, escaped quotes (""), embedded
/// newlines inside quotes, CRLF line endings and a UTF-8 BOM.
/// The first record is treated as the header. Short rows are padded with
/// empty strings; rows longer than the header raise ParseError.
Table parse_csv(std::istream& in, char delimiter = ',');

/// Convenience overload for parsing from a string.
Table parse_csv_string(const std::string& text, char delimiter = ',');

/// Write one field, quoting it only when needed.
void write_csv_field(std::ostream& out, const std::string& field, char delimiter = ',');

/// Write a full table (header + rows) as CSV.
void write_csv(std::ostream& out, const Table& table, char delimiter = ',');

}  // namespace csvq
