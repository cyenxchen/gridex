#include "Presentation/Views/Redis/RedisKeyDetailView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>

#include "Data/Adapters/Redis/RedisAdapter.h"

namespace gridex {

RedisKeyDetailView::RedisKeyDetailView(RedisAdapter* adapter,
                                       const QString& key,
                                       QWidget* parent)
    : QWidget(parent), adapter_(adapter), key_(key) {
    buildUi();
    reload();
}

void RedisKeyDetailView::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    buildHeader(root);

    auto* div = new QFrame(this);
    div->setFrameShape(QFrame::HLine);
    div->setFrameShadow(QFrame::Sunken);
    root->addWidget(div);

    body_ = new QStackedWidget(this);
    buildStringPage();
    buildListPage();
    buildHashPage();
    buildSetPage();
    buildZSetPage();
    buildStreamPage();
    root->addWidget(body_, 1);
}

void RedisKeyDetailView::buildHeader(QVBoxLayout* root) {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(44);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(10, 0, 10, 0);
    h->setSpacing(8);

    auto* keyLabel = new QLabel(key_, bar);
    keyLabel->setStyleSheet(QStringLiteral("font-weight:600; font-family:monospace;"));
    h->addWidget(keyLabel);

    typeBadge_ = new QLabel(bar);
    typeBadge_->setStyleSheet(
        QStringLiteral("font-size:10px; font-weight:bold; padding:2px 6px;"
                       " border-radius:3px; background:#5c6bc0; color:white;"));
    h->addWidget(typeBadge_);

    ttlLabel_ = new QLabel(bar);
    ttlLabel_->setStyleSheet(QStringLiteral("color:gray; font-size:11px;"));
    h->addWidget(ttlLabel_);

    h->addStretch(1);

    ttlSpin_ = new QSpinBox(bar);
    ttlSpin_->setRange(1, 2147483647);
    ttlSpin_->setValue(3600);
    ttlSpin_->setFixedWidth(80);
    ttlSpin_->setToolTip(tr("TTL in seconds"));
    h->addWidget(ttlSpin_);

    expireBtn_ = new QPushButton(tr("Expire"), bar);
    expireBtn_->setFixedHeight(26);
    connect(expireBtn_, &QPushButton::clicked, this, &RedisKeyDetailView::onExpire);
    h->addWidget(expireBtn_);

    persistBtn_ = new QPushButton(tr("Persist"), bar);
    persistBtn_->setFixedHeight(26);
    connect(persistBtn_, &QPushButton::clicked, this, &RedisKeyDetailView::onPersist);
    h->addWidget(persistBtn_);

    auto* delBtn = new QPushButton(tr("Delete Key"), bar);
    delBtn->setFixedHeight(26);
    delBtn->setStyleSheet(QStringLiteral("color:#e53935;"));
    connect(delBtn, &QPushButton::clicked, this, &RedisKeyDetailView::onDeleteKey);
    h->addWidget(delBtn);

    root->addWidget(bar);
}

void RedisKeyDetailView::buildStringPage() {
    stringPage_ = new QWidget(body_);
    auto* v = new QVBoxLayout(stringPage_);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(6);

    strEdit_ = new QPlainTextEdit(stringPage_);
    strEdit_->setFont(QFont(QStringLiteral("monospace"), 11));
    v->addWidget(strEdit_, 1);

    auto* saveBtn = new QPushButton(tr("Save"), stringPage_);
    saveBtn->setFixedWidth(80);
    connect(saveBtn, &QPushButton::clicked, this, &RedisKeyDetailView::onStringSave);
    v->addWidget(saveBtn, 0, Qt::AlignRight);

    body_->addWidget(stringPage_);
}

void RedisKeyDetailView::buildListPage() {
    listPage_ = new QWidget(body_);
    auto* v = new QVBoxLayout(listPage_);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(6);

    listWidget_ = new QListWidget(listPage_);
    listWidget_->setFont(QFont(QStringLiteral("monospace"), 11));
    v->addWidget(listWidget_, 1);

    auto* row = new QHBoxLayout;
    listInput_ = new QLineEdit(listPage_);
    listInput_->setPlaceholderText(tr("value"));
    row->addWidget(listInput_, 1);

    auto* pushHead = new QPushButton(tr("Push Head"), listPage_);
    connect(pushHead, &QPushButton::clicked, this, &RedisKeyDetailView::onListPushHead);
    row->addWidget(pushHead);

    auto* pushTail = new QPushButton(tr("Push Tail"), listPage_);
    connect(pushTail, &QPushButton::clicked, this, &RedisKeyDetailView::onListPushTail);
    row->addWidget(pushTail);

    auto* popHead = new QPushButton(tr("Pop Head"), listPage_);
    connect(popHead, &QPushButton::clicked, this, [this]{ onListPop(true); });
    row->addWidget(popHead);

    auto* popTail = new QPushButton(tr("Pop Tail"), listPage_);
    connect(popTail, &QPushButton::clicked, this, [this]{ onListPop(false); });
    row->addWidget(popTail);

    auto* remBtn = new QPushButton(tr("Remove"), listPage_);
    connect(remBtn, &QPushButton::clicked, this, &RedisKeyDetailView::onListRemove);
    row->addWidget(remBtn);

    v->addLayout(row);
    body_->addWidget(listPage_);
}

void RedisKeyDetailView::buildHashPage() {
    hashPage_ = new QWidget(body_);
    auto* v = new QVBoxLayout(hashPage_);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(6);

    hashTable_ = new QTableWidget(0, 2, hashPage_);
    hashTable_->setHorizontalHeaderLabels({tr("Field"), tr("Value")});
    hashTable_->horizontalHeader()->setStretchLastSection(true);
    hashTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    hashTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->addWidget(hashTable_, 1);

    auto* row = new QHBoxLayout;
    hashField_ = new QLineEdit(hashPage_);
    hashField_->setPlaceholderText(tr("field"));
    hashField_->setFixedWidth(140);
    row->addWidget(hashField_);

    hashValue_ = new QLineEdit(hashPage_);
    hashValue_->setPlaceholderText(tr("value"));
    row->addWidget(hashValue_, 1);

    auto* addBtn = new QPushButton(tr("Add / Update"), hashPage_);
    connect(addBtn, &QPushButton::clicked, this, &RedisKeyDetailView::onHashAdd);
    row->addWidget(addBtn);

    auto* delBtn = new QPushButton(tr("Delete Row"), hashPage_);
    connect(delBtn, &QPushButton::clicked, this, &RedisKeyDetailView::onHashDelete);
    row->addWidget(delBtn);

    v->addLayout(row);
    body_->addWidget(hashPage_);
}

void RedisKeyDetailView::buildSetPage() {
    setPage_ = new QWidget(body_);
    auto* v = new QVBoxLayout(setPage_);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(6);

    setList_ = new QListWidget(setPage_);
    setList_->setFont(QFont(QStringLiteral("monospace"), 11));
    v->addWidget(setList_, 1);

    auto* row = new QHBoxLayout;
    setInput_ = new QLineEdit(setPage_);
    setInput_->setPlaceholderText(tr("member"));
    row->addWidget(setInput_, 1);

    auto* addBtn = new QPushButton(tr("Add"), setPage_);
    connect(addBtn, &QPushButton::clicked, this, &RedisKeyDetailView::onSetAdd);
    row->addWidget(addBtn);

    auto* remBtn = new QPushButton(tr("Remove"), setPage_);
    connect(remBtn, &QPushButton::clicked, this, &RedisKeyDetailView::onSetRemove);
    row->addWidget(remBtn);

    v->addLayout(row);
    body_->addWidget(setPage_);
}

void RedisKeyDetailView::buildZSetPage() {
    zsetPage_ = new QWidget(body_);
    auto* v = new QVBoxLayout(zsetPage_);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(6);

    zsetTable_ = new QTableWidget(0, 2, zsetPage_);
    zsetTable_->setHorizontalHeaderLabels({tr("Member"), tr("Score")});
    zsetTable_->horizontalHeader()->setStretchLastSection(true);
    zsetTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    zsetTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->addWidget(zsetTable_, 1);

    auto* row = new QHBoxLayout;
    zsetMember_ = new QLineEdit(zsetPage_);
    zsetMember_->setPlaceholderText(tr("member"));
    row->addWidget(zsetMember_, 1);

    zsetScore_ = new QLineEdit(zsetPage_);
    zsetScore_->setPlaceholderText(tr("score"));
    zsetScore_->setFixedWidth(80);
    row->addWidget(zsetScore_);

    auto* addBtn = new QPushButton(tr("Add / Update"), zsetPage_);
    connect(addBtn, &QPushButton::clicked, this, &RedisKeyDetailView::onZSetAdd);
    row->addWidget(addBtn);

    auto* remBtn = new QPushButton(tr("Remove"), zsetPage_);
    connect(remBtn, &QPushButton::clicked, this, &RedisKeyDetailView::onZSetRemove);
    row->addWidget(remBtn);

    v->addLayout(row);
    body_->addWidget(zsetPage_);
}

void RedisKeyDetailView::buildStreamPage() {
    streamPage_ = new QWidget(body_);
    auto* v = new QVBoxLayout(streamPage_);
    v->setContentsMargins(10, 10, 10, 10);

    streamTable_ = new QTableWidget(0, 2, streamPage_);
    streamTable_->setHorizontalHeaderLabels({tr("ID"), tr("Fields")});
    streamTable_->horizontalHeader()->setStretchLastSection(true);
    streamTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->addWidget(streamTable_, 1);

    body_->addWidget(streamPage_);
}

// ---- reload ----

void RedisKeyDetailView::reload() {
    if (!adapter_) return;
    try {
        auto detail = adapter_->keyDetail(key_.toStdString());
        const QString type = QString::fromStdString(detail.type);
        typeBadge_->setText(type.toUpper());

        if (detail.ttl == -1) {
            ttlLabel_->setText(tr("no expiry"));
        } else if (detail.ttl == -2) {
            ttlLabel_->setText(tr("(missing)"));
        } else {
            ttlLabel_->setText(tr("TTL: %1s").arg(detail.ttl));
        }

        loadContent(detail.type);
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
    }
}

void RedisKeyDetailView::loadContent(const std::string& type) {
    try {
        if (type == "string") {
            auto val = adapter_->getString(key_.toStdString());
            strEdit_->setPlainText(QString::fromStdString(val));
            body_->setCurrentWidget(stringPage_);

        } else if (type == "list") {
            auto items = adapter_->lrange(key_.toStdString());
            listWidget_->clear();
            for (const auto& it : items)
                listWidget_->addItem(QString::fromStdString(it));
            body_->setCurrentWidget(listPage_);

        } else if (type == "hash") {
            auto fields = adapter_->hgetall(key_.toStdString());
            hashTable_->setRowCount(0);
            for (const auto& [f, v] : fields) {
                const int r = hashTable_->rowCount();
                hashTable_->insertRow(r);
                hashTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(f)));
                hashTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(v)));
            }
            body_->setCurrentWidget(hashPage_);

        } else if (type == "set") {
            auto members = adapter_->smembers(key_.toStdString());
            setList_->clear();
            for (const auto& m : members)
                setList_->addItem(QString::fromStdString(m));
            body_->setCurrentWidget(setPage_);

        } else if (type == "zset") {
            auto members = adapter_->zrangeWithScores(key_.toStdString());
            zsetTable_->setRowCount(0);
            for (const auto& [m, s] : members) {
                const int r = zsetTable_->rowCount();
                zsetTable_->insertRow(r);
                zsetTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(m)));
                zsetTable_->setItem(r, 1, new QTableWidgetItem(QString::number(s)));
            }
            body_->setCurrentWidget(zsetPage_);

        } else if (type == "stream") {
            auto entries = adapter_->xrevrange(key_.toStdString(), 50);
            streamTable_->setRowCount(0);
            for (const auto& entry : entries) {
                const int r = streamTable_->rowCount();
                streamTable_->insertRow(r);
                streamTable_->setItem(r, 0, new QTableWidgetItem(
                    QString::fromStdString(entry.id)));
                QString fields;
                for (const auto& [fk, fv] : entry.fields)
                    fields += QString::fromStdString(fk) + "=" + QString::fromStdString(fv) + " ";
                streamTable_->setItem(r, 1, new QTableWidgetItem(fields.trimmed()));
            }
            body_->setCurrentWidget(streamPage_);
        }
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
    }
}

// ---- slots ----

void RedisKeyDetailView::onPersist() {
    try {
        adapter_->persistKey(key_.toStdString());
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onExpire() {
    try {
        adapter_->expireKey(key_.toStdString(), ttlSpin_->value());
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onDeleteKey() {
    try {
        adapter_->deleteKey(key_.toStdString());
        emit keyDeleted(key_);
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onStringSave() {
    try {
        adapter_->setString(key_.toStdString(), strEdit_->toPlainText().toStdString());
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onListPushHead() {
    const auto val = listInput_->text().trimmed();
    if (val.isEmpty()) return;
    try {
        adapter_->lpush(key_.toStdString(), val.toStdString());
        listInput_->clear();
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onListPushTail() {
    const auto val = listInput_->text().trimmed();
    if (val.isEmpty()) return;
    try {
        adapter_->rpush(key_.toStdString(), val.toStdString());
        listInput_->clear();
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onListPop(bool head) {
    try {
        if (head) adapter_->lpop(key_.toStdString());
        else      adapter_->rpop(key_.toStdString());
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onListRemove() {
    const auto val = listInput_->text().trimmed();
    if (val.isEmpty()) {
        auto* cur = listWidget_->currentItem();
        if (!cur) return;
        try {
            adapter_->lrem(key_.toStdString(), cur->text().toStdString());
            reload();
        } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
        return;
    }
    try {
        adapter_->lrem(key_.toStdString(), val.toStdString());
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onHashAdd() {
    const auto field = hashField_->text().trimmed();
    if (field.isEmpty()) return;
    try {
        adapter_->hset(key_.toStdString(), field.toStdString(),
                       hashValue_->text().toStdString());
        hashField_->clear();
        hashValue_->clear();
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onHashDelete() {
    const int row = hashTable_->currentRow();
    if (row < 0) return;
    auto* item = hashTable_->item(row, 0);
    if (!item) return;
    try {
        adapter_->hdel(key_.toStdString(), item->text().toStdString());
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onSetAdd() {
    const auto member = setInput_->text().trimmed();
    if (member.isEmpty()) return;
    try {
        adapter_->sadd(key_.toStdString(), member.toStdString());
        setInput_->clear();
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onSetRemove() {
    const auto member = setInput_->text().trimmed();
    if (!member.isEmpty()) {
        try {
            adapter_->srem(key_.toStdString(), member.toStdString());
            setInput_->clear();
            reload();
        } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
        return;
    }
    auto* cur = setList_->currentItem();
    if (!cur) return;
    try {
        adapter_->srem(key_.toStdString(), cur->text().toStdString());
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onZSetAdd() {
    const auto member = zsetMember_->text().trimmed();
    if (member.isEmpty()) return;
    bool ok = false;
    const double score = zsetScore_->text().toDouble(&ok);
    if (!ok) { showError(tr("Invalid score")); return; }
    try {
        adapter_->zadd(key_.toStdString(), score, member.toStdString());
        zsetMember_->clear();
        zsetScore_->clear();
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::onZSetRemove() {
    const auto member = zsetMember_->text().trimmed();
    if (!member.isEmpty()) {
        try {
            adapter_->zrem(key_.toStdString(), member.toStdString());
            zsetMember_->clear();
            reload();
        } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
        return;
    }
    const int row = zsetTable_->currentRow();
    if (row < 0) return;
    auto* item = zsetTable_->item(row, 0);
    if (!item) return;
    try {
        adapter_->zrem(key_.toStdString(), item->text().toStdString());
        reload();
    } catch (const std::exception& e) { showError(QString::fromUtf8(e.what())); }
}

void RedisKeyDetailView::showError(const QString& msg) {
    QMessageBox::warning(this, tr("Redis Error"), msg);
}

} // namespace gridex
