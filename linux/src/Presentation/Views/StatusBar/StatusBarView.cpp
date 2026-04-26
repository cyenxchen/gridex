#include "Presentation/Views/StatusBar/StatusBarView.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>

namespace gridex {

QLabel* StatusBarView::makeSeparator(QWidget* parent) {
    auto* s = new QLabel(QStringLiteral("|"), parent);
    s->setContentsMargins(6, 0, 6, 0);
    return s;
}

QLabel* StatusBarView::makeItem(QWidget* parent) {
    auto* l = new QLabel(QString{}, parent);
    return l;
}

StatusBarView::StatusBarView(QWidget* parent) : QWidget(parent) {
    buildUi();
}

void StatusBarView::buildUi() {
    setFixedHeight(24);
    setAutoFillBackground(true);

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(8, 0, 8, 0);
    h->setSpacing(0);

    connLabel_   = makeItem(this);
    schemaLabel_ = makeItem(this);
    rowsLabel_   = makeItem(this);
    timeLabel_   = makeItem(this);
    mcpLabel_    = makeItem(this);
    versionLabel_ = makeItem(this);

    connLabel_->setText(tr("Not connected"));
    versionLabel_->setText(QStringLiteral("v0.1"));

    h->addWidget(connLabel_);
    h->addWidget(makeSeparator(this));
    h->addWidget(schemaLabel_);
    h->addWidget(makeSeparator(this));
    h->addWidget(rowsLabel_);
    h->addWidget(makeSeparator(this));
    h->addWidget(timeLabel_);
    h->addStretch();
    h->addWidget(mcpLabel_);
    h->addWidget(makeSeparator(this));
    h->addWidget(versionLabel_);
}

void StatusBarView::setConnection(const QString& text) { connLabel_->setText(text.isEmpty() ? tr("Not connected") : text); }
void StatusBarView::setSchema(const QString& text)     { schemaLabel_->setText(text); }
void StatusBarView::setRowCount(const QString& text)   { rowsLabel_->setText(text); }
void StatusBarView::setQueryTime(const QString& text)  { timeLabel_->setText(text); }
void StatusBarView::setMcpStatus(const QString& text)  { mcpLabel_->setText(text); }

}
