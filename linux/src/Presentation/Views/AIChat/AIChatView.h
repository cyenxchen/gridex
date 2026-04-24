#pragma once

#include <QPointer>
#include <QSet>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTextEdit;
class QToolButton;
class QVBoxLayout;

namespace gridex {

class AIChatViewModel;
class SecretStore;
class WorkspaceState;

// AI chat panel. Provider/model/API-key configuration lives in the
// Preferences dialog (File → Preferences); this tab is just the chat.
// Visual design mirrors the macOS SwiftUI implementation: brand header,
// welcome screen with suggested prompts, avatar bubbles with asymmetric
// rounded corners, gradient user messages, typing indicator, code blocks.
class AIChatView : public QWidget {
    Q_OBJECT

public:
    explicit AIChatView(SecretStore*    secretStore,
                        WorkspaceState* state   = nullptr,
                        QWidget*        parent  = nullptr);

private slots:
    void onSendClicked();
    void onMessagesChanged();
    void onLoadingChanged(bool loading);
    void onErrorOccurred(const QString& message);
    void onAttachTablesClicked();
    void onSuggestedPromptClicked(const QString& prompt);
    void onInputTextChanged();

private:
    void buildUi();
    void buildHeader(QVBoxLayout* root);
    void buildWelcomePage();
    void buildMessagesPage();
    void buildInputBar(QVBoxLayout* root);
    void rebuildMessages();
    void rebuildAttachedChips();
    void refreshHeader();
    void updateSendButton();
    void showMessagesIfHidden();
    std::string buildSchemaContext();
    bool eventFilter(QObject* obj, QEvent* event) override;

    AIChatViewModel* vm_           = nullptr;
    QPointer<WorkspaceState> state_;

    // Header
    QLabel*       brandTitle_     = nullptr;
    QLabel*       brandIcon_      = nullptr;   // gradient sparkle
    QLabel*       modelStatus_    = nullptr;   // "gemini-2.5-flash" / "Not configured"
    QLabel*       statusDot_      = nullptr;   // green dot when key present
    QToolButton*  newChatBtn_     = nullptr;

    // Body stack: welcome / messages
    QStackedWidget* bodyStack_    = nullptr;

    // Welcome page
    QWidget*      welcomePage_    = nullptr;

    // Messages page
    QScrollArea*  scrollArea_     = nullptr;
    QWidget*      messagesWidget_ = nullptr;
    QVBoxLayout*  messagesLayout_ = nullptr;
    QWidget*      typingWidget_   = nullptr;   // transient indicator

    // Attached-tables chip row
    QWidget*      chipRow_        = nullptr;
    QHBoxLayout*  chipLayout_     = nullptr;
    QSet<QString> attachedTables_;

    // Input capsule
    QToolButton*  attachBtn_      = nullptr;
    QTextEdit*    inputEdit_      = nullptr;
    QToolButton*  sendBtn_        = nullptr;
};

}  // namespace gridex
