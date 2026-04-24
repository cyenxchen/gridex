#include "Presentation/Views/QueryLog/QueryLogPanel.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

namespace gridex {

QueryLogPanel::QueryLogPanel(QWidget* parent) : QWidget(parent) {
    buildUi();
}

void QueryLogPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- Header row (always visible) ----
    auto* topDiv = new QFrame(this);
    topDiv->setFrameShape(QFrame::HLine);
    root->addWidget(topDiv);

    header_ = new QWidget(this);
    header_->setObjectName("QueryLogHeader");
    header_->setFixedHeight(26);
    header_->setAutoFillBackground(true);
    auto* headerH = new QHBoxLayout(header_);
    headerH->setContentsMargins(8, 0, 8, 0);
    headerH->setSpacing(6);

    toggleBtn_ = new QPushButton(QStringLiteral("▾"), header_);
    toggleBtn_->setObjectName("QueryLogToggle");
    toggleBtn_->setFixedSize(20, 20);
    toggleBtn_->setCursor(Qt::PointingHandCursor);
    connect(toggleBtn_, &QPushButton::clicked, this, [this] {
        setExpanded(!expanded_);
        emit toggleRequested();
    });
    headerH->addWidget(toggleBtn_);

    titleLabel_ = new QLabel(tr("Console log"), header_);
    titleLabel_->setObjectName("QueryLogTitle");
    headerH->addWidget(titleLabel_);
    headerH->addStretch();

    auto* clearBtn = new QPushButton(tr("Clear"), header_);
    clearBtn->setObjectName("QueryLogClear");
    clearBtn->setCursor(Qt::PointingHandCursor);
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        if (logEdit_) logEdit_->clear();
    });
    headerH->addWidget(clearBtn);

    root->addWidget(header_);

    // ---- Log body (collapsible) ----
    logEdit_ = new QPlainTextEdit(this);
    logEdit_->setObjectName("QueryLogEdit");
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumHeight(130);
    logEdit_->setMinimumHeight(80);
    logEdit_->setPlaceholderText(tr("Execute a query or select a table to see the log"));
    root->addWidget(logEdit_);
}

void QueryLogPanel::appendQuery(const QString& sql, int durationMs) {
    if (!logEdit_) return;
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    const QString line = QStringLiteral("[%1] (%2ms) %3").arg(ts).arg(durationMs).arg(sql.trimmed());
    logEdit_->appendPlainText(line);
    // Scroll to bottom so the latest entry is always visible.
    auto* sb = logEdit_->verticalScrollBar();
    if (sb) sb->setValue(sb->maximum());
}

void QueryLogPanel::setExpanded(bool expanded) {
    expanded_ = expanded;
    if (logEdit_) logEdit_->setVisible(expanded_);
    toggleBtn_->setText(expanded_ ? QStringLiteral("▾") : QStringLiteral("▸"));
}

}
