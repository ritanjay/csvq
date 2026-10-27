// csv.cpp - streaming RFC 4180 parser implemented as a small state machine.
#include "csvq/csv.hpp"

#include <iterator>
#include <sstream>

namespace csvq {

std::optional<std::size_t> Table::column_index(const std::string& name) const {
    for (std::size_t i = 0; i < header.size(); ++i) {
        if (header[i] == name) return i;
    }
    return std::nullopt;
}

std::size_t Table::require_column(const std::string& name) const {
    if (auto idx = column_index(name)) return *idx;
    std::string known;
    for (const auto& h : header) known += (known.empty() ? "" : ", ") + h;
    throw std::invalid_argument("unknown column '" + name + "' (available: " + known + ")");
}

namespace {

enum class State : unsigned char { FieldStart, Unquoted, Quoted, QuoteInQuoted };

}  // namespace

Table parse_csv(std::istream& in, char delimiter) {
    // Read the whole input once; scanning an in-memory buffer is several
    // times faster than pulling characters one by one from the stream.
    std::string buf{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const char* p = buf.data();
    const char* const end = p + buf.size();

    // Skip a UTF-8 byte order mark.
    if (buf.size() >= 3 && static_cast<unsigned char>(p[0]) == 0xEF &&
        static_cast<unsigned char>(p[1]) == 0xBB && static_cast<unsigned char>(p[2]) == 0xBF) {
        p += 3;
    }

    Table table;
    std::vector<Row> records;
    std::vector<std::size_t> record_lines;  // starting line of each record
    Row record;
    std::string field;
    State state = State::FieldStart;
    std::size_t line = 1;         // current physical line (for error messages)
    std::size_t record_line = 1;  // line on which the current record started
    std::size_t width_hint = 0;   // fields in the first record, used to reserve()

    auto end_field = [&] {
        record.push_back(std::move(field));
        field.clear();
        state = State::FieldStart;
    };
    auto end_record = [&] {
        end_field();
        // Skip completely blank lines (a single empty field).
        if (!(record.size() == 1 && record[0].empty())) {
            if (width_hint == 0) width_hint = record.size();
            records.push_back(std::move(record));
            record_lines.push_back(record_line);
        }
        record.clear();
        record.reserve(width_hint);
        record_line = line + 1;
    };
    auto is_special = [delimiter](char ch) {
        return ch == delimiter || ch == '\n' || ch == '\r' || ch == '"';
    };

    while (p < end) {
        const char c = *p++;
        switch (state) {
            case State::FieldStart:
                if (c == '"') {
                    state = State::Quoted;
                } else if (c == delimiter) {
                    end_field();
                } else if (c == '\n') {
                    end_record();
                    ++line;
                } else if (c == '\r') {
                    // Ignore; the following '\n' ends the record.
                } else {
                    // Fast path: copy the rest of an unquoted field in one go.
                    const char* q = p;
                    while (q < end && !is_special(*q)) ++q;
                    field.assign(p - 1, q);
                    p = q;
                    state = State::Unquoted;
                }
                break;

            case State::Unquoted:
                if (c == delimiter) {
                    end_field();
                } else if (c == '\n') {
                    end_record();
                    ++line;
                } else if (c == '\r') {
                    // Ignore CR of a CRLF pair.
                } else {
                    field += c;  // a stray quote inside an unquoted field is kept literally
                }
                break;

            case State::Quoted:
                if (c == '"') {
                    state = State::QuoteInQuoted;
                } else {
                    if (c == '\n') ++line;
                    field += c;
                }
                break;

            case State::QuoteInQuoted:
                if (c == '"') {  // escaped quote ""
                    field += '"';
                    state = State::Quoted;
                } else if (c == delimiter) {
                    end_field();
                } else if (c == '\n') {
                    end_record();
                    ++line;
                } else if (c == '\r') {
                    // Ignore CR of a CRLF pair.
                } else {
                    throw ParseError("unexpected character after closing quote", line);
                }
                break;
        }
    }

    if (state == State::Quoted) {
        throw ParseError("unterminated quoted field", record_line);
    }
    // Flush the last record when the file does not end with a newline.
    if (!field.empty() || !record.empty() || state != State::FieldStart) {
        end_record();
    }

    if (records.empty()) return table;

    table.header = std::move(records.front());
    const std::size_t width = table.header.size();
    table.rows.reserve(records.size() - 1);
    for (std::size_t i = 1; i < records.size(); ++i) {
        Row& r = records[i];
        if (r.size() > width) {
            throw ParseError("row has " + std::to_string(r.size()) + " fields, header has " +
                                 std::to_string(width),
                             record_lines[i]);
        }
        r.resize(width);  // pad short rows with empty fields
        table.rows.push_back(std::move(r));
    }
    return table;
}

Table parse_csv_string(const std::string& text, char delimiter) {
    std::istringstream in(text);
    return parse_csv(in, delimiter);
}

void write_csv_field(std::ostream& out, const std::string& field, char delimiter) {
    const bool needs_quotes =
        field.find_first_of(std::string{delimiter, '"', '\n', '\r'}) != std::string::npos;
    if (!needs_quotes) {
        out << field;
        return;
    }
    out << '"';
    for (char ch : field) {
        if (ch == '"') out << '"';
        out << ch;
    }
    out << '"';
}

void write_csv(std::ostream& out, const Table& table, char delimiter) {
    auto write_row = [&](const Row& row) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i) out << delimiter;
            write_csv_field(out, row[i], delimiter);
        }
        out << '\n';
    };
    write_row(table.header);
    for (const auto& row : table.rows) write_row(row);
}

}  // namespace csvq
