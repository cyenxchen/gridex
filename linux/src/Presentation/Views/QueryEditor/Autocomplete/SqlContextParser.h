#pragma once

#include "Presentation/Views/QueryEditor/Autocomplete/CompletionModels.h"

#include <QString>

namespace gridex {

// Parses SQL text up to cursor position to determine autocomplete context.
// Handles: statement isolation (split by ;), string literal detection,
// alias/CTE extraction, multi-table scope, compound keywords, positional
// awareness (clause-aware next-keyword suggestions).
class SqlContextParser {
public:
    CompletionContext parse(const QString& sql, int cursorOffset) const;
};

}  // namespace gridex
