#pragma once

#include <QWidget>
#include <string>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace gridex {

class MongodbAdapter;

class MongoCollectionView : public QWidget {
    Q_OBJECT
public:
    explicit MongoCollectionView(MongodbAdapter* adapter,
                                  const QString& collection,
                                  QWidget* parent = nullptr);

    void reload();

private slots:
    void onRefresh();
    void onInsert();
    void onDeleteSelected();
    void onRunFilter();
    void onSaveDocument();
    void onDocumentSelected(QListWidgetItem* item);
    void onPrevPage();
    void onNextPage();

private:
    void buildUi();
    void loadPage();
    void updatePagination(long total);
    void showError(const QString& msg);
    QString buildIdFilter(const QString& idStr) const;

    MongodbAdapter* adapter_     = nullptr;
    QString         collection_;

    QLabel*         titleLabel_  = nullptr;
    QLineEdit*      filterEdit_  = nullptr;
    QSpinBox*       limitSpin_   = nullptr;
    QPushButton*    runBtn_      = nullptr;
    QPushButton*    insertBtn_   = nullptr;
    QPushButton*    deleteBtn_   = nullptr;
    QPushButton*    refreshBtn_  = nullptr;

    QListWidget*    docList_     = nullptr;
    QPlainTextEdit* jsonEditor_  = nullptr;
    QPushButton*    saveBtn_     = nullptr;
    QLabel*         statusLabel_ = nullptr;

    QPushButton*    prevBtn_     = nullptr;
    QPushButton*    nextBtn_     = nullptr;
    QLabel*         pageLabel_   = nullptr;

    int  currentSkip_  = 0;
    int  currentLimit_ = 50;
    long totalCount_   = 0;
    QString currentFilter_;
    QString selectedIdJson_;
};

} // namespace gridex
