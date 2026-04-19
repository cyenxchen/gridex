#pragma once
#include <string>
#include <memory>
#include "DatabaseAdapter.h"
#include "DumpRestoreService.h"  // ProgressCallback

namespace DBModels
{
    struct ERDiagramResult
    {
        bool success = false;
        std::wstring d2Text;         // Generated D2 source (Copy D2 button)
        std::wstring jsonText;       // Schema JSON consumed by the WebView renderer
        std::wstring svgPath;        // Legacy: path to d2.exe rendered SVG (empty on the JSON path)
        int tableCount = 0;
        int relationshipCount = 0;
        std::wstring error;          // Subprocess stderr or generation error
    };

    // Generate ER diagram from schema using D2 declarative format.
    // Renders via bundled d2.exe (Assets/d2/d2.exe) -> SVG file in temp dir.
    // Native rendering via WinUI 3 SvgImageSource on caller side.
    class ERDiagramService
    {
    public:
        // Full pipeline: introspect schema -> generate D2 -> run d2.exe -> SVG path
        static ERDiagramResult Generate(
            std::shared_ptr<DatabaseAdapter> adapter,
            const std::wstring& schema,
            ProgressCallback progress = nullptr);

        // Just the D2 text without rendering — used by Copy D2 button
        static std::wstring GenerateD2Text(
            std::shared_ptr<DatabaseAdapter> adapter,
            const std::wstring& schema,
            ProgressCallback progress = nullptr);

        // Walk the schema and return a result whose jsonText feeds the
        // native WebView renderer (dagre + svg-pan-zoom + hand-rolled
        // SVG cards) — no d2.exe involved. Shape of jsonText:
        //   { "tables":[ {"name","schema","columns":[{name,type,isPk,isFk,nullable}]} ],
        //     "edges":[ {"fromTable","fromColumn","toTable","toColumn"} ] }
        // tableCount / relationshipCount are populated from the same
        // walk so the caller doesn't re-introspect.
        // On failure: success=false, jsonText empty, error populated.
        static ERDiagramResult GenerateJson(
            std::shared_ptr<DatabaseAdapter> adapter,
            const std::wstring& schema,
            ProgressCallback progress = nullptr);

        // Resolve d2.exe path (Package.Current install dir or exe dir)
        static std::wstring LocateD2Exe();

        // ── Public D2 helpers ──────────────────────────────
        // Shared escape/quote helpers for any caller emitting D2 source.
        // Previously file-scope statics; promoted to public static so other
        // services (e.g. enterprise row-relationship graph) reuse the same
        // rules without copy-paste drift.

        // Replace non-[A-Za-z0-9_] with '_'; prefix "t_" if starts with digit.
        static std::wstring SanitizeIdentifier(const std::wstring& name);

        // True if name collides with a d2 reserved keyword (shape, width, …).
        static bool IsD2Reserved(const std::wstring& name);

        // Wrap in double quotes if reserved, else return unchanged.
        static std::wstring QuoteIfReserved(const std::wstring& name);

        // Wrap text in double quotes and escape ", \, and newlines so it
        // can be used as a D2 string label.
        static std::wstring EscapeD2Label(const std::wstring& text);

        // Build %TEMP%\gridex\<prefix><tick>_<counter>.<extension>.
        // Default prefix "er_" preserves existing behavior; callers that
        // need a disjoint filename namespace pass their own prefix.
        static std::wstring TempPath(const std::wstring& extension,
                                     const std::wstring& prefix = L"er_");

        // Invoke bundled d2.exe with standard layout/theme/bundle flags.
        // Writes SVG to outputPath; captures stderr into stderrOut.
        // Returns process exit code (-1 on launch failure).
        static int RunD2(const std::wstring& d2ExePath,
                         const std::wstring& inputPath,
                         const std::wstring& outputPath,
                         std::wstring& stderrOut);
    };
}
