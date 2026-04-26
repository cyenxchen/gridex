#include "Presentation/Views/QueryEditor/Autocomplete/CompletionPopup.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>

namespace gridex {

namespace {
constexpr int kRowHeight    = 22;
constexpr int kMaxVisible   = 10;
constexpr int kWindowWidth  = 440;
constexpr int kIconCellPx   = 18;
}  // namespace

CompletionPopup::CompletionPopup(QWidget* parent)
    : QFrame(parent) {
    // Tool window that never steals focus and never grabs keyboard/mouse.
    // Qt::Popup would grab input and leave the editor unreachable once the
    // popup is re-shown (e.g. after accept -> debounce re-fire). Qt::Tool +
    // WindowDoesNotAcceptFocus keeps all key events going to the editor, and
    // eventFilter on the editor routes navigation keys to this popup.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                   | Qt::WindowDoesNotAcceptFocus
                   | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setFrameShape(QFrame::Box);
    setLineWidth(1);

    buildUi();
}

void CompletionPopup::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 4, 0, 0);
    root->setSpacing(0);

    list_ = new QListWidget(this);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setUniformItemSizes(true);
    root->addWidget(list_, 1);

    connect(list_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* it) {
                const int idx = list_->row(it);
                if (idx >= 0 && idx < static_cast<int>(items_.size())) {
                    emit accepted(items_[idx]);
                }
            });

    footer_ = new QLabel(
        tr("<span style='color:#777'>↑↓ Nav  ⏎ Tab Accept  Esc Dismiss</span>"),
        this);
    footer_->setTextFormat(Qt::RichText);
    root->addWidget(footer_);
}

QString CompletionPopup::typeLabel(CompletionType t) const {
    switch (t) {
        case CompletionType::Keyword:  return "kw";
        case CompletionType::Table:    return "T";
        case CompletionType::Column:   return "col";
        case CompletionType::Function: return "fn";
        case CompletionType::Join:     return "J";
    }
    return "?";
}

void CompletionPopup::setItems(const std::vector<CompletionItem>& items) {
    items_ = items;
    list_->clear();
    for (const auto& it : items_) {
        QString line = QString("%1  %2").arg(typeLabel(it.type), -3).arg(it.text);
        if (!it.detail.isEmpty()) {
            line += "    ";
            line += it.detail;
        }
        auto* wi = new QListWidgetItem(line);
        list_->addItem(wi);
    }
    if (!items_.empty()) list_->setCurrentRow(0);

    const int visible = std::min<int>(items_.size(), kMaxVisible);
    const int h = visible * kRowHeight + 4 + 20 /* footer */;
    setFixedSize(kWindowWidth, std::max(h, kRowHeight + 24));
    Q_UNUSED(kIconCellPx);
}

int CompletionPopup::selectedIndex() const {
    return list_->currentRow();
}

void CompletionPopup::moveSelection(int delta) {
    if (items_.empty()) return;
    int i = list_->currentRow() + delta;
    const int n = static_cast<int>(items_.size());
    if (i < 0) i = n - 1;
    if (i >= n) i = 0;
    list_->setCurrentRow(i);
    list_->scrollToItem(list_->currentItem());
}

const CompletionItem* CompletionPopup::selectedItem() const {
    const int idx = list_->currentRow();
    if (idx < 0 || idx >= static_cast<int>(items_.size())) return nullptr;
    return &items_[idx];
}

void CompletionPopup::keyPressEvent(QKeyEvent* event) {
    // The editor forwards relevant keys to the popup via public methods; this
    // handler only fires if the popup itself somehow receives focus.
    if (event->key() == Qt::Key_Escape) { emit dismissed(); return; }
    if (event->key() == Qt::Key_Up)   { moveSelection(-1); return; }
    if (event->key() == Qt::Key_Down) { moveSelection(+1); return; }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
        || event->key() == Qt::Key_Tab) {
        if (auto* it = selectedItem()) emit accepted(*it);
        return;
    }
    QFrame::keyPressEvent(event);
}

}  // namespace gridex
