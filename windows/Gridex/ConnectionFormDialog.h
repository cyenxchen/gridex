#pragma once

#include "ConnectionFormDialog.g.h"
#include "Models/ConnectionConfig.h"
#include <functional>

namespace winrt::Gridex::implementation
{
    struct ConnectionFormDialog : ConnectionFormDialogT<ConnectionFormDialog>
    {
        ConnectionFormDialog();

        void SetDatabaseType(DBModels::DatabaseType type);
        DBModels::ConnectionConfig GetConnectionConfig();
        std::wstring GetPassword();
        void SetConnectionConfig(const DBModels::ConnectionConfig& config);

        void SshToggle_Click(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void SshAuth_Changed(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        winrt::fire_and_forget BrowseFile_Click(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        winrt::fire_and_forget BrowseSshKey_Click(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void SaveButton_Click(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void TestButton_Click(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ConnectButton_Click(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        std::function<void()> OnSave;
        std::function<void()> OnTest;
        std::function<void()> OnConnect;

    private:
        DBModels::DatabaseType dbType_ = DBModels::DatabaseType::PostgreSQL;
        std::optional<DBModels::ColorTag> selectedColor_;
        void BuildColorTagPicker();
        void UpdateFieldVisibility();
        void PopulateGroupCombo();
    };
}

namespace winrt::Gridex::factory_implementation
{
    struct ConnectionFormDialog : ConnectionFormDialogT<ConnectionFormDialog, implementation::ConnectionFormDialog>
    {
    };
}
