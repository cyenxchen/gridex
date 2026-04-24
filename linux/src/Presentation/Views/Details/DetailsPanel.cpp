#include "Presentation/Views/Details/DetailsPanel.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "Presentation/Views/AIChat/AIChatView.h"

namespace gridex {

DetailsPanel::DetailsPanel(SecretStore* secretStore, WorkspaceState* state, QWidget* parent)
    : QWidget(parent) {
    buildUi();
    chatView_ = new AIChatView(secretStore, state, stack_);
    stack_->addWidget(chatView_);
}

void DetailsPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- Tab bar: [Details | Assistant] ----
    auto* tabBar = new QWidget(this);
    auto* tabH = new QHBoxLayout(tabBar);
    tabH->setContentsMargins(0, 0, 0, 0);
    tabH->setSpacing(0);

    auto makeTab = [this, tabBar](const QString& title, int index) {
        auto* btn = new QPushButton(title, tabBar);
        btn->setFlat(true);
        btn->setCheckable(true);
        btn->setAutoExclusive(true);
        btn->setProperty("tab", true);  // styled by style.qss
        btn->setCursor(Qt::PointingHandCursor);
        btn->setMinimumHeight(32);
        connect(btn, &QPushButton::clicked, this, [this, index] { onTabClicked(index); });
        return btn;
    };

    detailsTabBtn_   = makeTab(tr("Details"),   0);
    assistantTabBtn_ = makeTab(tr("Assistant"), 1);
    tabH->addWidget(detailsTabBtn_, 1);
    tabH->addWidget(assistantTabBtn_, 1);
    root->addWidget(tabBar);

    auto* tabDiv = new QFrame(this);
    tabDiv->setFrameShape(QFrame::HLine);
    root->addWidget(tabDiv);

    // ---- Stacked content ----
    stack_ = new QStackedWidget(this);

    // Page 0: Details (row field list)
    detailsPage_ = new QWidget(stack_);
    auto* dv = new QVBoxLayout(detailsPage_);
    dv->setContentsMargins(0, 0, 0, 0);
    dv->setSpacing(0);

    // Search
    auto* searchRow = new QWidget(detailsPage_);
    auto* sh = new QHBoxLayout(searchRow);
    sh->setContentsMargins(8, 6, 8, 6);
    searchEdit_ = new QLineEdit(searchRow);
    searchEdit_->setPlaceholderText(tr("Search for field..."));
    searchEdit_->setClearButtonEnabled(true);
    connect(searchEdit_, &QLineEdit::textChanged, this, &DetailsPanel::onSearchChanged);
    sh->addWidget(searchEdit_);
    dv->addWidget(searchRow);

    auto* searchDiv = new QFrame(detailsPage_);
    searchDiv->setFrameShape(QFrame::HLine);
    dv->addWidget(searchDiv);

    // Scroll area for field rows
    scrollArea_ = new QScrollArea(detailsPage_);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    fieldsHost_ = new QWidget(scrollArea_);
    fieldsLayout_ = new QVBoxLayout(fieldsHost_);
    fieldsLayout_->setContentsMargins(0, 0, 0, 0);
    fieldsLayout_->setSpacing(0);
    fieldsLayout_->addStretch();
    scrollArea_->setWidget(fieldsHost_);
    dv->addWidget(scrollArea_, 1);

    // Empty state
    emptyLabel_ = new QLabel(tr("No row selected"), detailsPage_);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    dv->addWidget(emptyLabel_, 1);
    scrollArea_->setVisible(false);

    stack_->addWidget(detailsPage_);
    // Page 1 (AIChatView) added in constructor after buildUi().

    root->addWidget(stack_, 1);

    // Initial tab state
    onTabClicked(0);
}

void DetailsPanel::onTabClicked(int index) {
    activeTab_ = index;
    stack_->setCurrentIndex(index);

    // Checked state is styled globally by style.qss (QPushButton:checked).
    detailsTabBtn_->setChecked(index == 0);
    assistantTabBtn_->setChecked(index == 1);
}

void DetailsPanel::setSelectedRow(const std::vector<FieldEntry>& fields) {
    currentFields_ = fields;
    emptyLabel_->setVisible(false);
    scrollArea_->setVisible(true);
    rebuildDetailsList();
}

void DetailsPanel::clearSelectedRow() {
    currentFields_.clear();
    emptyLabel_->setVisible(true);
    scrollArea_->setVisible(false);
    // Clear existing field widgets.
    while (fieldsLayout_->count() > 1) { // keep stretch at end
        auto* item = fieldsLayout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void DetailsPanel::onSearchChanged(const QString&) {
    rebuildDetailsList();
}

void DetailsPanel::rebuildDetailsList() {
    // Clear existing (keep the trailing stretch).
    while (fieldsLayout_->count() > 1) {
        auto* item = fieldsLayout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    const auto filter = searchEdit_->text().trimmed().toLower();

    for (std::size_t fi = 0; fi < currentFields_.size(); ++fi) {
        const auto& f = currentFields_[fi];
        const auto col = QString::fromUtf8(f.column.c_str());
        const auto val = QString::fromUtf8(f.value.c_str());

        if (!filter.isEmpty() &&
            !col.toLower().contains(filter) &&
            !val.toLower().contains(filter)) {
            continue;
        }

        auto* row = new QWidget(fieldsHost_);
        auto* rv = new QVBoxLayout(row);
        rv->setContentsMargins(10, 6, 10, 6);
        rv->setSpacing(2);

        auto* colLabel = new QLabel(col, row);
        rv->addWidget(colLabel);

        // Editable field — QLineEdit replaces QLabel.
        auto* valEdit = new QLineEdit(val, row);
        const int colIdx = static_cast<int>(fi);
        connect(valEdit, &QLineEdit::editingFinished, this,
                [this, valEdit, colIdx, val] {
                    const auto newVal = valEdit->text();
                    if (newVal != val) {
                        emit fieldEdited(colIdx, newVal);
                    }
                });
        rv->addWidget(valEdit);

        fieldsLayout_->insertWidget(fieldsLayout_->count() - 1, row);

        auto* div = new QFrame(fieldsHost_);
        div->setFrameShape(QFrame::HLine);
        fieldsLayout_->insertWidget(fieldsLayout_->count() - 1, div);
    }
}

}
