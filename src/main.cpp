// main.cpp - command-line interface for csvq.
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "csvq/csv.hpp"
#include "csvq/format.hpp"
#include "csvq/query.hpp"

namespace {

constexpr const char* kVersion = "1.0.0";

constexpr const char* kUsage = R"(csvq - query CSV files from the command line

Usage:
  csvq [options] FILE           (use - to read from stdin)

Options:
  -s, --select COLS        Comma-separated output columns
  -w, --where COND         Filter rows; repeat for AND. Operators: = != < <= > >= ~
                           (~ is case-insensitive "contains"). Example: -w "price>=10"
  -g, --group-by COLS      Comma-separated grouping columns
  -a, --agg LIST           Aggregates: count, count:col, sum:col, avg:col, min:col, max:col
  -o, --sort KEYS          Sort keys, e.g. "total:desc,name"
  -n, --limit N            Keep at most N rows
  -f, --format FMT         table (default), csv or json
  -d, --delimiter CHAR     Input delimiter (default ','; use '\t' for TSV)
      --stats              Print rows read and timing to stderr
  -h, --help               Show this help
  -v, --version            Show version

Examples:
  csvq -w "region=West" -o "revenue:desc" -n 5 sales.csv
  csvq -g region -a "count,sum:revenue,avg:revenue" -o "sum_revenue:desc" sales.csv
  csvq -s product,revenue -f json sales.csv
)";

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

/// Thrown for bad command-line usage (exit code 2).
struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace

int main(int argc, char** argv) {
    using namespace csvq;

    Query query;
    Format format = Format::Table;
    char delimiter = ',';
    bool stats = false;
    std::string path;

    try {
        std::vector<std::string> args(argv + 1, argv + argc);
        auto value_for = [&](std::size_t& i) -> const std::string& {
            if (i + 1 >= args.size()) throw UsageError("option " + args[i] + " needs a value");
            return args[++i];
        };

        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (a == "-h" || a == "--help") {
                std::cout << kUsage;
                return 0;
            } else if (a == "-v" || a == "--version") {
                std::cout << "csvq " << kVersion << '\n';
                return 0;
            } else if (a == "-s" || a == "--select") {
                for (auto& c : split(value_for(i), ',')) query.select.push_back(c);
            } else if (a == "-w" || a == "--where") {
                query.where.push_back(parse_condition(value_for(i)));
            } else if (a == "-g" || a == "--group-by") {
                for (auto& c : split(value_for(i), ',')) query.group_by.push_back(c);
            } else if (a == "-a" || a == "--agg") {
                for (auto& ag : parse_aggregates(value_for(i))) query.aggregates.push_back(ag);
            } else if (a == "-o" || a == "--sort") {
                for (auto& k : split(value_for(i), ',')) query.sort.push_back(parse_sort_key(k));
            } else if (a == "-n" || a == "--limit") {
                const std::string& v = value_for(i);
                const auto n = to_number(v);
                if (!n || *n < 0 || *n != static_cast<double>(static_cast<std::size_t>(*n)))
                    throw UsageError("--limit needs a non-negative integer, got '" + v + "'");
                query.limit = static_cast<std::size_t>(*n);
            } else if (a == "-f" || a == "--format") {
                format = parse_format(value_for(i));
            } else if (a == "-d" || a == "--delimiter") {
                const std::string& v = value_for(i);
                if (v == "\\t" || v == "tab")
                    delimiter = '\t';
                else if (v.size() == 1)
                    delimiter = v[0];
                else
                    throw UsageError("--delimiter must be a single character");
            } else if (a == "--stats") {
                stats = true;
            } else if (a.size() > 1 && a[0] == '-' && a != "-") {
                throw UsageError("unknown option '" + a + "' (see --help)");
            } else if (path.empty()) {
                path = a;
            } else {
                throw UsageError("only one input file is supported");
            }
        }
        if (path.empty()) throw UsageError("no input file given (see --help)");

        const auto t0 = std::chrono::steady_clock::now();
        Table input;
        if (path == "-") {
            input = parse_csv(std::cin, delimiter);
        } else {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                std::cerr << "csvq: cannot open '" << path << "'\n";
                return 1;
            }
            input = parse_csv(file, delimiter);
        }
        const auto t1 = std::chrono::steady_clock::now();
        const Table result = run_query(input, query);
        const auto t2 = std::chrono::steady_clock::now();

        write(std::cout, result, format);

        if (stats) {
            using ms = std::chrono::duration<double, std::milli>;
            std::cerr << "rows read: " << input.rows.size() << ", rows out: " << result.rows.size()
                      << ", parse: " << ms(t1 - t0).count() << " ms"
                      << ", query: " << ms(t2 - t1).count() << " ms\n";
        }
        return 0;
    } catch (const UsageError& e) {
        std::cerr << "csvq: " << e.what() << '\n';
        return 2;
    } catch (const ParseError& e) {
        std::cerr << "csvq: parse error at " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "csvq: " << e.what() << '\n';
        return 1;
    }
}
