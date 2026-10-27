// query.hpp - filtering, grouping, aggregation, sorting and projection.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "csvq/csv.hpp"

namespace csvq {

/// Comparison operators supported by --where.
enum class Op { Eq, Ne, Lt, Le, Gt, Ge, Contains };

/// One filter condition, e.g. "price >= 10" or "city ~ york".
struct Condition {
    std::string column;
    Op op;
    std::string value;
};

/// Aggregate functions supported by --agg.
enum class AggFn { Count, Sum, Avg, Min, Max };

/// One aggregate, e.g. "sum:price" (column is empty for plain "count").
struct Aggregate {
    AggFn fn;
    std::string column;

    /// Output column name, e.g. "sum_price" or "count".
    std::string label() const;
};

struct SortKey {
    std::string column;
    bool descending = false;
};

/// A full query. Steps run in SQL order: where -> group/agg -> sort -> select -> limit.
struct Query {
    std::vector<Condition> where;
    std::vector<std::string> group_by;
    std::vector<Aggregate> aggregates;
    std::vector<SortKey> sort;
    std::vector<std::string> select;
    std::optional<std::size_t> limit;
};

/// Parse "col>=10", "col != x", "name~smith" into a Condition.
Condition parse_condition(const std::string& text);

/// Parse a comma-separated aggregate list: "count,sum:price,avg:qty".
std::vector<Aggregate> parse_aggregates(const std::string& text);

/// Parse "col" or "col:desc" / "col:asc".
SortKey parse_sort_key(const std::string& text);

/// Parse a number if the whole string is numeric (leading/trailing spaces allowed).
std::optional<double> to_number(const std::string& s);

/// Format a double without trailing zeros: 3.50 -> "3.5", 2.0 -> "2".
std::string format_number(double v);

/// Run a query against a table and return the result table.
Table run_query(const Table& input, const Query& query);

}  // namespace csvq
