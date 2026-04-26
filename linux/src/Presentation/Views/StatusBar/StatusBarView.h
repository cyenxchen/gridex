#pragma once

#include <QWidget>

class QLabel;

namespace gridex {

// 24px bottom bar matching macOS StatusBarSwiftUIView:
//   [connection] | [schema] | [rows] | [time]       [MCP] | [v0.1]
class StatusBarView : public QWidget {
    Q_OBJECT

public:
    explicit StatusBarView(QWidget* parent = nullptr);

public slots:
    void setConnection(const QString& text);
    void setSchema(const QString& text);
    void setRowCount(const QString& text);
    void setQueryTime(const QString& text);
    void setMcpStatus(const QString& text);

private:
    void buildUi();
    static QLabel* makeSeparator(QWidget* parent);
    static QLabel* makeItem(QWidget* parent);

    QLabel* connLabel_   = nullptr;
    QLabel* schemaLabel_ = nullptr;
    QLabel* rowsLabel_   = nullptr;
    QLabel* timeLabel_   = nullptr;
    QLabel* mcpLabel_    = nullptr;
    QLabel* versionLabel_ = nullptr;
};

}
