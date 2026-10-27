// format.hpp - output renderers.
#pragma once

#include <ostream>
#include <string>

#include "csvq/csv.hpp"

namespace csvq {

enum class Format { Table, Csv, Json };

/// Parse "table", "csv" or "json".
Format parse_format(const std::string& name);

/// Pretty, aligned text table (numbers right-aligned). UTF-8 aware widths.
void write_pretty(std::ostream& out, const Table& t);

/// JSON array of objects. Numeric cells are emitted as JSON numbers.
void write_json(std::ostream& out, const Table& t);

void write(std::ostream& out, const Table& t, Format f);

}  // namespace csvq
