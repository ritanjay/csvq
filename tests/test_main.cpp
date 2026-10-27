// test_main.cpp - unit tests. A tiny self-contained harness keeps the
// project dependency-free (no network needed to build or test).
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "csvq/csv.hpp"
#include "csvq/format.hpp"
#include "csvq/query.hpp"

namespace {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Register {
    Register(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define TEST(name)                           \
    static void name();                      \
    static Register reg_##name(#name, name); \
    static void name()

#define CHECK(cond)                                                                               \
    do {                                                                                          \
        if (!(cond))                                                                              \
            throw Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #cond); \
    } while (0)

#define CHECK_EQ(a, b)                                                                         \
    do {                                                                                       \
        const auto& va_ = (a);                                                                 \
        const auto& vb_ = (b);                                                                 \
        if (!(va_ == vb_)) {                                                                   \
            std::ostringstream os_;                                                            \
            os_ << __FILE__ << ":" << __LINE__ << ": " #a " == " #b "\n    got:      '" << va_ \
                << "'\n    expected: '" << vb_ << "'";                                         \
            throw Failure(os_.str());                                                          \
        }                                                                                      \
    } while (0)

#define CHECK_THROWS(expr)                                                         \
    do {                                                                           \
        bool threw_ = false;                                                       \
        try {                                                                      \
            (void)(expr);                                                          \
        } catch (...) {                                                            \
            threw_ = true;                                                         \
        }                                                                          \
        if (!threw_)                                                               \
            throw Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                          ": expected exception from " #expr);                     \
    } while (0)

using namespace csvq;

const char* kSales =
    "region,product,units,revenue\n"
    "West,Widget,10,100.50\n"
    "East,Widget,5,50\n"
    "West,Gadget,3,90\n"
    "North,Gadget,,\n"
    "East,Gizmo,8,120\n";

std::string to_csv(const Table& t) {
    std::ostringstream os;
    write_csv(os, t);
    return os.str();
}

// ---------------------------------------------------------------- parser

TEST(parses_simple_file) {
    const Table t = parse_csv_string("a,b\n1,2\n3,4\n");
    CHECK_EQ(t.header.size(), 2u);
    CHECK_EQ(t.rows.size(), 2u);
    CHECK_EQ(t.rows[1][0], std::string("3"));
}

TEST(handles_quotes_escapes_and_embedded_newlines) {
    const Table t = parse_csv_string("name,note\n\"Smith, J\",\"said \"\"hi\"\"\nthen left\"\n");
    CHECK_EQ(t.rows.size(), 1u);
    CHECK_EQ(t.rows[0][0], std::string("Smith, J"));
    CHECK_EQ(t.rows[0][1], std::string("said \"hi\"\nthen left"));
}

TEST(handles_crlf_bom_and_missing_final_newline) {
    const Table t = parse_csv_string("\xEF\xBB\xBFid,val\r\n1,x\r\n2,y");
    CHECK_EQ(t.header[0], std::string("id"));
    CHECK_EQ(t.rows.size(), 2u);
    CHECK_EQ(t.rows[1][1], std::string("y"));
}

TEST(pads_short_rows_and_skips_blank_lines) {
    const Table t = parse_csv_string("a,b,c\n1\n\n4,5,6\n");
    CHECK_EQ(t.rows.size(), 2u);
    CHECK_EQ(t.rows[0].size(), 3u);
    CHECK_EQ(t.rows[0][2], std::string(""));
}

TEST(trailing_empty_field_is_kept) {
    const Table t = parse_csv_string("a,b\n1,");
    CHECK_EQ(t.rows.size(), 1u);
    CHECK_EQ(t.rows[0][1], std::string(""));
}

TEST(rejects_malformed_input) {
    CHECK_THROWS(parse_csv_string("a,b\n\"open,2\n"));  // unterminated quote
    CHECK_THROWS(parse_csv_string("a,b\n1,2,3\n"));     // too many fields
    CHECK_THROWS(parse_csv_string("a\n\"x\"y\n"));      // text after closing quote
}

TEST(supports_custom_delimiter) {
    const Table t = parse_csv_string("a\tb\n1\t2\n", '\t');
    CHECK_EQ(t.rows[0][1], std::string("2"));
}

TEST(csv_round_trip_quotes_when_needed) {
    const Table t = parse_csv_string("a,b\n\"x,y\",\"q\"\"t\"\nplain,\n");
    CHECK_EQ(to_csv(t), std::string("a,b\n\"x,y\",\"q\"\"t\"\nplain,\n"));
}

// ---------------------------------------------------------------- helpers

TEST(number_parsing_and_formatting) {
    CHECK(to_number(" 42 ").has_value());
    CHECK(!to_number("42abc").has_value());
    CHECK(!to_number("").has_value());
    CHECK(!to_number("nan").has_value());
    CHECK_EQ(format_number(3.50), std::string("3.5"));
    CHECK_EQ(format_number(2.0), std::string("2"));
    CHECK_EQ(format_number(-0.0), std::string("0"));
    CHECK_EQ(format_number(0.1 + 0.2), std::string("0.3"));
    CHECK_EQ(format_number(20.457627), std::string("20.4576"));
    CHECK_EQ(format_number(-0.00001), std::string("0"));
    CHECK_EQ(format_number(1500), std::string("1500"));
}

TEST(condition_parsing) {
    auto c = parse_condition("price >= 10");
    CHECK_EQ(c.column, std::string("price"));
    CHECK(c.op == Op::Ge);
    CHECK_EQ(c.value, std::string("10"));
    CHECK(parse_condition("a!=b").op == Op::Ne);
    CHECK(parse_condition("name~smith").op == Op::Contains);
    CHECK(parse_condition("x==1").op == Op::Eq);
    CHECK_THROWS(parse_condition("no operator"));
    CHECK_THROWS(parse_condition(">=5"));
}

TEST(aggregate_parsing) {
    const auto a = parse_aggregates("count, sum:revenue,AVG:units");
    CHECK_EQ(a.size(), 3u);
    CHECK_EQ(a[0].label(), std::string("count"));
    CHECK_EQ(a[1].label(), std::string("sum_revenue"));
    CHECK_EQ(a[2].label(), std::string("avg_units"));
    CHECK_THROWS(parse_aggregates("median:x"));
    CHECK_THROWS(parse_aggregates("sum"));
}

// ---------------------------------------------------------------- queries

TEST(filter_numeric_and_text) {
    const Table in = parse_csv_string(kSales);
    Query q;
    q.where = {parse_condition("units>=8")};
    CHECK_EQ(run_query(in, q).rows.size(), 2u);  // 10 and 8; "" and 5, 3 excluded

    q.where = {parse_condition("region=West"), parse_condition("product~GADG")};
    const Table out = run_query(in, q);
    CHECK_EQ(out.rows.size(), 1u);
    CHECK_EQ(out.rows[0][3], std::string("90"));
}

TEST(numeric_comparison_is_not_lexicographic) {
    const Table in = parse_csv_string("n\n9\n10\n100\n");
    Query q;
    q.where = {parse_condition("n>9")};
    CHECK_EQ(run_query(in, q).rows.size(), 2u);
}

TEST(group_by_with_aggregates_and_sort) {
    const Table in = parse_csv_string(kSales);
    Query q;
    q.group_by = {"region"};
    q.aggregates = parse_aggregates("count,sum:revenue,avg:units,count:units");
    q.sort = {parse_sort_key("sum_revenue:desc")};
    const Table out = run_query(in, q);
    CHECK_EQ(to_csv(out), std::string("region,count,sum_revenue,avg_units,count_units\n"
                                      "West,2,190.5,6.5,2\n"
                                      "East,2,170,6.5,2\n"
                                      "North,1,,,0\n"));
}

TEST(aggregates_without_group_by_give_one_row) {
    const Table in = parse_csv_string(kSales);
    Query q;
    q.aggregates = parse_aggregates("count,min:revenue,max:revenue");
    CHECK_EQ(to_csv(run_query(in, q)), std::string("count,min_revenue,max_revenue\n5,50,120\n"));

    q.where = {parse_condition("region=Nowhere")};
    CHECK_EQ(to_csv(run_query(in, q)), std::string("count,min_revenue,max_revenue\n0,,\n"));
}

TEST(select_sort_limit) {
    const Table in = parse_csv_string(kSales);
    Query q;
    q.select = {"product", "revenue"};
    q.sort = {parse_sort_key("revenue:desc")};
    q.limit = 2;
    CHECK_EQ(to_csv(run_query(in, q)), std::string("product,revenue\nGizmo,120\nWidget,100.50\n"));
}

TEST(multi_key_sort_is_stable) {
    const Table in = parse_csv_string("k,v\nb,1\na,2\nb,0\na,1\n");
    Query q;
    q.sort = {parse_sort_key("k"), parse_sort_key("v:desc")};
    CHECK_EQ(to_csv(run_query(in, q)), std::string("k,v\na,2\na,1\nb,1\nb,0\n"));
}

TEST(blank_cells_behave_like_null) {
    const Table in = parse_csv_string("n\n5\n\n \n2\n", ',');
    // Blank-only line is skipped by the parser; " " is kept as a blank cell.
    Query q;
    q.where = {parse_condition("n<10")};
    CHECK_EQ(run_query(in, q).rows.size(), 2u);
    q.where = {parse_condition("n=")};
    CHECK_EQ(run_query(in, q).rows.size(), 1u);
    q.where = {};
    q.sort = {parse_sort_key("n")};
    CHECK_EQ(to_csv(run_query(in, q)), std::string("n\n2\n5\n \n"));
    q.sort = {parse_sort_key("n:desc")};
    CHECK_EQ(to_csv(run_query(in, q)), std::string("n\n5\n2\n \n"));
}

TEST(unknown_column_is_reported) {
    const Table in = parse_csv_string(kSales);
    Query q;
    q.select = {"nope"};
    CHECK_THROWS(run_query(in, q));
}

// ---------------------------------------------------------------- output

TEST(json_output_types) {
    const Table t = parse_csv_string("name,n,code,empty\n\"A \"\"x\"\"\",1.5,007,\n");
    std::ostringstream os;
    write_json(os, t);
    CHECK_EQ(os.str(), std::string("[\n  {\"name\": \"A \\\"x\\\"\", \"n\": 1.5, \"code\": "
                                   "\"007\", \"empty\": null}\n]\n"));
}

TEST(pretty_table_alignment) {
    const Table t = parse_csv_string("item,qty\napple,5\nkiwi,12\n");
    std::ostringstream os;
    write_pretty(os, t);
    CHECK_EQ(os.str(), std::string("item  | qty\n"
                                   "------+----\n"
                                   "apple |   5\n"
                                   "kiwi  |  12\n"
                                   "(2 rows)\n"));
}

}  // namespace

int main() {
    int failed = 0;
    for (const auto& t : registry()) {
        try {
            t.fn();
            std::cout << "  [pass] " << t.name << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "  [FAIL] " << t.name << "\n    " << e.what() << '\n';
        }
    }
    std::cout << '\n'
              << (registry().size() - failed) << "/" << registry().size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
