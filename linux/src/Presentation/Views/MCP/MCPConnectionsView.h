#pragma once

#include <QWidget>

class QLineEdit;
class QComboBox;
class QLabel;
class QTableWidget;

namespace gridex {

class MCPWindowState;

class MCPConnectionsView : public QWidget {
    Q_OBJECT
public:
    explicit MCPConnectionsView(MCPWindowState* state, QWidget* parent = nullptr);

private slots:
    void refresh();
    void onFilterChanged();
    void onRowModeChanged(int row, int comboIndex);

private:
    void buildUi();

    MCPWindowState* state_;
    QLineEdit*    search_ = nullptr;
    QComboBox*    modeFilter_ = nullptr;
    QLabel*       countLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
};

}  // namespace gridex
