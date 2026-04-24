#pragma once

#include <optional>
#include <string>
#include <utility>

#include "Core/Models/Database/RowValue.h"

namespace gridex {

struct QueryParameter {
    RowValue value;
    std::optional<std::string> type;

    QueryParameter() = default;
    explicit QueryParameter(RowValue v, std::optional<std::string> t = std::nullopt)
        : value(std::move(v)), type(std::move(t)) {}
};

}
