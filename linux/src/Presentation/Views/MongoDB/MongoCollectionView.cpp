#include "Presentation/Views/MongoDB/MongoCollectionView.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

#include "Data/Adapters/MongoDB/MongodbAdapter.h"

namespace gridex {

MongoCollectionView::MongoCollectionView(MongodbAdapter* adapter,
                                          const QString& collection,
                                          QWidget* parent)
    : QWidget(parent), adapter_(adapter), collection_(collection) {
    buildUi();
    reload();
}

void MongoCollectionView::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Toolbar
    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(40);
    auto* th = new QHBoxLayout(toolbar);
    th->setContentsMargins(8, 4, 8, 4);
    th->setSpacing(6);

    titleLabel_ = new QLabel(collection_, toolbar);
    titleLabel_->setStyleSheet(QStringLiteral("font-weight: bold;"));
    th->addWidget(titleLabel_);

    th->addSpacing(8);

    filterEdit_ = new QLineEdit(toolbar);
    filterEdit_->setPlaceholderText(tr(R"(Filter JSON, e.g. {"name":"foo"})"));
    filterEdit_->setMinimumWidth(200);
    connect(filterEdit_, &QLineEdit::returnPressed, this, &MongoCollectionView::onRunFilter);
    th->addWidget(filterEdit_, 1);

    auto* limitLabel = new QLabel(tr("Limit:"), toolbar);
    th->addWidget(limitLabel);

    limitSpin_ = new QSpinBox(toolbar);
    limitSpin_->setRange(1, 1000);
    limitSpin_->setValue(50);
    limitSpin_->setFixedWidth(64);
    th->addWidget(limitSpin_);

    runBtn_ = new QPushButton(tr("Run"), toolbar);
    runBtn_->setFixedWidth(48);
    connect(runBtn_, &QPushButton::clicked, this, &MongoCollectionView::onRunFilter);
    th->addWidget(runBtn_);

    th->addSpacing(8);

    refreshBtn_ = new QPushButton(tr("⟳"), toolbar);
    refreshBtn_->setToolTip(tr("Refresh"));
    refreshBtn_->setFixedWidth(32);
    connect(refreshBtn_, &QPushButton::clicked, this, &MongoCollectionView::onRefresh);
    th->addWidget(refreshBtn_);

    insertBtn_ = new QPushButton(tr("+ Insert"), toolbar);
    connect(insertBtn_, &QPushButton::clicked, this, &MongoCollectionView::onInsert);
    th->addWidget(insertBtn_);

    deleteBtn_ = new QPushButton(tr("✕ Delete"), toolbar);
    connect(deleteBtn_, &QPushButton::clicked, this, &MongoCollectionView::onDeleteSelected);
    th->addWidget(deleteBtn_);

    root->addWidget(toolbar);

    // Separator
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep);

    // Split: doc list | json editor
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(4);

    docList_ = new QListWidget(splitter);
    docList_->setMinimumWidth(200);
    connect(docList_, &QListWidget::itemClicked,
            this, &MongoCollectionView::onDocumentSelected);
    splitter->addWidget(docList_);

    auto* editorPane = new QWidget(splitter);
    auto* ev = new QVBoxLayout(editorPane);
    ev->setContentsMargins(4, 4, 4, 4);
    ev->setSpacing(4);

    jsonEditor_ = new QPlainTextEdit(editorPane);
    jsonEditor_->setPlaceholderText(tr("Select a document to edit…"));
    jsonEditor_->setFont(QFont(QStringLiteral("Monospace"), 11));
    ev->addWidget(jsonEditor_, 1);

    saveBtn_ = new QPushButton(tr("Save Document"), editorPane);
    saveBtn_->setEnabled(false);
    connect(saveBtn_, &QPushButton::clicked, this, &MongoCollectionView::onSaveDocument);
    ev->addWidget(saveBtn_);

    splitter->addWidget(editorPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 600});

    root->addWidget(splitter, 1);

    // Pagination bar
    auto* pagebar = new QWidget(this);
    pagebar->setFixedHeight(32);
    auto* ph = new QHBoxLayout(pagebar);
    ph->setContentsMargins(8, 2, 8, 2);
    ph->setSpacing(6);

    prevBtn_ = new QPushButton(tr("← Prev"), pagebar);
    prevBtn_->setEnabled(false);
    connect(prevBtn_, &QPushButton::clicked, this, &MongoCollectionView::onPrevPage);
    ph->addWidget(prevBtn_);

    pageLabel_ = new QLabel(pagebar);
    ph->addWidget(pageLabel_);

    nextBtn_ = new QPushButton(tr("Next →"), pagebar);
    nextBtn_->setEnabled(false);
    connect(nextBtn_, &QPushButton::clicked, this, &MongoCollectionView::onNextPage);
    ph->addWidget(nextBtn_);

    ph->addStretch(1);

    statusLabel_ = new QLabel(pagebar);
    ph->addWidget(statusLabel_);

    root->addWidget(pagebar);
}

void MongoCollectionView::reload() {
    currentSkip_ = 0;
    currentFilter_ = filterEdit_ ? filterEdit_->text().trimmed() : QString{};
    currentLimit_ = limitSpin_ ? limitSpin_->value() : 50;
    loadPage();
}

void MongoCollectionView::loadPage() {
    if (!adapter_) return;

    const std::string col = collection_.toStdString();
    const std::string filter = currentFilter_.toStdString();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        totalCount_ = adapter_->countDocuments(col, filter);
        auto docs = adapter_->findDocuments(col, filter, currentLimit_, currentSkip_);

        docList_->clear();
        selectedIdJson_.clear();
        jsonEditor_->clear();
        saveBtn_->setEnabled(false);

        for (const auto& docJson : docs) {
            QString qjson = QString::fromStdString(docJson);
            // Extract _id value for display label
            QString label = qjson;
            const int oidStart = qjson.indexOf(QStringLiteral("\"$oid\""));
            const int strStart = qjson.indexOf(QStringLiteral("\"_id\""));
            if (oidStart != -1) {
                // ObjectId: find next string value after "$oid"
                const int colon = qjson.indexOf(':', oidStart);
                const int q1 = qjson.indexOf('"', colon + 1);
                const int q2 = qjson.indexOf('"', q1 + 1);
                if (q1 != -1 && q2 != -1)
                    label = qjson.mid(q1 + 1, q2 - q1 - 1);
            } else if (strStart != -1) {
                const int colon = qjson.indexOf(':', strStart + 5);
                if (colon != -1) {
                    const int q1 = qjson.indexOf('"', colon + 1);
                    const int q2 = q1 != -1 ? qjson.indexOf('"', q1 + 1) : -1;
                    if (q1 != -1 && q2 != -1)
                        label = qjson.mid(q1 + 1, q2 - q1 - 1);
                }
            }
            // Append a short preview of remaining fields
            const int previewEnd = qjson.indexOf(',', 0);
            if (previewEnd != -1 && qjson.length() > previewEnd + 2) {
                const QString preview = qjson.mid(previewEnd + 1, 60).simplified();
                label += QStringLiteral(" — ") + preview + QStringLiteral("…");
            }

            auto* item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, qjson);
            docList_->addItem(item);
        }

        updatePagination(totalCount_);
        statusLabel_->setText(tr("%1 documents").arg(totalCount_));
    } catch (const std::exception& ex) {
        showError(QString::fromLocal8Bit(ex.what()));
    }
    QApplication::restoreOverrideCursor();
}

void MongoCollectionView::updatePagination(long total) {
    const int pages = currentLimit_ > 0
        ? static_cast<int>((total + currentLimit_ - 1) / currentLimit_) : 1;
    const int currentPage = currentLimit_ > 0 ? currentSkip_ / currentLimit_ + 1 : 1;
    pageLabel_->setText(tr("Page %1 / %2").arg(currentPage).arg(pages));
    prevBtn_->setEnabled(currentSkip_ > 0);
    nextBtn_->setEnabled(currentSkip_ + currentLimit_ < static_cast<int>(total));
}

void MongoCollectionView::onRefresh() {
    reload();
}

void MongoCollectionView::onRunFilter() {
    currentFilter_ = filterEdit_->text().trimmed();
    currentLimit_ = limitSpin_->value();
    currentSkip_ = 0;
    loadPage();
}

void MongoCollectionView::onDocumentSelected(QListWidgetItem* item) {
    if (!item) return;
    const QString json = item->data(Qt::UserRole).toString();
    jsonEditor_->setPlainText(json);

    // Extract _id filter JSON for updates/deletes
    selectedIdJson_ = buildIdFilter(json);
    saveBtn_->setEnabled(!selectedIdJson_.isEmpty());
}

QString MongoCollectionView::buildIdFilter(const QString& docJson) const {
    // Look for "$oid" pattern → ObjectId
    const int oidStart = docJson.indexOf(QStringLiteral("\"$oid\""));
    if (oidStart != -1) {
        const int colon = docJson.indexOf(':', oidStart);
        const int q1 = docJson.indexOf('"', colon + 1);
        const int q2 = q1 != -1 ? docJson.indexOf('"', q1 + 1) : -1;
        if (q1 != -1 && q2 != -1) {
            const QString oid = docJson.mid(q1 + 1, q2 - q1 - 1);
            return QStringLiteral("{\"_id\":{\"$oid\":\"%1\"}}").arg(oid);
        }
    }
    // Fallback: string _id
    const int idStart = docJson.indexOf(QStringLiteral("\"_id\""));
    if (idStart != -1) {
        const int colon = docJson.indexOf(':', idStart + 5);
        if (colon != -1) {
            const int q1 = docJson.indexOf('"', colon + 1);
            const int q2 = q1 != -1 ? docJson.indexOf('"', q1 + 1) : -1;
            if (q1 != -1 && q2 != -1) {
                const QString sid = docJson.mid(q1 + 1, q2 - q1 - 1);
                return QStringLiteral("{\"_id\":\"%1\"}").arg(sid);
            }
        }
    }
    return {};
}

void MongoCollectionView::onSaveDocument() {
    if (!adapter_ || selectedIdJson_.isEmpty()) return;
    const std::string idFilter = selectedIdJson_.toStdString();
    const std::string json = jsonEditor_->toPlainText().toStdString();
    try {
        adapter_->updateDocument(collection_.toStdString(), idFilter, json);
        statusLabel_->setText(tr("Saved."));
        reload();
    } catch (const std::exception& ex) {
        showError(QString::fromLocal8Bit(ex.what()));
    }
}

void MongoCollectionView::onInsert() {
    if (!adapter_) return;

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Insert Document — %1").arg(collection_));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->resize(520, 400);

    auto* vl = new QVBoxLayout(dlg);
    auto* editor = new QPlainTextEdit(dlg);
    editor->setFont(QFont(QStringLiteral("Monospace"), 11));
    editor->setPlainText(QStringLiteral("{\n  \n}"));
    vl->addWidget(editor, 1);

    auto* btns = new QHBoxLayout;
    auto* cancelBtn = new QPushButton(tr("Cancel"), dlg);
    auto* okBtn = new QPushButton(tr("Insert"), dlg);
    okBtn->setDefault(true);
    btns->addStretch();
    btns->addWidget(cancelBtn);
    btns->addWidget(okBtn);
    vl->addLayout(btns);

    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, dlg, [this, dlg, editor]() {
        const std::string json = editor->toPlainText().toStdString();
        try {
            const auto insertedId = adapter_->insertDocument(
                collection_.toStdString(), json);
            dlg->accept();
            statusLabel_->setText(tr("Inserted: %1").arg(
                QString::fromStdString(insertedId)));
            reload();
        } catch (const std::exception& ex) {
            QMessageBox::critical(dlg, tr("Insert Failed"),
                                  QString::fromLocal8Bit(ex.what()));
        }
    });

    dlg->exec();
}

void MongoCollectionView::onDeleteSelected() {
    if (!adapter_) return;
    auto* item = docList_->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Delete"), tr("Select a document first."));
        return;
    }
    const QString idFilter = buildIdFilter(item->data(Qt::UserRole).toString());
    if (idFilter.isEmpty()) {
        showError(tr("Cannot determine document _id"));
        return;
    }
    const auto ret = QMessageBox::question(
        this, tr("Delete Document"),
        tr("Delete document with filter:\n%1\n\nThis cannot be undone.").arg(idFilter),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    try {
        adapter_->deleteDocument(collection_.toStdString(), idFilter.toStdString());
        statusLabel_->setText(tr("Deleted."));
        reload();
    } catch (const std::exception& ex) {
        showError(QString::fromLocal8Bit(ex.what()));
    }
}

void MongoCollectionView::onPrevPage() {
    currentSkip_ = std::max(0, currentSkip_ - currentLimit_);
    loadPage();
}

void MongoCollectionView::onNextPage() {
    currentSkip_ += currentLimit_;
    loadPage();
}

void MongoCollectionView::showError(const QString& msg) {
    statusLabel_->setText(QStringLiteral("Error: ") + msg);
    QMessageBox::critical(this, tr("MongoDB Error"), msg);
}

} // namespace gridex
