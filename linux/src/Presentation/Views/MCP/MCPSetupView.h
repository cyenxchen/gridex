#pragma once

#include <QWidget>

#include <QComboBox>
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace gridex {

class MCPSetupView : public QWidget {
    Q_OBJECT
public:
    explicit MCPSetupView(QWidget* parent = nullptr);

private slots:
    void onClientChanged();
    void onCopyClicked();
    void onInstallClicked();
    void onOpenPathClicked();

private:
    void buildUi();
    QString configPathForSelected() const;
    QString configJsonForSelected() const;

    QComboBox* clientCombo_ = nullptr;
    QLabel* configPathLabel_ = nullptr;
    QPlainTextEdit* jsonPreview_ = nullptr;
    QPushButton* installBtn_ = nullptr;
    QPushButton* copyBtn_ = nullptr;
};

}  // namespace gridex
