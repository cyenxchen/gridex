#pragma once

#include <QWidget>

class QLineEdit;
class QComboBox;
class QTableWidget;
class QPushButton;
class QWidget;
class QVBoxLayout;
class QLabel;

namespace gridex {

class MCPWindowState;

class MCPActivityView : public QWidget {
    Q_OBJECT
public:
    explicit MCPActivityView(MCPWindowState* state, QWidget* parent = nullptr);

private slots:
    void refresh();
    void onFilterChanged();
    void onSelectionChanged();
    void onRefreshClicked();
    void onClearClicked();
    void onExportClicked();

private:
    void buildUi();
    void rebuildDetail();

    MCPWindowState* state_;
    QLineEdit*    search_ = nullptr;
    QComboBox*    toolFilter_ = nullptr;
    QComboBox*    statusFilter_ = nullptr;
    QTableWidget* table_ = nullptr;

    QWidget*      detailPanel_ = nullptr;
    QVBoxLayout*  detailLayout_ = nullptr;
    QLabel*       detailPlaceholder_ = nullptr;

    int selectedRow_ = -1;
};

}  // namespace gridex
