// format.cpp - text table, CSV and JSON writers.
#include "csvq/format.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "csvq/query.hpp"

namespace csvq {

namespace {

/// Display width of a UTF-8 string (counts code points, not bytes).
std::size_t display_width(const std::string& s) {
    std::size_t n = 0;
    for (const char ch : s) {
        if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++n;  // skip continuation bytes
    }
    return n;
}

void json_string(std::ostream& out, const std::string& s) {
    out << '"';
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out << buf;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    out << '"';
}

/// True if a cell should be emitted as a bare JSON number.
bool is_json_number(const std::string& s) {
    if (!to_number(s)) return false;
    // Reject forms JSON does not allow: leading '+', leading zeros, hex, ".5", "5.", spaces.
    if (s.empty() || s.front() == '+' || s.front() == ' ' || s.back() == ' ') return false;
    if (s.find_first_of("xXnN") != std::string::npos) return false;  // hex, nan, inf
    const std::size_t start = (s[0] == '-') ? 1 : 0;
    if (start >= s.size() || s[start] == '.') return false;
    if (s[start] == '0' && s.size() > start + 1 &&
        std::isdigit(static_cast<unsigned char>(s[start + 1])))
        return false;
    const auto dot = s.find('.');
    if (dot != std::string::npos &&
        (dot + 1 >= s.size() || !std::isdigit(static_cast<unsigned char>(s[dot + 1]))))
        return false;
    return true;
}

}  // namespace

Format parse_format(const std::string& name) {
    if (name == "table") return Format::Table;
    if (name == "csv") return Format::Csv;
    if (name == "json") return Format::Json;
    throw std::invalid_argument("unknown format '" + name + "' (use table, csv or json)");
}

void write_pretty(std::ostream& out, const Table& t) {
    const std::size_t cols = t.header.size();
    std::vector<std::size_t> width(cols);
    std::vector<bool> numeric(cols, true);

    for (std::size_t c = 0; c < cols; ++c) width[c] = display_width(t.header[c]);
    for (const auto& row : t.rows) {
        for (std::size_t c = 0; c < cols; ++c) {
            width[c] = std::max(width[c], display_width(row[c]));
            if (!row[c].empty() && !to_number(row[c])) numeric[c] = false;
        }
    }

    auto pad = [&](const std::string& s, std::size_t c, bool right) {
        const std::string fill(width[c] - display_width(s), ' ');
        if (right)
            out << fill << s;
        else if (c + 1 == cols)
            out << s;  // no trailing spaces on the last column
        else
            out << s << fill;
    };
    auto rule = [&] {
        for (std::size_t c = 0; c < cols; ++c)
            out << (c ? "-+-" : "") << std::string(width[c], '-');
        out << '\n';
    };

    for (std::size_t c = 0; c < cols; ++c) {
        if (c) out << " | ";
        pad(t.header[c], c, false);
    }
    out << '\n';
    rule();
    for (const auto& row : t.rows) {
        for (std::size_t c = 0; c < cols; ++c) {
            if (c) out << " | ";
            pad(row[c], c, numeric[c] && !t.rows.empty());
        }
        out << '\n';
    }
    out << "(" << t.rows.size() << (t.rows.size() == 1 ? " row)" : " rows)") << '\n';
}

void write_json(std::ostream& out, const Table& t) {
    out << "[";
    for (std::size_t r = 0; r < t.rows.size(); ++r) {
        out << (r ? ",\n  {" : "\n  {");
        for (std::size_t c = 0; c < t.header.size(); ++c) {
            if (c) out << ", ";
            json_string(out, t.header[c]);
            out << ": ";
            const std::string& v = t.rows[r][c];
            if (is_json_number(v))
                out << v;
            else if (v.empty())
                out << "null";
            else
                json_string(out, v);
        }
        out << "}";
    }
    out << (t.rows.empty() ? "]\n" : "\n]\n");
}

void write(std::ostream& out, const Table& t, Format f) {
    switch (f) {
        case Format::Table:
            write_pretty(out, t);
            break;
        case Format::Csv:
            write_csv(out, t);
            break;
        case Format::Json:
            write_json(out, t);
            break;
    }
}

}  // namespace csvq
