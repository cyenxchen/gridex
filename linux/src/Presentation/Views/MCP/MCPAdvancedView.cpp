#include "Presentation/Views/MCP/MCPAdvancedView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QScrollArea>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "Presentation/Views/MCP/MCPWindowState.h"
#include "Services/MCP/MCPServer.h"

namespace gridex {

namespace {

QSpinBox* makeSpin(QWidget* p, int min, int max, int step, const QString& suffix = QString()) {
    auto* s = new QSpinBox(p);
    s->setRange(min, max);
    s->setSingleStep(step);
    if (!suffix.isEmpty()) s->setSuffix(suffix);
    return s;
}

}  // namespace

MCPAdvancedView::MCPAdvancedView(MCPWindowState* state, QWidget* parent)
    : QWidget(parent), state_(state) {
    buildUi();
    loadFromSettings();
    applyLimits();
}

void MCPAdvancedView::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("mcpScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    rootLayout->addWidget(scroll);

    auto* host = new QWidget(scroll);
    auto* hostH = new QHBoxLayout(host);
    hostH->setContentsMargins(24, 20, 24, 24);
    hostH->setSpacing(0);
    hostH->addStretch();
    auto* content = new QWidget(host);
    content->setMaximumWidth(900);
    auto* outer = new QVBoxLayout(content);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(16);

    // Rate limits
    auto* rateBox = new QGroupBox(tr("Rate Limits"), this);
    auto* rf = new QFormLayout(rateBox);
    queriesPerMinute_ = makeSpin(rateBox, 10, 200, 10);
    queriesPerHour_   = makeSpin(rateBox, 100, 5000, 100);
    writesPerMinute_  = makeSpin(rateBox, 1, 50, 1);
    ddlPerMinute_     = makeSpin(rateBox, 1, 10, 1);
    rf->addRow(tr("Queries per minute"), queriesPerMinute_);
    rf->addRow(tr("Queries per hour"),   queriesPerHour_);
    rf->addRow(tr("Writes per minute"),  writesPerMinute_);
    rf->addRow(tr("DDL per minute"),     ddlPerMinute_);
    outer->addWidget(rateBox);

    // Timeouts
    auto* toBox = new QGroupBox(tr("Timeouts"), this);
    auto* tof = new QFormLayout(toBox);
    queryTimeout_      = makeSpin(toBox, 5, 300, 5, "s");
    approvalTimeout_   = makeSpin(toBox, 10, 300, 10, "s");
    connectionTimeout_ = makeSpin(toBox, 5, 60, 5, "s");
    tof->addRow(tr("Query timeout"),      queryTimeout_);
    tof->addRow(tr("Approval timeout"),   approvalTimeout_);
    tof->addRow(tr("Connection timeout"), connectionTimeout_);
    outer->addWidget(toBox);

    // Audit log
    auto* audBox = new QGroupBox(tr("Audit Log"), this);
    auto* auf = new QFormLayout(audBox);
    retention_ = new QComboBox(audBox);
    retention_->addItem(tr("7 days"),  7);
    retention_->addItem(tr("30 days"), 30);
    retention_->addItem(tr("90 days"), 90);
    retention_->addItem(tr("1 year"),  365);
    retention_->addItem(tr("Forever"), 0);
    maxSizeMB_ = makeSpin(audBox, 10, 500, 10, " MB");
    auf->addRow(tr("Retention"),    retention_);
    auf->addRow(tr("Max log size"), maxSizeMB_);
    auto* locLbl = new QLabel(audBox);
    locLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    locLbl->setStyleSheet("color: palette(mid);");
    if (state_ && state_->server()) {
        locLbl->setText(QString::fromStdString(state_->server()->auditLogger().logFilePath()));
    }
    auf->addRow(tr("Location"), locLbl);
    outer->addWidget(audBox);

    // Security
    auto* secBox = new QGroupBox(tr("Security"), this);
    auto* sv = new QVBoxLayout(secBox);
    requireApprovalWrites_ = new QCheckBox(tr("Require approval for write operations"), secBox);
    allowRemoteHttp_       = new QCheckBox(tr("Allow remote HTTP connections"), secBox);
    sv->addWidget(requireApprovalWrites_);
    sv->addWidget(allowRemoteHttp_);
    auto* warn = new QLabel(
        tr("⚠ Remote HTTP allows connections from other machines. Use only on trusted networks."), secBox);
    warn->setWordWrap(true);
    warn->setStyleSheet("color: #ef9f27; font-size: small;");
    warn->setVisible(false);
    sv->addWidget(warn);
    connect(allowRemoteHttp_, &QCheckBox::toggled, warn, &QLabel::setVisible);
    outer->addWidget(secBox);

    // Reset
    auto* resetRow = new QHBoxLayout();
    resetRow->addStretch();
    auto* resetBtn = new QPushButton(tr("Reset to Defaults…"), this);
    connect(resetBtn, &QPushButton::clicked, this, &MCPAdvancedView::onResetClicked);
    resetRow->addWidget(resetBtn);
    outer->addLayout(resetRow);

    outer->addStretch();
    hostH->addWidget(content, 0, Qt::AlignTop);
    hostH->addStretch();
    scroll->setWidget(host);

    // Hook up save-on-change
    for (auto* s : {queriesPerMinute_, queriesPerHour_, writesPerMinute_, ddlPerMinute_,
                    queryTimeout_, approvalTimeout_, connectionTimeout_, maxSizeMB_}) {
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, &MCPAdvancedView::onValueChanged);
    }
    connect(retention_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MCPAdvancedView::onValueChanged);
    connect(requireApprovalWrites_, &QCheckBox::toggled, this, &MCPAdvancedView::onValueChanged);
    connect(allowRemoteHttp_,       &QCheckBox::toggled, this, &MCPAdvancedView::onValueChanged);
}

void MCPAdvancedView::loadFromSettings() {
    QSettings s;
    queriesPerMinute_->setValue(s.value("mcp.rateLimit.queriesPerMinute", 60).toInt());
    queriesPerHour_  ->setValue(s.value("mcp.rateLimit.queriesPerHour",   1000).toInt());
    writesPerMinute_ ->setValue(s.value("mcp.rateLimit.writesPerMinute",  10).toInt());
    ddlPerMinute_    ->setValue(s.value("mcp.rateLimit.ddlPerMinute",     1).toInt());

    queryTimeout_     ->setValue(s.value("mcp.timeout.query",      30).toInt());
    approvalTimeout_  ->setValue(s.value("mcp.timeout.approval",   60).toInt());
    connectionTimeout_->setValue(s.value("mcp.timeout.connection", 10).toInt());

    int ret = s.value("mcp.audit.retentionDays", 90).toInt();
    int idx = retention_->findData(ret);
    retention_->setCurrentIndex(idx >= 0 ? idx : 2);
    maxSizeMB_->setValue(s.value("mcp.audit.maxSizeMB", 100).toInt());

    requireApprovalWrites_->setChecked(s.value("mcp.security.requireApprovalForWrites", true).toBool());
    allowRemoteHttp_      ->setChecked(s.value("mcp.security.allowRemoteHTTP",          false).toBool());
}

void MCPAdvancedView::saveToSettings() {
    QSettings s;
    s.setValue("mcp.rateLimit.queriesPerMinute", queriesPerMinute_->value());
    s.setValue("mcp.rateLimit.queriesPerHour",   queriesPerHour_->value());
    s.setValue("mcp.rateLimit.writesPerMinute",  writesPerMinute_->value());
    s.setValue("mcp.rateLimit.ddlPerMinute",     ddlPerMinute_->value());

    s.setValue("mcp.timeout.query",      queryTimeout_->value());
    s.setValue("mcp.timeout.approval",   approvalTimeout_->value());
    s.setValue("mcp.timeout.connection", connectionTimeout_->value());

    s.setValue("mcp.audit.retentionDays", retention_->currentData().toInt());
    s.setValue("mcp.audit.maxSizeMB",     maxSizeMB_->value());

    s.setValue("mcp.security.requireApprovalForWrites", requireApprovalWrites_->isChecked());
    s.setValue("mcp.security.allowRemoteHTTP",          allowRemoteHttp_->isChecked());
}

void MCPAdvancedView::applyLimits() {
    if (!state_ || !state_->server()) return;
    mcp::RateLimits l;
    l.queriesPerMinute = queriesPerMinute_->value();
    l.queriesPerHour   = queriesPerHour_->value();
    l.writesPerMinute  = writesPerMinute_->value();
    l.ddlPerMinute     = ddlPerMinute_->value();
    state_->server()->rateLimiter().setLimits(l);
    state_->server()->auditLogger().setMaxFileSize(
        static_cast<qint64>(maxSizeMB_->value()) * 1024 * 1024);
}

void MCPAdvancedView::onValueChanged() {
    saveToSettings();
    applyLimits();
}

void MCPAdvancedView::onResetClicked() {
    if (QMessageBox::question(this, tr("Reset All Settings?"),
            tr("This will restore all MCP settings to their default values."),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    queriesPerMinute_->setValue(60);
    queriesPerHour_  ->setValue(1000);
    writesPerMinute_ ->setValue(10);
    ddlPerMinute_    ->setValue(1);
    queryTimeout_     ->setValue(30);
    approvalTimeout_  ->setValue(60);
    connectionTimeout_->setValue(10);
    retention_->setCurrentIndex(retention_->findData(90));
    maxSizeMB_->setValue(100);
    requireApprovalWrites_->setChecked(true);
    allowRemoteHttp_      ->setChecked(false);
}

}  // namespace gridex
