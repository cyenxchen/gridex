#pragma once

#include <QDialog>
#include <array>
#include <optional>
#include <string>

#include "Core/Models/Database/ConnectionConfig.h"

class QCheckBox;
class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

namespace gridex {

// Matches macOS ConnectionFormView layout:
//   Name
//   Status color (6 color dots + Tag dropdown)
//   SQLite: File + Browse
//   Others:
//     Host/Socket + Port (same row)
//     User + Other options
//     Password + Store in keychain
//     Database + SSL mode
//     SSL keys (Key/Cert/CA Cert + clear)
//     Optional SSH section: SSH Host+Port, SSH User+Auth, SSH Password/Key
//   Divider
//   Bottom bar:  [Over SSH]                    [Save] [Test] [Connect]
class ConnectionFormDialog : public QDialog {
    Q_OBJECT

public:
    // Result codes returned by exec().
    enum Result {
        Cancelled = QDialog::Rejected,
        Saved     = QDialog::Accepted,
        Connect   = 2,   // save + connect
    };

    explicit ConnectionFormDialog(QWidget* parent = nullptr);

    void setConfig(const ConnectionConfig& config);
    void setPassword(const std::string& password);

    [[nodiscard]] ConnectionConfig config() const;
    [[nodiscard]] std::optional<std::string> password() const;
    [[nodiscard]] std::optional<std::string> sshPassword() const;
    [[nodiscard]] bool storeInKeychain() const;

    void setSshPassword(const std::string& password);

signals:
    void testRequested(const ConnectionConfig& config,
                       const std::optional<std::string>& password);

public slots:
    void showTestResult(bool success, const QString& detail);

private slots:
    void onDatabaseTypeChanged(int index);
    void onBrowseFile();
    void onColorDotClicked(int index);
    void onToggleSSH();
    void onSSHAuthChanged(int index);
    void onFieldChanged();
    void onBrowseSSHKey();
    void onBrowseSSLKey(int slot);
    void onClearSSLKeys();
    void onSaveClicked();
    void onTestClicked();
    void onConnectClicked();

private:
    void buildUi();
    void applyLayoutForType(DatabaseType type);
    void updateButtonStates();
    void refreshColorDotBorders();
    void hideAllSSHRows();
    void rebuildSSHRows();

    // Top-of-dialog fields
    QLineEdit*   nameEdit_   = nullptr;
    QComboBox*   typeCombo_  = nullptr;
    QComboBox*   colorCombo_ = nullptr;
    std::array<QPushButton*, 6> colorDots_{};

    // SQLite page
    QWidget*     filePage_    = nullptr;
    QLineEdit*   filePathEdit_ = nullptr;
    QPushButton* filePickBtn_ = nullptr;

    // Host-based page
    QWidget*     hostPage_     = nullptr;
    QLineEdit*   hostEdit_     = nullptr;
    QSpinBox*    portSpin_    = nullptr;
    QLineEdit*   userEdit_    = nullptr;
    QPushButton* otherOptsBtn_ = nullptr;
    QLineEdit*   passwordEdit_ = nullptr;
    QComboBox*   storeCombo_  = nullptr;
    QLineEdit*   databaseEdit_ = nullptr;
    QComboBox*   sslModeCombo_ = nullptr;
    QLineEdit*   sslKeyPathEdit_  = nullptr;
    QLineEdit*   sslCertPathEdit_ = nullptr;
    QLineEdit*   sslCAPathEdit_   = nullptr;

    // SSH section (inside host page)
    QWidget*     sshSection_  = nullptr;
    QLineEdit*   sshHostEdit_ = nullptr;
    QSpinBox*    sshPortSpin_ = nullptr;
    QLineEdit*   sshUserEdit_ = nullptr;
    QComboBox*   sshAuthCombo_ = nullptr;
    QLineEdit*   sshPasswordEdit_ = nullptr;
    QLineEdit*   sshKeyPathEdit_ = nullptr;
    QWidget*     sshPasswordRow_ = nullptr;
    QWidget*     sshKeyRow_      = nullptr;

    // Bottom bar
    QPushButton* overSshBtn_  = nullptr;
    QPushButton* saveBtn_     = nullptr;
    QPushButton* testBtn_     = nullptr;
    QPushButton* connectBtn_  = nullptr;
    QLabel*      testResult_  = nullptr;

    ColorTag   colorTag_ = ColorTag::Blue;
    bool       sshEnabled_ = false;
    std::string editingId_;
};

}
