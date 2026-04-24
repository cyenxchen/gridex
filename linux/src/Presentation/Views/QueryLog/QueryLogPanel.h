#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace gridex {

// Collapsible SQL log panel — shows executed queries with timestamps.
// Lives at the bottom of DataGridView below the table.
// Toggle visibility via the collapse button in the header row.
class QueryLogPanel : public QWidget {
    Q_OBJECT

public:
    explicit QueryLogPanel(QWidget* parent = nullptr);

    // Append a query entry; called after every adapter execution.
    void appendQuery(const QString& sql, int durationMs);

    // Expand / collapse the body (plain text area).
    void setExpanded(bool expanded);
    [[nodiscard]] bool isExpanded() const noexcept { return expanded_; }

signals:
    void toggleRequested();

private:
    void buildUi();

    bool expanded_ = true;

    QWidget*       header_    = nullptr;
    QPushButton*   toggleBtn_ = nullptr;
    QLabel*        titleLabel_ = nullptr;
    QPlainTextEdit* logEdit_  = nullptr;
};

}
