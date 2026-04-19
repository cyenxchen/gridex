//
// MCPBuiltinTools.cpp
//
// Phase 5 shipped tools. Remaining (list_schemas, list_relationships,
// get_sample_rows, explain_query, search_across_tables, insert_rows,
// update_rows, delete_rows, execute_write_query) land in later sprints.

#include "MCPBuiltinTools.h"
#include "Tier1/ListConnectionsTool.h"
#include "Tier1/ListTablesTool.h"
#include "Tier1/DescribeTableTool.h"
#include "Tier2/QueryTool.h"

namespace DBModels { namespace MCPBuiltinTools {

void registerAll(MCPToolRegistry& r)
{
    // Tier 1 — schema introspection (always on for non-Locked).
    r.registerTool(std::make_shared<ListConnectionsTool>());
    r.registerTool(std::make_shared<ListTablesTool>());
    r.registerTool(std::make_shared<DescribeTableTool>());

    // Tier 2 — read queries (ReadOnly mode allowed, ReadWrite too).
    r.registerTool(std::make_shared<QueryTool>());

    // Tier 3 — write tools (TODO: insert_rows, update_rows,
    // delete_rows, execute_write_query). Until they land Tier 3 is
    // surfaced via the AI as unavailable.
}

}} // namespace
