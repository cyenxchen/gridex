#include "pch.h"
#include "xaml-includes.h"
#include "MCPPage.h"
#if __has_include("MCPPage.g.cpp")
#include "MCPPage.g.cpp"
#endif

#include "Models/AppSettings.h"
#include "Models/ConnectionStore.h"
#include "Services/MCP/MCPServerHost.h"
#include "GridexVersion.h"
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.UI.h>

namespace winrt::Gridex::implementation
{
    namespace mux  = winrt::Microsoft::UI::Xaml;
    namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
    namespace wadt = winrt::Windows::ApplicationModel::DataTransfer;

    // GRIDEX_VERSION is a wide-string literal in GridexVersion.h — we
    // need the server version as UTF-8 for the JSON handshake.
    static std::string versionUtf8()
    {
        std::wstring w(GRIDEX_VERSION);
        std::string out;
        out.reserve(w.size());
        for (wchar_t c : w) out.push_back(static_cast<char>(c & 0x7F));
        return out;
    }

    MCPPage::MCPPage()
    {
        InitializeComponent();

        this->Loaded([this](auto&&, auto&&) { RefreshUI(); });
    }

    void MCPPage::RefreshUI()
    {
        auto settings = DBModels::AppSettings::Load();

        // Populate controls from persisted settings.
        HttpToggle().IsOn(settings.mcpHttpEnabled);
        HttpPortBox().Value(static_cast<double>(settings.mcpHttpPort));
        HttpPortRow().Visibility(settings.mcpHttpEnabled
            ? mux::Visibility::Visible : mux::Visibility::Collapsed);

        QpmBox().Value(static_cast<double>(settings.mcpQueriesPerMinute));
        QphBox().Value(static_cast<double>(settings.mcpQueriesPerHour));
        WpmBox().Value(static_cast<double>(settings.mcpWritesPerMinute));
        DdlBox().Value(static_cast<double>(settings.mcpDdlPerMinute));
        RetentionBox().Value(static_cast<double>(settings.mcpAuditRetentionDays));
        MaxSizeBox().Value(static_cast<double>(settings.mcpAuditMaxSizeMB));

        // Count connections per MCP mode.
        int locked = 0, ro = 0, rw = 0;
        auto configs = DBModels::ConnectionStore::Load();
        for (const auto& c : configs)
        {
            switch (c.mcpMode)
            {
                case DBModels::MCPConnectionMode::Locked:    ++locked; break;
                case DBModels::MCPConnectionMode::ReadOnly:  ++ro;     break;
                case DBModels::MCPConnectionMode::ReadWrite: ++rw;     break;
            }
        }
        LockedCount().Text(winrt::hstring(std::to_wstring(locked)));
        ReadOnlyCount().Text(winrt::hstring(std::to_wstring(ro)));
        ReadWriteCount().Text(winrt::hstring(std::to_wstring(rw)));

        // Server state.
        auto* srv = DBModels::MCPServerHost::instance();
        const bool running = srv && srv->isRunning();
        ApplyStartStopButton(running);
        if (running)
        {
            const auto tools = srv->toolRegistry().definitions().size();
            ToolsCountText().Text(winrt::hstring(std::to_wstring(tools) + L" tools"));
        }
        else
        {
            ToolsCountText().Text(L"0 tools (server stopped)");
        }

        // Setup tab — show the stdio config snippet for Claude Desktop.
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exe(exePath);
        // Escape backslashes for JSON embedding.
        std::wstring escaped;
        escaped.reserve(exe.size() * 2);
        for (wchar_t c : exe) { if (c == L'\\') escaped += L"\\\\"; else escaped += c; }

        std::wstring cfg =
            L"{\n"
            L"  \"mcpServers\": {\n"
            L"    \"gridex\": {\n"
            L"      \"command\": \"" + escaped + L"\",\n"
            L"      \"args\": [\"--mcp-stdio\"]\n"
            L"    }\n"
            L"  }\n"
            L"}";
        SetupConfigBox().Text(winrt::hstring(cfg));

        SetupPathText().Text(
            L"Default Claude Desktop config: %APPDATA%\\Claude\\claude_desktop_config.json");

        ActivityPathText().Text(
            L"Audit log: %APPDATA%\\Gridex\\mcp-audit.jsonl");
    }

    void MCPPage::ApplyStartStopButton(bool running)
    {
        StartStopBtn().Content(
            winrt::box_value(winrt::hstring(running ? L"Stop Server" : L"Start Server")));
        StatusText().Text(running ? L"Running" : L"Stopped");
        RunningTitle().Text(running
            ? L"MCP Server is running" : L"MCP Server is stopped");
        RunningSubtitle().Text(running
            ? L"AI clients can now see Gridex as an MCP server."
            : L"Enable below to let AI clients connect.");
        StatusDot().Fill(
            mux::Media::SolidColorBrush(
                winrt::Windows::UI::Colors::Gray()));
        // Green when running.
        if (running)
        {
            StatusDot().Fill(
                mux::Media::SolidColorBrush(
                    winrt::Windows::UI::ColorHelper::FromArgb(255, 76, 175, 80)));
        }
    }

    void MCPPage::Back_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto frame = this->Frame())
        {
            if (frame.CanGoBack()) frame.GoBack();
        }
    }

    void MCPPage::StartStop_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto settings = DBModels::AppSettings::Load();
        auto* srv = DBModels::MCPServerHost::instance();

        if (srv && srv->isRunning())
        {
            // Stop.
            DBModels::MCPServerHost::stop();
            settings.mcpEnabled = false;
            settings.Save();
        }
        else
        {
            // Start — always in HttpOnly mode from the GUI. The
            // --mcp-stdio CLI path uses a separate bootstrap.
            DBModels::MCPServerHost::ensureCreated(
                settings,
                versionUtf8(),
                DBModels::MCPTransportMode::HttpOnly);

            // Hook XamlRoot + dispatcher so approval dialogs can open.
            auto* running = DBModels::MCPServerHost::instance();
            if (running)
            {
                auto content = this->XamlRoot();
                // Forward pointers as void* — MCPApprovalGate stores
                // winrt handles internally. We pass the same object
                // each time; lifetime outlives the server.
                static winrt::Microsoft::UI::Dispatching::DispatcherQueue dq{ nullptr };
                static winrt::Microsoft::UI::Xaml::XamlRoot xr{ nullptr };
                dq = this->DispatcherQueue();
                xr = content;
                running->setUIContext(&dq, &xr);
            }

            DBModels::MCPServerHost::start();
            settings.mcpEnabled = true;
            settings.Save();
        }
        RefreshUI();
    }

    void MCPPage::HttpToggle_Toggled(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        HttpPortRow().Visibility(HttpToggle().IsOn()
            ? mux::Visibility::Visible : mux::Visibility::Collapsed);
    }

    void MCPPage::CopyConfig_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        wadt::DataPackage pkg;
        pkg.SetText(SetupConfigBox().Text());
        wadt::Clipboard::SetContent(pkg);
    }

    void MCPPage::SaveConfig_Click(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto s = DBModels::AppSettings::Load();
        s.mcpHttpEnabled = HttpToggle().IsOn();
        s.mcpHttpPort    = static_cast<int>(HttpPortBox().Value());
        s.mcpQueriesPerMinute = static_cast<int>(QpmBox().Value());
        s.mcpQueriesPerHour   = static_cast<int>(QphBox().Value());
        s.mcpWritesPerMinute  = static_cast<int>(WpmBox().Value());
        s.mcpDdlPerMinute     = static_cast<int>(DdlBox().Value());
        s.mcpAuditRetentionDays = static_cast<int>(RetentionBox().Value());
        s.mcpAuditMaxSizeMB     = static_cast<int>(MaxSizeBox().Value());
        s.Save();
        SaveConfigStatusText().Text(L"Saved. Restart the server to apply rate-limit / audit changes.");
    }
}
