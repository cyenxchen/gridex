#pragma once

#include <QWidget>

class QSpinBox;
class QComboBox;
class QCheckBox;
class QPushButton;

namespace gridex {

class MCPWindowState;

class MCPAdvancedView : public QWidget {
    Q_OBJECT
public:
    explicit MCPAdvancedView(MCPWindowState* state, QWidget* parent = nullptr);

private slots:
    void onValueChanged();
    void onResetClicked();

private:
    void buildUi();
    void loadFromSettings();
    void saveToSettings();
    void applyLimits();

    MCPWindowState* state_;

    QSpinBox* queriesPerMinute_ = nullptr;
    QSpinBox* queriesPerHour_   = nullptr;
    QSpinBox* writesPerMinute_  = nullptr;
    QSpinBox* ddlPerMinute_     = nullptr;

    QSpinBox* queryTimeout_      = nullptr;
    QSpinBox* approvalTimeout_   = nullptr;
    QSpinBox* connectionTimeout_ = nullptr;

    QComboBox* retention_ = nullptr;
    QSpinBox*  maxSizeMB_ = nullptr;

    QCheckBox* requireApprovalWrites_ = nullptr;
    QCheckBox* allowRemoteHttp_       = nullptr;
};

}  // namespace gridex
