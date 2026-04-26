#pragma once

#include <QWidget>
#include <memory>

class QLabel;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTableView;
class QTimer;

namespace gridex {

class IDatabaseAdapter;
class QueryResultModel;
class SqlHighlighter;
class AutocompleteProvider;
class CompletionPopup;
class SqlContextParser;
struct CompletionItem;

// Split view: SQL editor on top + result grid on bottom.
// Ctrl+Enter / Run button executes the current text via adapter.executeRaw.
class QueryEditorView : public QWidget {
    Q_OBJECT

public:
    explicit QueryEditorView(QWidget* parent = nullptr);
    ~QueryEditorView() override;

    void setAdapter(IDatabaseAdapter* adapter);
    void setSql(const QString& sql);

signals:
    void queryExecuted(const QString& sql, int rowCount, int durationMs);
    void saveQueryRequested(const QString& sql);

private slots:
    void onRunClicked();
    void maybeShowCompletions();
    void exportResultAsCsv();
    void exportResultAsSql();
    void exportResultAsJson();

private:
    void buildUi();
    bool eventFilter(QObject* obj, QEvent* event) override;

    // Autocomplete helpers
    void triggerCompletionNow();              // force (Ctrl+Space)
    void hidePopup();
    void acceptCompletion(const CompletionItem& item);
    void reloadSchema();                      // async fetch via adapter

    IDatabaseAdapter* adapter_ = nullptr;

    QPlainTextEdit*   editor_      = nullptr;
    SqlHighlighter*   hl_          = nullptr;
    QPushButton*      runBtn_      = nullptr;
    QLabel*           statusLbl_   = nullptr;
    QSplitter*        splitter_    = nullptr;
    QTableView*       resultView_  = nullptr;
    QueryResultModel* resultModel_ = nullptr;

    QPushButton*      exportResultBtn_  = nullptr;
    QMenu*            exportResultMenu_ = nullptr;

    std::unique_ptr<AutocompleteProvider> provider_;
    std::unique_ptr<SqlContextParser>     parser_;
    CompletionPopup*  popup_       = nullptr;
    QTimer*           debounce_    = nullptr;
};

}
