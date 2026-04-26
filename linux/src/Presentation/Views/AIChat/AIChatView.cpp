#include "Presentation/Views/AIChat/AIChatView.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QLinearGradient>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include "Core/Enums/DatabaseType.h"
#include "Core/Enums/SQLDialect.h"
#include "Core/Errors/GridexError.h"
#include "Core/Models/AI/AIModels.h"
#include "Core/Models/Schema/SchemaSnapshot.h"
#include "Core/Protocols/Database/IDatabaseAdapter.h"
#include "Data/Keychain/SecretStore.h"
#include "Presentation/ViewModels/AIChatViewModel.h"
#include "Presentation/ViewModels/WorkspaceState.h"

namespace gridex {

namespace {

// Catppuccin accent gradient (mauve → blue) matching macOS purple→blue.
constexpr const char* kGrad1 = "#cba6f7";
constexpr const char* kGrad2 = "#89b4fa";

// Paint a gradient square with an emoji glyph centred — used for brand icons
// and user/assistant avatars. Returns a QPixmap at 2x for HiDPI.
QPixmap makeGradientGlyphPixmap(int size, const QString& glyph,
                                const QColor& c1, const QColor& c2,
                                bool circle = false, int radius = 8) {
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient grad(0, 0, size, size);
    grad.setColorAt(0, c1);
    grad.setColorAt(1, c2);
    p.setBrush(grad);
    p.setPen(Qt::NoPen);
    if (circle) p.drawEllipse(0, 0, size, size);
    else        p.drawRoundedRect(0, 0, size, size, radius, radius);

    QFont f = p.font();
    f.setPixelSize(int(size * 0.55));
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, glyph);
    return pm;
}

// Parse `text` into alternating segments: plain text or fenced code block.
// Detects ```lang\n...\n``` and ```\n...\n``` markdown fences.
struct Segment {
    bool isCode = false;
    QString language;
    QString body;
};
std::vector<Segment> parseSegments(const QString& text) {
    std::vector<Segment> out;
    const QStringList lines = text.split('\n');
    QStringList buf;
    auto flushText = [&]() {
        if (buf.isEmpty()) return;
        const QString joined = buf.join('\n').trimmed();
        if (!joined.isEmpty()) out.push_back({false, {}, joined});
        buf.clear();
    };
    int i = 0;
    while (i < lines.size()) {
        const QString trimmed = lines[i].trimmed();
        if (trimmed.startsWith(QStringLiteral("```"))) {
            flushText();
            const QString lang = trimmed.mid(3).trimmed();
            QStringList code;
            ++i;
            while (i < lines.size()
                   && lines[i].trimmed() != QStringLiteral("```")) {
                code << lines[i];
                ++i;
            }
            out.push_back({true, lang.isEmpty() ? QStringLiteral("plain") : lang,
                           code.join('\n')});
            ++i;
            continue;
        }
        buf << lines[i];
        ++i;
    }
    flushText();
    return out;
}

// Code block widget: header (lang label + Copy button) + monospace body.
QWidget* makeCodeBlock(const QString& language, const QString& code,
                       QWidget* parent) {
    auto* w = new QFrame(parent);
    w->setObjectName(QStringLiteral("codeBlock"));
    w->setStyleSheet(QStringLiteral(
        "QFrame#codeBlock { background-color: #11111b;"
        " border: 1px solid #313244; border-radius: 8px; }"));

    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // Header
    auto* header = new QWidget(w);
    header->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #181825;"
        " border-top-left-radius: 7px; border-top-right-radius: 7px; }"));
    auto* hh = new QHBoxLayout(header);
    hh->setContentsMargins(10, 4, 10, 4);
    hh->setSpacing(6);
    auto* lang = new QLabel(language.toUpper(), header);
    {
        QFont f = lang->font();
        f.setFamily(QStringLiteral("monospace"));
        f.setPointSize(9);
        f.setWeight(QFont::DemiBold);
        lang->setFont(f);
    }
    lang->setForegroundRole(QPalette::PlaceholderText);
    hh->addWidget(lang);
    hh->addStretch();

    auto* copy = new QToolButton(header);
    copy->setText(QStringLiteral("📋 Copy"));
    copy->setCursor(Qt::PointingHandCursor);
    copy->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none;"
        " color: #a6adc8; font-size: 11px; padding: 0 4px; }"
        "QToolButton:hover { color: #cdd6f4; }"));
    QObject::connect(copy, &QToolButton::clicked, copy, [copy, code]() {
        QApplication::clipboard()->setText(code);
        copy->setText(QStringLiteral("✓ Copied"));
        QTimer::singleShot(1500, copy, [copy]() {
            copy->setText(QStringLiteral("📋 Copy"));
        });
    });
    hh->addWidget(copy);
    v->addWidget(header);

    // Body — QLabel with monospace font, wraps horizontal scroll instead
    // of wrapping (code shouldn't be re-flowed).
    auto* body = new QLabel(code, w);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->setContentsMargins(12, 8, 12, 10);
    {
        QFont f(QStringLiteral("monospace"));
        f.setStyleHint(QFont::Monospace);
        f.setPointSize(11);
        body->setFont(f);
    }
    body->setStyleSheet(QStringLiteral("color: #cdd6f4;"));
    v->addWidget(body);
    return w;
}

// Animated typing indicator — 3 dots pulsing phase-shifted.
class TypingIndicator : public QWidget {
public:
    explicit TypingIndicator(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(48, 28);
        auto* anim = new QVariantAnimation(this);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setDuration(900);
        anim->setLoopCount(-1);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            phase_ = v.toDouble();
            update();
        });
        anim->start();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        const int y = height() / 2;
        for (int i = 0; i < 3; ++i) {
            const double off = i * 0.2;
            const double prog = std::fmod(phase_ + off, 1.0);
            const double scale = 0.7 + 0.5 * std::sin(prog * 3.14159);
            const int r = int(4 * scale);
            const int x = 10 + i * 10;
            const int alpha = int(120 + 135 * std::sin(prog * 3.14159));
            p.setBrush(QColor(186, 194, 222, alpha));
            p.drawEllipse(QPoint(x, y), r, r);
        }
    }
private:
    double phase_ = 0.0;
};

}  // namespace

// ---- Construction -----------------------------------------------------

AIChatView::AIChatView(SecretStore* secretStore, WorkspaceState* state, QWidget* parent)
    : QWidget(parent), state_(state) {
    vm_ = new AIChatViewModel(secretStore, this);

    if (state) {
        connect(state, &WorkspaceState::connectionOpened, this, [this, state]() {
            const auto dbTypeStr = QString::fromUtf8(
                std::string(displayName(state->config().databaseType)).c_str());
            vm_->setDatabaseContext(dbTypeStr, QString{});
        });
        if (state->isOpen()) {
            const auto dbTypeStr = QString::fromUtf8(
                std::string(displayName(state->config().databaseType)).c_str());
            vm_->setDatabaseContext(dbTypeStr, QString{});
        }
    }

    buildUi();

    connect(vm_, &AIChatViewModel::messagesChanged,  this, &AIChatView::onMessagesChanged);
    connect(vm_, &AIChatViewModel::isLoadingChanged, this, &AIChatView::onLoadingChanged);
    connect(vm_, &AIChatViewModel::errorOccurred,    this, &AIChatView::onErrorOccurred);
}

// ---- UI construction --------------------------------------------------

void AIChatView::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildHeader(root);

    // Body stack: welcome (empty chat) vs messages list.
    bodyStack_ = new QStackedWidget(this);
    buildWelcomePage();
    buildMessagesPage();
    bodyStack_->addWidget(welcomePage_);
    bodyStack_->addWidget(scrollArea_);
    bodyStack_->setCurrentWidget(welcomePage_);
    root->addWidget(bodyStack_, 1);

    // Chip row — shown when attachedTables_ non-empty.
    chipRow_ = new QWidget(this);
    chipLayout_ = new QHBoxLayout(chipRow_);
    chipLayout_->setContentsMargins(12, 6, 12, 4);
    chipLayout_->setSpacing(6);
    chipLayout_->addStretch();
    chipRow_->setVisible(false);
    root->addWidget(chipRow_);

    buildInputBar(root);

    refreshHeader();
}

void AIChatView::buildHeader(QVBoxLayout* root) {
    auto* header = new QWidget(this);
    header->setFixedHeight(48);
    header->setAutoFillBackground(true);
    header->setBackgroundRole(QPalette::Window);

    auto* h = new QHBoxLayout(header);
    h->setContentsMargins(12, 8, 12, 8);
    h->setSpacing(10);

    // Brand icon: gradient rounded square with ✨.
    brandIcon_ = new QLabel(header);
    brandIcon_->setFixedSize(28, 28);
    brandIcon_->setPixmap(makeGradientGlyphPixmap(
        28, QStringLiteral("✨"),
        QColor(kGrad1), QColor(kGrad2), /*circle=*/false, /*radius=*/7));
    h->addWidget(brandIcon_);

    // Title + model line
    auto* titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(1);
    brandTitle_ = new QLabel(tr("AI Assistant"), header);
    {
        QFont f = brandTitle_->font();
        f.setWeight(QFont::DemiBold);
        brandTitle_->setFont(f);
    }
    titleCol->addWidget(brandTitle_);

    auto* subRow = new QHBoxLayout();
    subRow->setContentsMargins(0, 0, 0, 0);
    subRow->setSpacing(5);
    statusDot_ = new QLabel(header);
    statusDot_->setFixedSize(8, 8);
    statusDot_->setStyleSheet(QStringLiteral(
        "QLabel { background-color: #f9e2af; border-radius: 4px; }"));
    subRow->addWidget(statusDot_);
    modelStatus_ = new QLabel(header);
    modelStatus_->setForegroundRole(QPalette::PlaceholderText);
    {
        QFont f = modelStatus_->font();
        f.setPointSize(f.pointSize() - 1);
        modelStatus_->setFont(f);
    }
    subRow->addWidget(modelStatus_, 1);
    titleCol->addLayout(subRow);

    h->addLayout(titleCol, 1);

    // New-chat button (circle icon-only).
    newChatBtn_ = new QToolButton(header);
    newChatBtn_->setText(QStringLiteral("↻"));
    newChatBtn_->setFixedSize(28, 28);
    newChatBtn_->setCursor(Qt::PointingHandCursor);
    newChatBtn_->setToolTip(tr("New chat"));
    newChatBtn_->setStyleSheet(QStringLiteral(
        "QToolButton { background-color: #313244; color: #cdd6f4;"
        " border: none; border-radius: 14px; font-size: 14px; }"
        "QToolButton:hover { background-color: #45475a; }"));
    connect(newChatBtn_, &QToolButton::clicked, this, [this]() {
        vm_->clearHistory();
    });
    h->addWidget(newChatBtn_);

    root->addWidget(header);

    auto* div = new QFrame(this);
    div->setFrameShape(QFrame::HLine);
    root->addWidget(div);
}

void AIChatView::buildWelcomePage() {
    welcomePage_ = new QWidget(this);
    auto* v = new QVBoxLayout(welcomePage_);
    v->setContentsMargins(30, 40, 30, 40);
    v->setSpacing(16);
    v->addStretch();

    // Big brand icon (circle)
    auto* bigIcon = new QLabel(welcomePage_);
    bigIcon->setFixedSize(64, 64);
    bigIcon->setAlignment(Qt::AlignCenter);
    bigIcon->setPixmap(makeGradientGlyphPixmap(
        64, QStringLiteral("✨"),
        QColor(kGrad1), QColor(kGrad2), /*circle=*/true));
    auto* iconRow = new QHBoxLayout();
    iconRow->addStretch();
    iconRow->addWidget(bigIcon);
    iconRow->addStretch();
    v->addLayout(iconRow);

    auto* title = new QLabel(tr("How can I help you?"), welcomePage_);
    title->setAlignment(Qt::AlignCenter);
    {
        QFont f = title->font();
        f.setPointSize(14);
        f.setWeight(QFont::DemiBold);
        title->setFont(f);
    }
    v->addWidget(title);

    auto* subtitle = new QLabel(tr("Ask me anything about your database"), welcomePage_);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setForegroundRole(QPalette::PlaceholderText);
    v->addWidget(subtitle);

    v->addSpacing(12);

    // Suggested prompts
    const QList<QPair<QString, QString>> prompts = {
        {QStringLiteral("🔍"), tr("Show me the schema of all tables")},
        {QStringLiteral("📊"), tr("Find the top 10 records by date")},
        {QStringLiteral("📝"), tr("Generate a SQL query to count rows")},
        {QStringLiteral("💡"), tr("Explain the relationships between tables")},
    };
    for (const auto& [icon, text] : prompts) {
        auto* btn = new QToolButton(welcomePage_);
        btn->setText(QStringLiteral("  %1    %2").arg(icon, text));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setMinimumHeight(36);
        btn->setStyleSheet(QStringLiteral(
            "QToolButton { background-color: #313244; color: #cdd6f4;"
            " border: 1px solid #45475a; border-radius: 10px;"
            " padding: 8px 12px; text-align: left; font-size: 12px; }"
            "QToolButton:hover { background-color: #45475a; border-color: #585b70; }"));
        connect(btn, &QToolButton::clicked, this, [this, text]() {
            onSuggestedPromptClicked(text);
        });
        v->addWidget(btn);
    }

    v->addStretch();
}

void AIChatView::buildMessagesPage() {
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    messagesWidget_ = new QWidget(scrollArea_);
    messagesLayout_ = new QVBoxLayout(messagesWidget_);
    messagesLayout_->setContentsMargins(14, 16, 14, 16);
    messagesLayout_->setSpacing(14);
    messagesLayout_->addStretch();

    scrollArea_->setWidget(messagesWidget_);
}

void AIChatView::buildInputBar(QVBoxLayout* root) {
    auto* bar = new QWidget(this);
    auto* outer = new QVBoxLayout(bar);
    outer->setContentsMargins(12, 10, 12, 12);
    outer->setSpacing(0);

    // Capsule: [+] [text edit] [send]
    auto* capsule = new QFrame(bar);
    capsule->setObjectName(QStringLiteral("chatCapsule"));
    capsule->setStyleSheet(QStringLiteral(
        "QFrame#chatCapsule { background-color: #313244;"
        " border: 1px solid #45475a; border-radius: 18px; }"));

    auto* h = new QHBoxLayout(capsule);
    h->setContentsMargins(6, 4, 6, 4);
    h->setSpacing(4);

    attachBtn_ = new QToolButton(capsule);
    attachBtn_->setText(QStringLiteral("+"));
    attachBtn_->setFixedSize(28, 28);
    attachBtn_->setCursor(Qt::PointingHandCursor);
    attachBtn_->setToolTip(tr("Attach tables — their schema is sent as context"));
    attachBtn_->setStyleSheet(QStringLiteral(
        "QToolButton { background-color: #45475a; color: #bac2de;"
        " border: none; border-radius: 14px; font-size: 16px; font-weight: 600; }"
        "QToolButton:hover { background-color: #585b70; color: #cdd6f4; }"
        "QToolButton::menu-indicator { image: none; width: 0; }"));
    connect(attachBtn_, &QToolButton::clicked, this, &AIChatView::onAttachTablesClicked);
    h->addWidget(attachBtn_);

    inputEdit_ = new QTextEdit(capsule);
    inputEdit_->setPlaceholderText(tr("Ask anything..."));
    inputEdit_->setFrameShape(QFrame::NoFrame);
    inputEdit_->setStyleSheet(QStringLiteral(
        "QTextEdit { background: transparent; color: #cdd6f4;"
        " padding: 4px 6px; font-size: 13px; }"));
    inputEdit_->setFixedHeight(36);
    inputEdit_->installEventFilter(this);
    connect(inputEdit_, &QTextEdit::textChanged,
            this, &AIChatView::onInputTextChanged);
    h->addWidget(inputEdit_, 1);

    sendBtn_ = new QToolButton(capsule);
    sendBtn_->setText(QStringLiteral("↑"));
    sendBtn_->setFixedSize(28, 28);
    sendBtn_->setCursor(Qt::PointingHandCursor);
    sendBtn_->setToolTip(tr("Send"));
    sendBtn_->setStyleSheet(QString(
        "QToolButton { background-color: %1; color: white;"
        " border: none; border-radius: 14px; font-size: 15px; font-weight: 700; }"
        "QToolButton:hover { background-color: %2; }"
        "QToolButton:disabled { background-color: #45475a; color: #6c7086; }")
        .arg(kGrad2, kGrad1));
    sendBtn_->setEnabled(false);
    connect(sendBtn_, &QToolButton::clicked, this, &AIChatView::onSendClicked);
    h->addWidget(sendBtn_);

    outer->addWidget(capsule);
    root->addWidget(bar);
}

// ---- Slots ------------------------------------------------------------

void AIChatView::onSendClicked() {
    const QString text = inputEdit_->toPlainText().trimmed();
    if (text.isEmpty()) return;
    inputEdit_->clear();

    const std::string ctx = buildSchemaContext();
    QString full = text;
    if (!ctx.empty()) full = QString::fromStdString(ctx) + "\n\n" + text;
    vm_->sendMessage(full);

    showMessagesIfHidden();
}

void AIChatView::onSuggestedPromptClicked(const QString& prompt) {
    inputEdit_->setPlainText(prompt);
    inputEdit_->setFocus();
    updateSendButton();
}

void AIChatView::onInputTextChanged() {
    updateSendButton();
}

void AIChatView::updateSendButton() {
    const bool canSend = !inputEdit_->toPlainText().trimmed().isEmpty();
    sendBtn_->setEnabled(canSend);
}

void AIChatView::showMessagesIfHidden() {
    if (bodyStack_ && bodyStack_->currentWidget() != scrollArea_) {
        bodyStack_->setCurrentWidget(scrollArea_);
    }
}

void AIChatView::onAttachTablesClicked() {
    if (!state_ || !state_->adapter()) {
        QMessageBox::information(this, tr("Attach Tables"),
            tr("Open a connection first."));
        return;
    }

    std::vector<std::string> names;
    try {
        for (const auto& info : state_->adapter()->listTables(std::nullopt)) {
            if (info.type == TableKind::Table) names.push_back(info.name);
        }
    } catch (const GridexError& e) {
        QMessageBox::warning(this, tr("Attach Tables"), QString::fromUtf8(e.what()));
        return;
    }
    std::sort(names.begin(), names.end());

    QMenu menu(this);
    if (names.empty()) {
        menu.addAction(tr("(no tables)"))->setEnabled(false);
    } else {
        for (const auto& n : names) {
            const QString qn = QString::fromStdString(n);
            auto* act = menu.addAction(qn);
            act->setCheckable(true);
            act->setChecked(attachedTables_.contains(qn));
            connect(act, &QAction::toggled, this, [this, qn](bool on) {
                if (on) attachedTables_.insert(qn);
                else    attachedTables_.remove(qn);
                rebuildAttachedChips();
            });
        }
    }
    menu.exec(attachBtn_->mapToGlobal(QPoint(0, attachBtn_->height())));
}

void AIChatView::rebuildAttachedChips() {
    while (chipLayout_->count() > 1) {
        auto* item = chipLayout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    QStringList sorted(attachedTables_.begin(), attachedTables_.end());
    sorted.sort();

    for (const QString& name : sorted) {
        auto* chip = new QWidget(chipRow_);
        auto* h = new QHBoxLayout(chip);
        h->setContentsMargins(10, 3, 4, 3);
        h->setSpacing(4);
        chip->setStyleSheet(QStringLiteral(
            "QWidget { background-color: rgba(203, 166, 247, 0.18); border-radius: 10px; }"));

        auto* lbl = new QLabel(QStringLiteral("📊 %1").arg(name), chip);
        lbl->setStyleSheet(QStringLiteral("color: #cba6f7; font-weight: 500;"));
        h->addWidget(lbl);

        auto* close = new QToolButton(chip);
        close->setText(QStringLiteral("×"));
        close->setCursor(Qt::PointingHandCursor);
        close->setStyleSheet(QStringLiteral(
            "QToolButton { background: transparent; border: none;"
            " color: #bac2de; font-size: 13px; padding: 0 4px; }"
            "QToolButton:hover { color: #f38ba8; }"));
        connect(close, &QToolButton::clicked, this, [this, name]() {
            attachedTables_.remove(name);
            rebuildAttachedChips();
        });
        h->addWidget(close);

        chipLayout_->insertWidget(chipLayout_->count() - 1, chip);
    }
    chipRow_->setVisible(!attachedTables_.isEmpty());
}

std::string AIChatView::buildSchemaContext() {
    if (attachedTables_.isEmpty() || !state_ || !state_->adapter()) return {};

    auto* adapter = state_->adapter();
    std::string out = "--- Schema Context ---\n";
    QStringList sorted(attachedTables_.begin(), attachedTables_.end());
    sorted.sort();
    for (const QString& name : sorted) {
        try {
            const auto desc = adapter->describeTable(name.toStdString(), std::nullopt);
            out += desc.toDDL(sqlDialect(adapter->databaseType()));
            out += "\n";
        } catch (...) {
            out += "-- (failed to describe " + name.toStdString() + ")\n";
        }
    }
    return out;
}

void AIChatView::onMessagesChanged() {
    rebuildMessages();
    if (!vm_->messages().empty()) showMessagesIfHidden();
    else if (bodyStack_) bodyStack_->setCurrentWidget(welcomePage_);
}

void AIChatView::onLoadingChanged(bool loading) {
    sendBtn_->setEnabled(!loading && !inputEdit_->toPlainText().trimmed().isEmpty());
    inputEdit_->setEnabled(!loading);

    // Remove any existing typing indicator first.
    if (typingWidget_) {
        typingWidget_->deleteLater();
        typingWidget_ = nullptr;
    }
    if (loading) {
        // Insert typing indicator row before the trailing stretch.
        auto* row = new QWidget(messagesWidget_);
        auto* rowH = new QHBoxLayout(row);
        rowH->setContentsMargins(0, 0, 0, 0);
        rowH->setSpacing(10);
        auto* avatar = new QLabel(row);
        avatar->setFixedSize(28, 28);
        avatar->setPixmap(makeGradientGlyphPixmap(
            28, QStringLiteral("✨"),
            QColor(kGrad1), QColor(kGrad2), true));
        rowH->addWidget(avatar, 0, Qt::AlignTop);
        rowH->addWidget(new TypingIndicator(row));
        rowH->addStretch();
        const int stretchIdx = messagesLayout_->count() - 1;
        messagesLayout_->insertWidget(stretchIdx, row);
        typingWidget_ = row;

        QTimer::singleShot(50, this, [this]() {
            if (scrollArea_) {
                QScrollBar* sb = scrollArea_->verticalScrollBar();
                sb->setValue(sb->maximum());
            }
        });
    }
}

void AIChatView::onErrorOccurred(const QString& message) {
    QMessageBox::warning(this, tr("AI Error"), message);
}

void AIChatView::refreshHeader() {
    if (!modelStatus_ || !vm_) return;
    const QString prov = vm_->selectedProvider();
    const QString model = vm_->selectedModel();
    modelStatus_->setText(model.isEmpty()
        ? tr("Not configured")
        : QStringLiteral("%1 · %2").arg(prov, model));

    // Status dot: green when model is set (assume key present via Settings),
    // yellow/warn when nothing.
    const bool configured = !model.isEmpty();
    statusDot_->setStyleSheet(QString(
        "QLabel { background-color: %1; border-radius: 4px; }")
        .arg(configured ? QStringLiteral("#a6e3a1") : QStringLiteral("#f9e2af")));
}

// ---- Message rendering ------------------------------------------------

namespace {

// Build the bubble content for a single message — list of widgets
// (text bubble and/or code blocks) stacked vertically.
QWidget* makeBubbleColumn(const ChatMessage& msg, QWidget* parent) {
    const bool isUser = (msg.role == LLMMessage::Role::User);
    auto* col = new QWidget(parent);
    auto* v = new QVBoxLayout(col);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);

    // Sender label above the bubble.
    auto* sender = new QLabel(isUser ? QObject::tr("You") : QObject::tr("Assistant"), col);
    sender->setStyleSheet(QStringLiteral("color: #7f849c; font-size: 10px; font-weight: 600;"));
    sender->setAlignment(isUser ? Qt::AlignRight : Qt::AlignLeft);
    v->addWidget(sender);

    // Parse markdown segments.
    const QString text = QString::fromStdString(msg.content);
    const auto segments = parseSegments(text);

    if (segments.empty()) {
        auto* lbl = new QLabel(text.isEmpty() ? QStringLiteral(" ") : text, col);
        lbl->setWordWrap(true);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lbl->setContentsMargins(12, 9, 12, 9);
        lbl->setStyleSheet(isUser
            ? QString("QLabel { background-color: %1; color: white;"
                      " border-top-left-radius: 14px; border-top-right-radius: 14px;"
                      " border-bottom-left-radius: 14px; border-bottom-right-radius: 4px;"
                      " font-size: 13px; }").arg(kGrad2)
            : QStringLiteral("QLabel { background-color: #313244; color: #cdd6f4;"
                      " border-top-left-radius: 14px; border-top-right-radius: 14px;"
                      " border-bottom-left-radius: 4px; border-bottom-right-radius: 14px;"
                      " font-size: 13px; }"));
        v->addWidget(lbl);
    } else {
        for (const auto& seg : segments) {
            if (seg.isCode) {
                v->addWidget(makeCodeBlock(seg.language, seg.body, col));
            } else {
                auto* lbl = new QLabel(seg.body, col);
                lbl->setWordWrap(true);
                lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
                lbl->setContentsMargins(12, 9, 12, 9);
                lbl->setStyleSheet(isUser
                    ? QString("QLabel { background-color: %1; color: white;"
                              " border-top-left-radius: 14px; border-top-right-radius: 14px;"
                              " border-bottom-left-radius: 14px; border-bottom-right-radius: 4px;"
                              " font-size: 13px; }").arg(kGrad2)
                    : QStringLiteral("QLabel { background-color: #313244; color: #cdd6f4;"
                              " border-top-left-radius: 14px; border-top-right-radius: 14px;"
                              " border-bottom-left-radius: 4px; border-bottom-right-radius: 14px;"
                              " font-size: 13px; }"));
                v->addWidget(lbl);
            }
        }
    }
    return col;
}

}  // namespace

void AIChatView::rebuildMessages() {
    // Clear everything except trailing stretch.
    while (messagesLayout_->count() > 1) {
        auto* item = messagesLayout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    typingWidget_ = nullptr;  // cleared above

    const auto& msgs = vm_->messages();
    for (const auto& msg : msgs) {
        const bool isUser = (msg.role == LLMMessage::Role::User);

        auto* row = new QWidget(messagesWidget_);
        auto* rowH = new QHBoxLayout(row);
        rowH->setContentsMargins(0, 0, 0, 0);
        rowH->setSpacing(10);

        auto addAvatar = [&]() {
            auto* avatar = new QLabel(row);
            avatar->setFixedSize(28, 28);
            avatar->setPixmap(makeGradientGlyphPixmap(
                28,
                isUser ? QStringLiteral("🧑") : QStringLiteral("✨"),
                isUser ? QColor("#585b70") : QColor(kGrad1),
                isUser ? QColor("#45475a") : QColor(kGrad2),
                /*circle=*/true));
            rowH->addWidget(avatar, 0, Qt::AlignTop);
        };

        if (!isUser) addAvatar();
        else rowH->addSpacing(40);

        rowH->addWidget(makeBubbleColumn(msg, row), 1,
            isUser ? Qt::AlignRight : Qt::AlignLeft);

        if (isUser) addAvatar();
        else rowH->addSpacing(40);

        const int stretchIdx = messagesLayout_->count() - 1;
        messagesLayout_->insertWidget(stretchIdx, row);
    }

    // Auto-scroll to bottom.
    QTimer::singleShot(0, this, [this]() {
        if (scrollArea_) {
            QScrollBar* sb = scrollArea_->verticalScrollBar();
            sb->setValue(sb->maximum());
        }
    });
}

bool AIChatView::eventFilter(QObject* obj, QEvent* event) {
    if (obj == inputEdit_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && !(ke->modifiers() & Qt::ShiftModifier)) {
            onSendClicked();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

}  // namespace gridex
