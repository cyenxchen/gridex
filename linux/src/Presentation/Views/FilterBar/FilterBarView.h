#pragma once

#include <QWidget>
#include <vector>
#include <string>

#include "Core/Models/Query/FilterExpression.h"

class QComboBox;
class QLineEdit;
class QPushButton;

namespace gridex {

// Filter bar matching macOS layout:
//   [Any column ▾] [= ▾] [value input] [Apply] [Clear]
// Emits filterApplied(FilterExpression) or filterCleared().
// Column list is updated via setColumns() when a table loads.
class FilterBarView : public QWidget {
    Q_OBJECT

public:
    explicit FilterBarView(QWidget* parent = nullptr);

    // Populate column combo from query result headers.
    void setColumns(const std::vector<std::string>& columns);

    // Reset inputs to defaults.
    void reset();

signals:
    void filterApplied(const gridex::FilterExpression& expr);
    void filterCleared();

private slots:
    void onApply();
    void onClear();
    void onOperatorChanged(int index);

private:
    void buildUi();

    QComboBox*   columnCombo_   = nullptr;
    QComboBox*   operatorCombo_ = nullptr;
    QLineEdit*   valueEdit_     = nullptr;
    QPushButton* applyBtn_      = nullptr;
    QPushButton* clearBtn_      = nullptr;
};

}
