#pragma once
#include <string>
#include <optional>
#include "QueryResult.h"
#include "ColumnInfo.h"

namespace DBModels
{
    enum class TabType
    {
        DataGrid,
        QueryEditor,
        TableStructure,
        TableList,
        FunctionDetail,
        ERDiagram
    };

    struct ContentTab
    {
        std::wstring id;
        TabType type = TabType::DataGrid;
        std::wstring title;
        std::wstring tableName;
        std::wstring schema;
        std::wstring databaseName;
        bool isActive = false;
        bool isDirty = false;

        // Cached data per tab (so switching tabs restores state)
        std::optional<QueryResult> cachedData;
        std::vector<ColumnInfo> cachedColumns;
        std::vector<IndexInfo> cachedIndexes;
        std::vector<ForeignKeyInfo> cachedForeignKeys;
        int cachedPage = 0;

        // Query editor SQL text (only used when type == QueryEditor).
        // Saved on tab switch so each query tab has its own content
        // instead of sharing the single QueryEditorView text box.
        std::wstring cachedSql;

        // ER Diagram tab payload (only used when type == ERDiagram)
        // erJsonText is what the WebView renderer consumes (dagre +
        // custom SVG). erD2Text is kept only so the Copy D2 toolbar
        // button still works; it isn't needed for rendering anymore.
        std::wstring erJsonText;
        std::wstring erD2Text;
        std::wstring erSvgPath;
        int erTableCount = 0;
        int erRelationshipCount = 0;
    };
}
