#pragma once

#include <QWidget>
#include <string>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QVBoxLayout;

namespace gridex {

class RedisAdapter;

class RedisKeyDetailView : public QWidget {
    Q_OBJECT
public:
    explicit RedisKeyDetailView(RedisAdapter* adapter,
                                const QString& key,
                                QWidget* parent = nullptr);

    void reload();

signals:
    void keyDeleted(const QString& key);

private slots:
    void onPersist();
    void onExpire();
    void onDeleteKey();
    void onStringSave();
    void onListPushHead();
    void onListPushTail();
    void onListPop(bool head);
    void onListRemove();
    void onHashAdd();
    void onHashDelete();
    void onSetAdd();
    void onSetRemove();
    void onZSetAdd();
    void onZSetRemove();

private:
    void buildUi();
    void buildHeader(QVBoxLayout* root);
    void buildStringPage();
    void buildListPage();
    void buildHashPage();
    void buildSetPage();
    void buildZSetPage();
    void buildStreamPage();
    void loadContent(const std::string& type);
    void showError(const QString& msg);

    RedisAdapter*   adapter_    = nullptr;
    QString         key_;

    QLabel*         typeBadge_  = nullptr;
    QLabel*         ttlLabel_   = nullptr;
    QSpinBox*       ttlSpin_    = nullptr;
    QPushButton*    persistBtn_ = nullptr;
    QPushButton*    expireBtn_  = nullptr;
    QStackedWidget* body_       = nullptr;

    QWidget*        stringPage_ = nullptr;
    QPlainTextEdit* strEdit_    = nullptr;

    QWidget*        listPage_   = nullptr;
    QListWidget*    listWidget_ = nullptr;
    QLineEdit*      listInput_  = nullptr;

    QWidget*        hashPage_   = nullptr;
    QTableWidget*   hashTable_  = nullptr;
    QLineEdit*      hashField_  = nullptr;
    QLineEdit*      hashValue_  = nullptr;

    QWidget*        setPage_    = nullptr;
    QListWidget*    setList_    = nullptr;
    QLineEdit*      setInput_   = nullptr;

    QWidget*        zsetPage_   = nullptr;
    QTableWidget*   zsetTable_  = nullptr;
    QLineEdit*      zsetMember_ = nullptr;
    QLineEdit*      zsetScore_  = nullptr;

    QWidget*        streamPage_   = nullptr;
    QTableWidget*   streamTable_  = nullptr;
};

} // namespace gridex
