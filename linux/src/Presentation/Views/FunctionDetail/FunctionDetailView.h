#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace gridex {

class IDatabaseAdapter;

class FunctionDetailView : public QWidget {
    Q_OBJECT

public:
    explicit FunctionDetailView(IDatabaseAdapter* adapter,
                                const QString& schema,
                                const QString& name,
                                bool isProcedure,
                                QWidget* parent = nullptr);

private slots:
    void onCopy();
    void onRefresh();

private:
    void buildUi();
    void loadSource();

    IDatabaseAdapter* adapter_    = nullptr;
    QString           schema_;
    QString           name_;
    bool              isProcedure_ = false;

    QLabel*        headerLabel_  = nullptr;
    QLabel*        badgeLabel_   = nullptr;
    QPlainTextEdit* editor_      = nullptr;
    QPushButton*   copyBtn_      = nullptr;
    QPushButton*   refreshBtn_   = nullptr;
};

}
