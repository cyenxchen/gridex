#pragma once

#include "MCPPage.g.h"

namespace winrt::Gridex::implementation
{
    // MCPPage — Gridex MCP Server dashboard (5 tabs, mirrors macOS).
    // Overview tab is fully wired; Connections / Activity / Setup / Config
    // have minimum viable bodies and will grow in follow-up sprints.
    struct MCPPage : MCPPageT<MCPPage>
    {
        MCPPage();

        void Back_Click(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        void StartStop_Click(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        void HttpToggle_Toggled(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        void CopyConfig_Click(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        void SaveConfig_Click(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        void RefreshUI();
        void ApplyStartStopButton(bool running);
    };
}

namespace winrt::Gridex::factory_implementation
{
    struct MCPPage : MCPPageT<MCPPage, implementation::MCPPage> {};
}
