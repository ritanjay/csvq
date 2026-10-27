// query.cpp - the query engine.
#include "csvq/query.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace csvq {

namespace {

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Compare two cell values: numerically when both are numbers, otherwise as text.
/// Returns <0, 0 or >0 like strcmp.
int compare_values(const std::string& a, const std::string& b) {
    const auto na = to_number(a);
    const auto nb = to_number(b);
    if (na && nb) return (*na < *nb) ? -1 : (*na > *nb ? 1 : 0);
    // Numbers sort before text so mixed columns stay predictable.
    if (na && !nb) return -1;
    if (!na && nb) return 1;
    return a.compare(b);
}

bool is_blank(const std::string& s) { return trim(s).empty(); }

bool matches(const Condition& c, const std::string& cell) {
    // Blank cells act like SQL NULL: they never satisfy an ordering comparison.
    // (They can still be found with "col=" or excluded with "col!=".)
    if (is_blank(cell) && c.op != Op::Eq && c.op != Op::Ne) return false;
    if (is_blank(c.value) && (c.op == Op::Eq || c.op == Op::Ne)) {
        return (c.op == Op::Eq) == is_blank(cell);
    }
    switch (c.op) {
        case Op::Contains:
            return to_lower(cell).find(to_lower(c.value)) != std::string::npos;
        case Op::Eq:
            return compare_values(cell, c.value) == 0;
        case Op::Ne:
            return compare_values(cell, c.value) != 0;
        case Op::Lt:
            return compare_values(cell, c.value) < 0;
        case Op::Le:
            return compare_values(cell, c.value) <= 0;
        case Op::Gt:
            return compare_values(cell, c.value) > 0;
        case Op::Ge:
            return compare_values(cell, c.value) >= 0;
    }
    return false;
}

/// Running state for one aggregate inside one group.
struct Accumulator {
    std::size_t count = 0;    // rows seen (for plain count)
    std::size_t numeric = 0;  // numeric values seen (for sum/avg/min/max)
    double sum = 0.0;
    double min = std::numeric_limits<double>::infinity();
    double max = -std::numeric_limits<double>::infinity();

    void add(const std::optional<double>& v) {
        ++count;
        if (!v) return;  // empty / non-numeric cells are ignored, like SQL NULLs
        ++numeric;
        sum += *v;
        min = std::min(min, *v);
        max = std::max(max, *v);
    }

    std::string result(AggFn fn) const {
        switch (fn) {
            case AggFn::Count:
                return std::to_string(count);
            case AggFn::Sum:
                return numeric ? format_number(sum) : "";
            case AggFn::Avg:
                return numeric ? format_number(sum / static_cast<double>(numeric)) : "";
            case AggFn::Min:
                return numeric ? format_number(min) : "";
            case AggFn::Max:
                return numeric ? format_number(max) : "";
        }
        return "";
    }
};

Table filter(const Table& in, const std::vector<Condition>& where) {
    std::vector<std::size_t> idx;
    for (const auto& c : where) idx.push_back(in.require_column(c.column));

    Table out;
    out.header = in.header;
    for (const auto& row : in.rows) {
        bool keep = true;
        for (std::size_t i = 0; i < where.size() && keep; ++i) {
            keep = matches(where[i], row[idx[i]]);
        }
        if (keep) out.rows.push_back(row);
    }
    return out;
}

Table aggregate(const Table& in, const std::vector<std::string>& group_by,
                const std::vector<Aggregate>& aggs) {
    std::vector<std::size_t> key_idx;
    for (const auto& g : group_by) key_idx.push_back(in.require_column(g));

    std::vector<std::optional<std::size_t>> agg_idx;
    for (const auto& a : aggs) {
        if (a.fn == AggFn::Count && a.column.empty()) {
            agg_idx.push_back(std::nullopt);
        } else {
            agg_idx.push_back(in.require_column(a.column));
        }
    }

    // Keep groups in first-seen order so output is stable and intuitive.
    // Groups are looked up by a single joined key string (unit separator
    // between parts), which is much cheaper than hashing a vector of strings.
    std::unordered_map<std::string, std::size_t> group_pos;
    std::vector<Row> keys;
    std::vector<std::vector<Accumulator>> accs;
    std::string key;

    for (const auto& row : in.rows) {
        key.clear();
        for (auto k : key_idx) {
            key += row[k];
            key += '\x1f';
        }
        auto [it, inserted] = group_pos.try_emplace(key, keys.size());
        if (inserted) {
            Row parts;
            for (auto k : key_idx) parts.push_back(row[k]);
            keys.push_back(std::move(parts));
            accs.emplace_back(aggs.size());
        }
        auto& group = accs[it->second];
        for (std::size_t i = 0; i < aggs.size(); ++i) {
            if (!agg_idx[i]) {
                group[i].add(std::nullopt);
            } else {
                // count:col counts non-empty cells only.
                const std::string& cell = row[*agg_idx[i]];
                if (aggs[i].fn == AggFn::Count) {
                    if (!trim(cell).empty()) group[i].add(std::nullopt);
                } else {
                    group[i].add(to_number(cell));
                }
            }
        }
    }

    // With no GROUP BY, aggregates over an empty input still yield one row.
    if (group_by.empty() && keys.empty()) {
        keys.emplace_back();
        accs.emplace_back(aggs.size());
    }

    Table out;
    out.header = group_by;
    for (const auto& a : aggs) out.header.push_back(a.label());
    for (std::size_t g = 0; g < keys.size(); ++g) {
        Row r = keys[g];
        for (std::size_t i = 0; i < aggs.size(); ++i) r.push_back(accs[g][i].result(aggs[i].fn));
        out.rows.push_back(std::move(r));
    }
    return out;
}

void sort_table(Table& t, const std::vector<SortKey>& keys) {
    std::vector<std::pair<std::size_t, bool>> idx;
    for (const auto& k : keys) idx.emplace_back(t.require_column(k.column), k.descending);
    std::stable_sort(t.rows.begin(), t.rows.end(), [&](const Row& a, const Row& b) {
        for (const auto& [col, desc] : idx) {
            // Blank cells always sort last, whatever the direction.
            const bool ba = is_blank(a[col]);
            const bool bb = is_blank(b[col]);
            if (ba != bb) return bb;
            if (ba) continue;
            const int c = compare_values(a[col], b[col]);
            if (c != 0) return desc ? c > 0 : c < 0;
        }
        return false;
    });
}

Table project(const Table& in, const std::vector<std::string>& cols) {
    std::vector<std::size_t> idx;
    for (const auto& c : cols) idx.push_back(in.require_column(c));
    Table out;
    out.header = cols;
    out.rows.reserve(in.rows.size());
    for (const auto& row : in.rows) {
        Row r;
        r.reserve(idx.size());
        for (auto i : idx) r.push_back(row[i]);
        out.rows.push_back(std::move(r));
    }
    return out;
}

}  // namespace

std::string Aggregate::label() const {
    static const char* names[] = {"count", "sum", "avg", "min", "max"};
    std::string name = names[static_cast<int>(fn)];
    return column.empty() ? name : name + "_" + column;
}

Condition parse_condition(const std::string& text) {
    // Longest operators first so ">=" is not read as ">".
    static const std::pair<const char*, Op> ops[] = {
        {">=", Op::Ge}, {"<=", Op::Le}, {"!=", Op::Ne}, {"==", Op::Eq},
        {"=", Op::Eq},  {">", Op::Gt},  {"<", Op::Lt},  {"~", Op::Contains},
    };
    std::size_t best_pos = std::string::npos;
    std::size_t best_len = 0;
    Op best_op = Op::Eq;
    for (const auto& [sym, op] : ops) {
        const auto pos = text.find(sym);
        const std::size_t len = std::char_traits<char>::length(sym);
        if (pos != std::string::npos && (pos < best_pos || (pos == best_pos && len > best_len))) {
            best_pos = pos;
            best_len = len;
            best_op = op;
        }
    }
    if (best_pos == std::string::npos) {
        throw std::invalid_argument("invalid condition '" + text +
                                    "' (use =, !=, <, <=, >, >= or ~)");
    }
    Condition c{trim(text.substr(0, best_pos)), best_op, trim(text.substr(best_pos + best_len))};
    if (c.column.empty()) throw std::invalid_argument("condition '" + text + "' has no column");
    return c;
}

std::vector<Aggregate> parse_aggregates(const std::string& text) {
    std::vector<Aggregate> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (item.empty()) continue;
        const auto colon = item.find(':');
        const std::string fn = to_lower(trim(item.substr(0, colon)));
        const std::string col = colon == std::string::npos ? "" : trim(item.substr(colon + 1));

        Aggregate a{AggFn::Count, col};
        if (fn == "count")
            a.fn = AggFn::Count;
        else if (fn == "sum")
            a.fn = AggFn::Sum;
        else if (fn == "avg" || fn == "mean")
            a.fn = AggFn::Avg;
        else if (fn == "min")
            a.fn = AggFn::Min;
        else if (fn == "max")
            a.fn = AggFn::Max;
        else
            throw std::invalid_argument("unknown aggregate '" + fn +
                                        "' (use count, sum, avg, min, max)");

        if (a.fn != AggFn::Count && col.empty()) {
            throw std::invalid_argument("aggregate '" + fn + "' needs a column, e.g. " + fn +
                                        ":price");
        }
        out.push_back(a);
    }
    if (out.empty()) throw std::invalid_argument("empty aggregate list");
    return out;
}

SortKey parse_sort_key(const std::string& text) {
    const auto colon = text.rfind(':');
    if (colon != std::string::npos) {
        const std::string dir = to_lower(trim(text.substr(colon + 1)));
        if (dir == "desc") return {trim(text.substr(0, colon)), true};
        if (dir == "asc") return {trim(text.substr(0, colon)), false};
    }
    return {trim(text), false};
}

std::optional<double> to_number(const std::string& s) {
    const std::string t = trim(s);
    if (t.empty()) return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}

std::string format_number(double v) {
    // Round to 4 decimals (hides float noise like 0.30000000000000004),
    // then drop trailing zeros: 3.5000 -> "3.5", 2.0000 -> "2".
    char buf[128];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s = buf;
    s.erase(s.find_last_not_of('0') + 1);
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";  // normalise negative zero after rounding
    return s;
}

Table run_query(const Table& input, const Query& q) {
    // Only materialise a filtered copy when there is a filter; otherwise work
    // straight from the input to avoid copying every row.
    Table t;
    const Table* src = &input;
    if (!q.where.empty()) {
        t = filter(input, q.where);
        src = &t;
    }

    if (!q.aggregates.empty() || !q.group_by.empty()) {
        // GROUP BY without aggregates behaves like DISTINCT + count.
        const auto aggs =
            q.aggregates.empty() ? std::vector<Aggregate>{{AggFn::Count, ""}} : q.aggregates;
        t = aggregate(*src, q.group_by, aggs);
    } else if (src == &input) {
        t = input;  // sort/select/limit need a mutable copy
    }

    if (!q.sort.empty()) sort_table(t, q.sort);
    if (!q.select.empty()) t = project(t, q.select);
    if (q.limit && t.rows.size() > *q.limit) t.rows.resize(*q.limit);
    return t;
}

}  // namespace csvq
