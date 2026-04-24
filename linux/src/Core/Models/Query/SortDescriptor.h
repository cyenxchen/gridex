#pragma once

#include <string>
#include <string_view>

#include "Core/Enums/SQLDialect.h"

namespace gridex {

enum class SortDirection {
    Ascending,
    Descending,
};

inline std::string_view rawValue(SortDirection d) {
    switch (d) {
        case SortDirection::Ascending:  return "ASC";
        case SortDirection::Descending: return "DESC";
    }
    return "";
}

inline void toggle(SortDirection& d) noexcept {
    d = d == SortDirection::Ascending ? SortDirection::Descending : SortDirection::Ascending;
}

struct QuerySortDescriptor {
    std::string column;
    SortDirection direction = SortDirection::Ascending;

    [[nodiscard]] std::string toSQL(SQLDialect dialect) const {
        std::string out = quoteIdentifier(dialect, column);
        out.push_back(' ');
        out.append(rawValue(direction));
        return out;
    }

    bool operator==(const QuerySortDescriptor&) const noexcept = default;
};

}
