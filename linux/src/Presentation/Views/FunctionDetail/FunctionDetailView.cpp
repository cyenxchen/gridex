#include "Presentation/Views/FunctionDetail/FunctionDetailView.h"

#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "Core/Protocols/Database/IDatabaseAdapter.h"
#include "Core/Errors/GridexError.h"
#include "Presentation/Views/QueryEditor/SqlHighlighter.h"

namespace gridex {

FunctionDetailView::FunctionDetailView(IDatabaseAdapter* adapter,
                                       const QString& schema,
                                       const QString& name,
                                       bool isProcedure,
                                       QWidget* parent)
    : QWidget(parent)
    , adapter_(adapter)
    , schema_(schema)
    , name_(name)
    , isProcedure_(isProcedure)
{
    buildUi();
    loadSource();
}

void FunctionDetailView::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header bar
    auto* headerBar = new QWidget(this);
    auto* hh = new QHBoxLayout(headerBar);
    hh->setContentsMargins(12, 6, 12, 6);
    hh->setSpacing(8);

    headerLabel_ = new QLabel(name_, headerBar);
    QFont hf = headerLabel_->font();
    hf.setWeight(QFont::Medium);
    headerLabel_->setFont(hf);

    badgeLabel_ = new QLabel(isProcedure_ ? tr("Procedure") : tr("Function"), headerBar);
    badgeLabel_->setObjectName(isProcedure_
        ? QStringLiteral("procedureBadge")
        : QStringLiteral("functionBadge"));
    badgeLabel_->setContentsMargins(6, 2, 6, 2);

    copyBtn_ = new QPushButton(tr("Copy"), headerBar);
    copyBtn_->setCursor(Qt::PointingHandCursor);
    connect(copyBtn_, &QPushButton::clicked, this, &FunctionDetailView::onCopy);

    refreshBtn_ = new QPushButton(tr("Refresh"), headerBar);
    refreshBtn_->setCursor(Qt::PointingHandCursor);
    connect(refreshBtn_, &QPushButton::clicked, this, &FunctionDetailView::onRefresh);

    hh->addWidget(headerLabel_);
    hh->addWidget(badgeLabel_);
    hh->addStretch();
    hh->addWidget(copyBtn_);
    hh->addWidget(refreshBtn_);
    root->addWidget(headerBar);

    auto* div = new QFrame(this);
    div->setFrameShape(QFrame::HLine);
    root->addWidget(div);

    // Source editor (readonly)
    editor_ = new QPlainTextEdit(this);
    editor_->setReadOnly(true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(12);
    editor_->setFont(mono);
    editor_->setFrameShape(QFrame::NoFrame);
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);

    new SqlHighlighter(editor_->document());

    root->addWidget(editor_, 1);
}

void FunctionDetailView::loadSource() {
    if (!adapter_) return;

    const std::optional<std::string> schemaOpt =
        schema_.isEmpty() ? std::nullopt
                          : std::make_optional(schema_.toStdString());
    try {
        std::string src;
        if (isProcedure_) {
            src = adapter_->getProcedureSource(name_.toStdString(), schemaOpt);
        } else {
            src = adapter_->getFunctionSource(name_.toStdString(), schemaOpt);
        }
        editor_->setPlainText(QString::fromUtf8(src.c_str()));
    } catch (const GridexError& e) {
        editor_->setPlainText(tr("-- Error loading source: %1")
                                  .arg(QString::fromUtf8(e.what())));
    } catch (const std::exception& e) {
        editor_->setPlainText(tr("-- Error: %1").arg(QString::fromUtf8(e.what())));
    }
}

void FunctionDetailView::onCopy() {
    QApplication::clipboard()->setText(editor_->toPlainText());
}

void FunctionDetailView::onRefresh() {
    loadSource();
}

}
