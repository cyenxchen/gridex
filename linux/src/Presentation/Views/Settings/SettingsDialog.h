#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace gridex {

class SecretStore;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(SecretStore* secretStore, QWidget* parent = nullptr);

private slots:
    void onProviderChanged(const QString& provider);
    void onModelChanged(const QString& modelName);
    void onSaveClicked();
    void onKeyEditTimeout();

private:
    void buildUi();
    void buildAiPage(QWidget* page);
    void buildAppearancePage(QWidget* page);
    void loadForProvider(const QString& provider);
    void fetchModels(const QString& provider, const QString& apiKey);

    SecretStore* secretStore_ = nullptr;

    QListWidget*    navList_  = nullptr;
    QStackedWidget* pages_    = nullptr;

    // AI page controls.
    QComboBox*    providerCombo_ = nullptr;
    QComboBox*    modelCombo_    = nullptr;
    QLineEdit*    apiKeyEdit_    = nullptr;
    QLineEdit*    endpointEdit_  = nullptr;
    QProgressBar* modelSpinner_  = nullptr;
    QLabel*       modelStatus_   = nullptr;
    QPushButton*  saveBtn_       = nullptr;

    QTimer* keyDebounce_ = nullptr;

    // Appearance page controls.
    QComboBox* themeCombo_ = nullptr;
};

}  // namespace gridex
