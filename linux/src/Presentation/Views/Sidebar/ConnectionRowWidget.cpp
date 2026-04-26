#include "Presentation/Views/Sidebar/ConnectionRowWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>

namespace gridex {

namespace {

// Brand-inspired palette per DB type — matches macOS DatabaseTypeIcon hues.
struct IconSpec { QString hex; QString letter; };
IconSpec iconSpec(DatabaseType t) {
    switch (t) {
        case DatabaseType::PostgreSQL: return {"#336791", "P"};
        case DatabaseType::MySQL:      return {"#F29111", "M"};
        case DatabaseType::SQLite:     return {"#003B57", "S"};
        case DatabaseType::Redis:      return {"#DC382D", "R"};
        case DatabaseType::MongoDB:    return {"#4DB33D", "M"};
        case DatabaseType::MSSQL:      return {"#CC2927", "T"};
    }
    return {"#888888", "?"};
}

QString subtitleFor(const ConnectionConfig& c) {
    if (c.databaseType == DatabaseType::SQLite) {
        return c.filePath ? QString::fromUtf8(c.filePath->c_str()) : QStringLiteral("No file");
    }
    QString s = QString::fromUtf8(c.displayHost().c_str());
    if (c.database && !c.database->empty()) {
        s += QStringLiteral(" / ") + QString::fromUtf8(c.database->c_str());
    }
    return s;
}

}

ConnectionRowWidget::ConnectionRowWidget(const ConnectionConfig& config, QWidget* parent)
    : QWidget(parent), connectionId_(QString::fromUtf8(config.id.c_str())) {
    buildUi(config);
    applyPalette();
}

void ConnectionRowWidget::buildUi(const ConnectionConfig& config) {
    setAutoFillBackground(true);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(12);

    // --- 1) Color bar (3×28) ---
    colorBar_ = new QLabel(this);
    colorBar_->setFixedSize(3, 28);
    if (config.colorTag) {
        const auto rgb = rgbColor(*config.colorTag);
        colorBar_->setStyleSheet(QString("background-color: rgb(%1,%2,%3); border-radius: 1px;")
                                     .arg(rgb.r).arg(rgb.g).arg(rgb.b));
    }
    root->addWidget(colorBar_);

    // --- 2) DB type icon (32×32 rounded square with letter) ---
    const auto spec = iconSpec(config.databaseType);
    iconBox_ = new QWidget(this);
    iconBox_->setFixedSize(32, 32);
    iconBox_->setStyleSheet(QString("background-color: %1; border-radius: 7px;").arg(spec.hex));
    auto* iconLay = new QVBoxLayout(iconBox_);
    iconLay->setContentsMargins(0, 0, 0, 0);
    iconLetter_ = new QLabel(spec.letter, iconBox_);
    iconLetter_->setAlignment(Qt::AlignCenter);
    iconLay->addWidget(iconLetter_);
    root->addWidget(iconBox_);

    // --- 3) Name + env badge + subtitle ---
    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(3);

    auto* nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(6);
    nameLabel_ = new QLabel(QString::fromUtf8(config.name.c_str()), this);
    nameRow->addWidget(nameLabel_);

    if (config.colorTag) {
        const auto rgb = rgbColor(*config.colorTag);
        envBadge_ = new QLabel(QString::fromUtf8(std::string(environmentHint(*config.colorTag)).c_str()).toUpper(), this);
        envBadge_->setStyleSheet(
            QString("color: rgb(%1,%2,%3); font-size: 9px; font-weight: 700;"
                    "letter-spacing: 0.3px;"
                    "background-color: rgba(%1,%2,%3,36); padding: 1px 5px; border-radius: 3px;")
                .arg(rgb.r).arg(rgb.g).arg(rgb.b));
        nameRow->addWidget(envBadge_);
    }
    nameRow->addStretch();
    textCol->addLayout(nameRow);

    subtitle_ = new QLabel(subtitleFor(config), this);
    subtitle_->setTextFormat(Qt::PlainText);
    textCol->addWidget(subtitle_);
    root->addLayout(textCol, 1);

    // --- 4) DB type label ---
    typeLabel_ = new QLabel(
        QString::fromUtf8(std::string(displayName(config.databaseType)).c_str()).toUpper(),
        this);
    root->addWidget(typeLabel_);
}

void ConnectionRowWidget::setSelected(bool selected) {
    if (selected_ == selected) return;
    selected_ = selected;
    applyPalette();
}

void ConnectionRowWidget::applyPalette() {
    // Hover + selection background are drawn by the QListWidget::item rule in
    // style.qss — the custom row widget just paints its children.
}

}
