#pragma once

#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>
#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;

namespace gridex {

class AIChatView;
class SecretStore;
class WorkspaceState;

// Right sidebar panel matching macOS DetailsPanel:
//   [Details | Assistant] tab bar
//   Details tab: search + selected row field list (column: value pairs)
//   Assistant tab: AIChatView
class DetailsPanel : public QWidget {
    Q_OBJECT

public:
    explicit DetailsPanel(SecretStore* secretStore,
                          WorkspaceState* state,
                          QWidget* parent = nullptr);

    struct FieldEntry {
        std::string column;
        std::string value;
    };

public slots:
    void setSelectedRow(const std::vector<FieldEntry>& fields);
    void clearSelectedRow();

signals:
    // Emitted when user edits a field value in the Details tab.
    // columnIndex is the position in the current fields array.
    void fieldEdited(int columnIndex, const QString& newValue);

private slots:
    void onTabClicked(int index);
    void onSearchChanged(const QString& text);

private:
    void buildUi();
    void rebuildDetailsList();

    int activeTab_ = 0;

    // Tab bar
    QPushButton* detailsTabBtn_ = nullptr;
    QPushButton* assistantTabBtn_ = nullptr;

    // Stacked content
    QStackedWidget* stack_ = nullptr;

    // Details tab
    QWidget*     detailsPage_  = nullptr;
    QLineEdit*   searchEdit_   = nullptr;
    QScrollArea* scrollArea_   = nullptr;
    QWidget*     fieldsHost_   = nullptr;
    QVBoxLayout* fieldsLayout_ = nullptr;
    QLabel*      emptyLabel_   = nullptr;

    // Assistant tab
    AIChatView* chatView_ = nullptr;

    std::vector<FieldEntry> currentFields_;
};

}
