#include "Presentation/Views/DatabaseSwitcher/DatabaseSwitcherDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "Core/Errors/GridexError.h"
#include "Core/Protocols/Database/IDatabaseAdapter.h"

namespace gridex {

DatabaseSwitcherDialog::DatabaseSwitcherDialog(IDatabaseAdapter* adapter,
                                               const QString& currentDatabase,
                                               QWidget* parent)
    : QDialog(parent)
    , adapter_(adapter)
    , currentDatabase_(currentDatabase)
{
    buildUi();
    populate();
}

void DatabaseSwitcherDialog::buildUi() {
    setWindowTitle(tr("Open Database"));
    setMinimumWidth(360);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(16, 16, 16, 12);

    // Title
    auto* title = new QLabel(tr("Open database"), this);
    title->setAlignment(Qt::AlignCenter);
    root->addWidget(title);

    // Search field
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(tr("Search for database..."));
    searchEdit_->setClearButtonEnabled(true);
    connect(searchEdit_, &QLineEdit::textChanged,
            this, &DatabaseSwitcherDialog::onSearchChanged);
    root->addWidget(searchEdit_);

    // Database list
    listWidget_ = new QListWidget(this);
    listWidget_->setFrameShape(QFrame::NoFrame);
    listWidget_->setMinimumHeight(200);
    connect(listWidget_, &QListWidget::itemDoubleClicked,
            this, &DatabaseSwitcherDialog::onItemDoubleClicked);
    connect(listWidget_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* item) {
                openBtn_->setEnabled(item != nullptr);
            });
    root->addWidget(listWidget_);

    // Horizontal divider
    auto* div = new QFrame(this);
    div->setFrameShape(QFrame::HLine);
    root->addWidget(div);

    // Buttons row
    auto* btnRow = new QWidget(this);
    auto* btnH = new QHBoxLayout(btnRow);
    btnH->setContentsMargins(0, 0, 0, 0);

    auto* cancelBtn = new QPushButton(tr("Cancel"), btnRow);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* newBtn = new QPushButton(tr("+ New..."), btnRow);
    newBtn->setToolTip(tr("Create a new database on this server"));
    connect(newBtn, &QPushButton::clicked, this, &DatabaseSwitcherDialog::onCreateClicked);

    openBtn_ = new QPushButton(tr("Open"), btnRow);
    openBtn_->setObjectName(QStringLiteral("primaryButton"));
    openBtn_->setEnabled(false);
    openBtn_->setDefault(true);
    connect(openBtn_, &QPushButton::clicked,
            this, &DatabaseSwitcherDialog::onAccepted);

    btnH->addWidget(cancelBtn);
    btnH->addStretch();
    btnH->addWidget(newBtn);
    btnH->addWidget(openBtn_);
    root->addWidget(btnRow);
}

void DatabaseSwitcherDialog::onCreateClicked() {
    if (!adapter_) return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("New Database"),
        tr("Name for the new database:"),
        QLineEdit::Normal, QString{}, &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    try {
        adapter_->createDatabase(name.toStdString());
    } catch (const GridexError& e) {
        QMessageBox::critical(this, tr("Create Database Failed"),
                              QString::fromUtf8(e.what()));
        return;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Create Database Failed"),
                              QString::fromUtf8(e.what()));
        return;
    }

    // Refresh the list and select the new entry.
    populate();
    const auto matches = listWidget_->findItems(name, Qt::MatchExactly);
    if (!matches.isEmpty()) {
        listWidget_->setCurrentItem(matches.first());
    }
    QMessageBox::information(this, tr("New Database"),
        tr("Database \"%1\" created. Select it and press Open to switch.").arg(name));
}

void DatabaseSwitcherDialog::populate() {
    if (!adapter_) return;

    listWidget_->clear();
    try {
        const auto databases = adapter_->listDatabases();
        for (const auto& db : databases) {
            const QString name = QString::fromUtf8(db.c_str());
            auto* item = new QListWidgetItem(name, listWidget_);
            if (name == currentDatabase_) {
                item->setForeground(Qt::blue);
                // Pre-select current database
                listWidget_->setCurrentItem(item);
            }
        }
    } catch (const GridexError&) {
        auto* item = new QListWidgetItem(tr("(failed to list databases)"), listWidget_);
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }

    if (listWidget_->currentItem()) {
        openBtn_->setEnabled(true);
    }
}

void DatabaseSwitcherDialog::onSearchChanged(const QString& text) {
    const auto lower = text.trimmed().toLower();
    for (int i = 0; i < listWidget_->count(); ++i) {
        auto* item = listWidget_->item(i);
        item->setHidden(!lower.isEmpty() && !item->text().toLower().contains(lower));
    }
    // Select first visible item when filtering
    if (!lower.isEmpty()) {
        for (int i = 0; i < listWidget_->count(); ++i) {
            auto* item = listWidget_->item(i);
            if (!item->isHidden()) {
                listWidget_->setCurrentItem(item);
                break;
            }
        }
    }
}

void DatabaseSwitcherDialog::onAccepted() {
    auto* item = listWidget_->currentItem();
    if (!item || item->isHidden()) return;
    const QString name = item->text();
    emit databaseSelected(name);
    accept();
}

void DatabaseSwitcherDialog::onItemDoubleClicked() {
    onAccepted();
}

} // namespace gridex
