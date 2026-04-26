#include "Presentation/Views/QueryEditor/QueryEditorView.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QTableView>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <QVBoxLayout>

#include "Core/Errors/GridexError.h"
#include "Core/Protocols/Database/IDatabaseAdapter.h"
#include "Presentation/Views/DataGrid/QueryResultModel.h"
#include "Presentation/Views/QueryEditor/Autocomplete/AutocompleteProvider.h"
#include "Presentation/Views/QueryEditor/Autocomplete/CompletionModels.h"
#include "Presentation/Views/QueryEditor/Autocomplete/CompletionPopup.h"
#include "Presentation/Views/QueryEditor/Autocomplete/SqlContextParser.h"
#include "Presentation/Views/QueryEditor/SqlHighlighter.h"

namespace gridex {

namespace {

// QPlainTextEdit subclass that suppresses the built-in placeholder while an
// IME composition (preedit text) is active. Qt's paintEvent only checks
// document()->isEmpty() when deciding whether to draw the placeholder, not
// whether the current block has preedit text — so typing Vietnamese Telex/VNI
// leaves the placeholder overlapping the composing character until commit
// (space/punctuation). We swap placeholder text with "" for the duration of
// the paint, then restore it, so callers observe no state change.
class IMEAwareTextEdit : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;

protected:
    void paintEvent(QPaintEvent* event) override {
        const bool suppress = document()->isEmpty() && hasPreeditText();
        QString savedPlaceholder;
        if (suppress) {
            savedPlaceholder = placeholderText();
            QPlainTextEdit::setPlaceholderText(QString{});
        }
        QPlainTextEdit::paintEvent(event);
        if (suppress) {
            QPlainTextEdit::setPlaceholderText(savedPlaceholder);
        }
    }

    void inputMethodEvent(QInputMethodEvent* event) override {
        QPlainTextEdit::inputMethodEvent(event);
        // Force viewport repaint so placeholder is cleared as soon as
        // preedit appears (instead of lagging until the next paint trigger).
        viewport()->update();
    }

private:
    bool hasPreeditText() const {
        auto* layout = textCursor().block().layout();
        return layout && !layout->preeditAreaText().isEmpty();
    }
};

}  // namespace

QueryEditorView::QueryEditorView(QWidget* parent)
    : QWidget(parent),
      provider_(std::make_unique<AutocompleteProvider>()),
      parser_(std::make_unique<SqlContextParser>()) {
    buildUi();
}

QueryEditorView::~QueryEditorView() = default;

void QueryEditorView::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- Toolbar: [Run ▶] [status ...] ----
    auto* top = new QWidget(this);
    top->setFixedHeight(32);
    top->setAutoFillBackground(true);
    auto* topH = new QHBoxLayout(top);
    topH->setContentsMargins(8, 0, 12, 0);
    topH->setSpacing(10);

    runBtn_ = new QPushButton(tr("▶ Run"), top);
    runBtn_->setObjectName(QStringLiteral("primaryButton"));  // styled in style.qss
    runBtn_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    runBtn_->setToolTip(tr("Execute query (Ctrl+Enter)"));
    connect(runBtn_, &QPushButton::clicked, this, &QueryEditorView::onRunClicked);
    topH->addWidget(runBtn_);

    statusLbl_ = new QLabel(QString{}, top);
    topH->addWidget(statusLbl_, 1);

    auto* saveBtn = new QPushButton(tr("⭐ Save"), top);
    saveBtn->setToolTip(tr("Save this query to Saved Queries"));
    connect(saveBtn, &QPushButton::clicked, this, [this] {
        const QString sql = editor_ ? editor_->toPlainText().trimmed() : QString{};
        if (!sql.isEmpty()) emit saveQueryRequested(sql);
    });
    topH->addWidget(saveBtn);

    root->addWidget(top);

    auto* topDiv = new QFrame(this);
    topDiv->setFrameShape(QFrame::HLine);
    root->addWidget(topDiv);

    // ---- Splitter: editor (top) | result grid (bottom) ----
    splitter_ = new QSplitter(Qt::Vertical, this);

    editor_ = new IMEAwareTextEdit(splitter_);
    editor_->setPlaceholderText(tr("SELECT * FROM ..."));
    editor_->setFrameShape(QFrame::NoFrame);
    editor_->setTabStopDistance(32);
    QFont mono(QStringLiteral("Monospace"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(12);
    editor_->setFont(mono);
    editor_->installEventFilter(this);
    hl_ = new SqlHighlighter(editor_->document());
    splitter_->addWidget(editor_);

    // ---- Autocomplete wiring ----
    popup_ = new CompletionPopup(this);
    popup_->hide();
    connect(popup_, &CompletionPopup::accepted, this, &QueryEditorView::acceptCompletion);
    connect(popup_, &CompletionPopup::dismissed, this, &QueryEditorView::hidePopup);

    // Debounce textChanged so we don't reparse on every keystroke for large
    // SQL files. 60ms feels instant while skipping rapid-fire edits.
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(60);
    connect(debounce_, &QTimer::timeout, this, &QueryEditorView::maybeShowCompletions);
    connect(editor_, &QPlainTextEdit::textChanged, this, [this]() {
        if (debounce_) debounce_->start();
    });

    // Result grid below editor.
    resultView_ = new QTableView(splitter_);
    resultView_->setFrameShape(QFrame::NoFrame);
    resultView_->setAlternatingRowColors(true);
    resultView_->setShowGrid(true);
    resultView_->setSelectionBehavior(QAbstractItemView::SelectItems);
    resultView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultView_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    resultView_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    resultView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    resultView_->horizontalHeader()->setStretchLastSection(true);
    resultView_->verticalHeader()->setDefaultSectionSize(30);

    resultModel_ = new QueryResultModel(this);
    resultView_->setModel(resultModel_);
    splitter_->addWidget(resultView_);

    // ---- Export button overlay at bottom-right of result table ----
    exportResultMenu_ = new QMenu(resultView_);
    auto* actCsv  = exportResultMenu_->addAction(tr("Export as CSV..."));
    auto* actSql  = exportResultMenu_->addAction(tr("Export as SQL INSERT..."));
    auto* actJson = exportResultMenu_->addAction(tr("Export as JSON..."));
    connect(actCsv,  &QAction::triggered, this, &QueryEditorView::exportResultAsCsv);
    connect(actSql,  &QAction::triggered, this, &QueryEditorView::exportResultAsSql);
    connect(actJson, &QAction::triggered, this, &QueryEditorView::exportResultAsJson);

    exportResultBtn_ = new QPushButton(QStringLiteral("⇩ Export ▾"), resultView_);
    exportResultBtn_->setFixedSize(92, 26);
    exportResultBtn_->setToolTip(tr("Export query result"));
    exportResultBtn_->setCursor(Qt::PointingHandCursor);
    exportResultBtn_->setMenu(exportResultMenu_);
    exportResultBtn_->hide();  // shown when result has rows
    // Reposition on result view resize. Install filter on resultView_ so we
    // catch QEvent::Resize regardless of who triggered it (splitter, window).
    resultView_->installEventFilter(this);
    // Show/hide button when model data changes.
    connect(resultModel_, &QAbstractTableModel::modelReset, this, [this]() {
        const bool hasData = resultModel_->rowCount() > 0;
        exportResultBtn_->setVisible(hasData);
        if (hasData) {
            // Position: bottom-right with 14px margin to clear scrollbars.
            const int x = resultView_->width()  - exportResultBtn_->width()  - 14;
            const int y = resultView_->height() - exportResultBtn_->height() - 14;
            exportResultBtn_->move(x, y);
            exportResultBtn_->raise();
        }
    });

    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 2);
    splitter_->setSizes({200, 400});

    root->addWidget(splitter_, 1);
}

void QueryEditorView::setSql(const QString& sql) {
    if (editor_) editor_->setPlainText(sql);
}

void QueryEditorView::setAdapter(IDatabaseAdapter* adapter) {
    adapter_ = adapter;
    runBtn_->setEnabled(adapter_ != nullptr);
    if (!adapter_) {
        resultModel_->clear();
        statusLbl_->setText(QString{});
        if (provider_) provider_->updateSchema({});
        return;
    }
    reloadSchema();
}

void QueryEditorView::reloadSchema() {
    if (!adapter_ || !provider_) return;

    // Fetch listTables + describeTable on a worker thread so the UI doesn't
    // stall on large schemas. Result is applied back on the GUI thread.
    IDatabaseAdapter* adapter = adapter_;
    QPointer<QueryEditorView> self(this);
    (void)QtConcurrent::run([self, adapter]() {
        std::vector<TableDescription> tables;
        try {
            const auto infos = adapter->listTables(std::nullopt);
            for (const auto& info : infos) {
                if (info.type != TableKind::Table) continue;
                try {
                    tables.push_back(adapter->describeTable(info.name, std::nullopt));
                } catch (...) { /* skip broken tables */ }
            }
        } catch (...) {
            // Best-effort. Empty schema is valid (just no suggestions).
        }
        QMetaObject::invokeMethod(qApp, [self, tables]() {
            if (self && self->provider_) self->provider_->updateSchema(tables);
        }, Qt::QueuedConnection);
    });
}

void QueryEditorView::hidePopup() {
    if (debounce_) debounce_->stop();  // cancel pending re-show
    if (popup_) popup_->hide();
}

void QueryEditorView::triggerCompletionNow() {
    // Force a fresh context computation regardless of prefix length.
    if (!provider_) return;
    const auto text = editor_->toPlainText();
    const int offset = editor_->textCursor().position();
    auto ctx = parser_->parse(text, offset);
    auto items = provider_->suggestions(ctx);
    if (items.empty()) { hidePopup(); return; }
    popup_->setItems(items);

    // Position popup just below the cursor.
    const auto rect = editor_->cursorRect();
    const QPoint gpos = editor_->viewport()->mapToGlobal(
        QPoint(rect.left(), rect.bottom() + 2));
    popup_->move(gpos);
    popup_->show();
    // X11/Wayland may pull focus to the newly-shown tool window despite
    // WA_ShowWithoutActivating — force focus back so keystrokes keep going
    // to the editor for inline filtering of the suggestion list.
    editor_->setFocus(Qt::OtherFocusReason);
}

void QueryEditorView::maybeShowCompletions() {
    if (!provider_) return;

    // Respect 2-char auto-trigger rule: only auto-popup when prefix has >=2
    // chars. Ctrl+Space still forces via triggerCompletionNow().
    const auto text = editor_->toPlainText();
    const int offset = editor_->textCursor().position();
    const auto ctx = parser_->parse(text, offset);
    if (ctx.prefix.size() < 2 && ctx.trigger.kind != CompletionTriggerKind::None) {
        hidePopup();
        return;
    }
    auto items = provider_->suggestions(ctx);
    if (items.empty()) { hidePopup(); return; }
    popup_->setItems(items);

    const auto rect = editor_->cursorRect();
    const QPoint gpos = editor_->viewport()->mapToGlobal(
        QPoint(rect.left(), rect.bottom() + 2));
    popup_->move(gpos);
    if (!popup_->isVisible()) {
        popup_->show();
        editor_->setFocus(Qt::OtherFocusReason);
    }
}

void QueryEditorView::acceptCompletion(const CompletionItem& item) {
    if (!editor_) return;
    // Replace current word (the in-progress prefix) with item.insertText.
    auto cursor = editor_->textCursor();
    // Walk back while previous char is [A-Za-z0-9_.]
    const QString doc = editor_->toPlainText();
    int pos = cursor.position();
    int start = pos;
    while (start > 0) {
        const QChar ch = doc.at(start - 1);
        if (ch.isLetterOrNumber() || ch == '_' || ch == '.') --start; else break;
    }
    cursor.setPosition(start, QTextCursor::MoveAnchor);
    cursor.setPosition(pos,   QTextCursor::KeepAnchor);
    cursor.insertText(item.insertText);

    if (provider_) provider_->trackUsed(item.text);
    hidePopup();
}

void QueryEditorView::onRunClicked() {
    if (!adapter_) return;
    const auto sql = editor_->toPlainText().trimmed().toStdString();
    if (sql.empty()) return;

    statusLbl_->setText(tr("Running…"));

    QElapsedTimer timer;
    timer.start();
    try {
        auto result = adapter_->executeRaw(sql);
        const int n   = static_cast<int>(result.rows.size());
        const int cols = static_cast<int>(result.columns.size());
        const int ms  = static_cast<int>(timer.elapsed());
        resultModel_->setResult(std::move(result));
        resultView_->resizeColumnsToContents();

        // Stretch last column to fill viewport when total column width < view width.
        if (auto* h = resultView_->horizontalHeader()) {
            const int cc = resultModel_->columnCount();
            if (cc > 0) {
                int used = 0;
                for (int i = 0; i < cc - 1; ++i) used += h->sectionSize(i);
                const int avail = resultView_->viewport()->width() - used;
                if (avail > h->sectionSize(cc - 1))
                    h->resizeSection(cc - 1, avail);
            }
        }

        statusLbl_->setStyleSheet(QStringLiteral("color: #a6e3a1;"));  // success (catppuccin green)
        statusLbl_->setText(tr("%1 rows × %2 cols · %3 ms").arg(n).arg(cols).arg(ms));
        emit queryExecuted(QString::fromStdString(sql), n, ms);
    } catch (const GridexError& e) {
        resultModel_->clear();
        statusLbl_->setStyleSheet(QStringLiteral("color: #f38ba8;"));  // error (catppuccin red)
        statusLbl_->setText(QString::fromUtf8(e.what()));
    }
}

bool QueryEditorView::eventFilter(QObject* obj, QEvent* event) {
    // Reposition export overlay when the result view resizes.
    if (obj == resultView_ && event->type() == QEvent::Resize
        && exportResultBtn_ && exportResultBtn_->isVisible()) {
        const int x = resultView_->width()  - exportResultBtn_->width()  - 14;
        const int y = resultView_->height() - exportResultBtn_->height() - 14;
        exportResultBtn_->move(x, y);
    }

    if (obj == editor_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);

        // Ctrl+Enter -> Run (takes priority over popup).
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && (ke->modifiers() & Qt::ControlModifier)) {
            onRunClicked();
            return true;
        }

        // Ctrl+Space -> force-open completion popup.
        if (ke->key() == Qt::Key_Space && (ke->modifiers() & Qt::ControlModifier)) {
            triggerCompletionNow();
            return true;
        }

        // While popup is visible, intercept navigation/accept/dismiss keys.
        if (popup_ && popup_->isVisible()) {
            switch (ke->key()) {
                case Qt::Key_Up:     popup_->moveSelection(-1); return true;
                case Qt::Key_Down:   popup_->moveSelection(+1); return true;
                case Qt::Key_Escape: hidePopup(); return true;
                case Qt::Key_Return:
                case Qt::Key_Enter:
                case Qt::Key_Tab:
                    if (auto* it = popup_->selectedItem()) {
                        acceptCompletion(*it);
                    }
                    return true;
                default:
                    break;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

// --------------------------------------------------------------------
// Result export
// --------------------------------------------------------------------

namespace {

// CSV cell: quote if value contains separator, quote char, CR/LF.
// Quote char inside cell is doubled. Null -> empty cell (standard CSV).
QString csvCell(const RowValue& v) {
    if (v.isNull()) return QString{};
    const QString s = QString::fromStdString(v.displayString());
    if (s.contains(',') || s.contains('"') || s.contains('\n') || s.contains('\r')) {
        QString escaped = s;
        escaped.replace('"', QStringLiteral("\"\""));
        return '"' + escaped + '"';
    }
    return s;
}

// SQL literal: NULL, numbers unquoted, strings with single-quote escape.
// Conservative: treat everything except NULL as string-literal. DB engines
// will coerce when inserting.
QString sqlLiteral(const RowValue& v) {
    if (v.isNull()) return QStringLiteral("NULL");
    const QString s = QString::fromStdString(v.displayString());
    // Numbers/booleans pass through unquoted.
    bool ok = false;
    s.toDouble(&ok);
    if (ok) return s;
    if (s == QLatin1String("true") || s == QLatin1String("false")) return s;
    QString escaped = s;
    escaped.replace('\'', QStringLiteral("''"));
    return '\'' + escaped + '\'';
}

// Default filename from the first column's table name if present, else
// "query-result".
QString defaultExportStem(const QueryResult& r) {
    for (const auto& c : r.columns) {
        if (c.tableName && !c.tableName->empty()) {
            return QString::fromStdString(*c.tableName);
        }
    }
    return QStringLiteral("query-result");
}

}  // namespace

void QueryEditorView::exportResultAsCsv() {
    const auto& r = resultModel_->result();
    if (r.rows.empty()) return;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString defaultPath = dir + "/" + defaultExportStem(r) + ".csv";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Result as CSV"), defaultPath, tr("CSV (*.csv)"));
    if (path.isEmpty()) return;

    const QString outPath = path.endsWith(".csv", Qt::CaseInsensitive) ? path : path + ".csv";
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export CSV"),
                             tr("Cannot open file for writing:\n%1").arg(outPath));
        return;
    }
    QTextStream out(&f);

    // Header
    QStringList header;
    for (const auto& c : r.columns) header << QString::fromStdString(c.name);
    out << header.join(',') << '\n';

    // Rows
    for (const auto& row : r.rows) {
        QStringList cells;
        cells.reserve(static_cast<int>(row.size()));
        for (const auto& v : row) cells << csvCell(v);
        out << cells.join(',') << '\n';
    }
}

void QueryEditorView::exportResultAsSql() {
    const auto& r = resultModel_->result();
    if (r.rows.empty()) return;

    const QString tableName = defaultExportStem(r);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString defaultPath = dir + "/" + tableName + ".sql";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Result as SQL"), defaultPath, tr("SQL (*.sql)"));
    if (path.isEmpty()) return;

    const QString outPath = path.endsWith(".sql", Qt::CaseInsensitive) ? path : path + ".sql";
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export SQL"),
                             tr("Cannot open file for writing:\n%1").arg(outPath));
        return;
    }
    QTextStream out(&f);

    // Column list: "col1", "col2", ...
    QStringList colList;
    for (const auto& c : r.columns) colList << '"' + QString::fromStdString(c.name) + '"';
    const QString colsJoined = colList.join(", ");
    const QString header = QString("INSERT INTO \"%1\" (%2) VALUES").arg(tableName, colsJoined);

    for (const auto& row : r.rows) {
        QStringList vals;
        vals.reserve(static_cast<int>(row.size()));
        for (const auto& v : row) vals << sqlLiteral(v);
        out << header << " (" << vals.join(", ") << ");\n";
    }
}

void QueryEditorView::exportResultAsJson() {
    const auto& r = resultModel_->result();
    if (r.rows.empty()) return;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString defaultPath = dir + "/" + defaultExportStem(r) + ".json";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Result as JSON"), defaultPath, tr("JSON (*.json)"));
    if (path.isEmpty()) return;

    const QString outPath = path.endsWith(".json", Qt::CaseInsensitive) ? path : path + ".json";
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export JSON"),
                             tr("Cannot open file for writing:\n%1").arg(outPath));
        return;
    }

    QJsonArray rowsArr;
    for (const auto& row : r.rows) {
        QJsonObject obj;
        for (std::size_t c = 0; c < r.columns.size() && c < row.size(); ++c) {
            const QString key = QString::fromStdString(r.columns[c].name);
            const auto& v = row[c];
            if (v.isNull()) obj.insert(key, QJsonValue::Null);
            else obj.insert(key, QString::fromStdString(v.displayString()));
        }
        rowsArr.append(obj);
    }
    QJsonDocument doc(rowsArr);
    f.write(doc.toJson(QJsonDocument::Indented));
}

}
