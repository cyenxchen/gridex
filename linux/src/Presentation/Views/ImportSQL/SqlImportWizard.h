#pragma once

#include <memory>
#include <vector>
#include <string>
#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QCheckBox;
class QProgressBar;
class QListWidget;
class QPushButton;

namespace gridex {

class IDatabaseAdapter;

struct ImportError {
    int    stmtIndex;
    int    approxLine;
    QString message;
};

class SqlImportWizard : public QDialog {
    Q_OBJECT

public:
    explicit SqlImportWizard(IDatabaseAdapter* adapter,
                             const QString&    filePath,
                             QWidget*          parent = nullptr);
    ~SqlImportWizard();

private slots:
    void onBrowse();
    void onImport();
    void onStatementDone(int index, int total, const QString& error);
    void onImportFinished();
    void onErrorDoubleClicked(int row);

private:
    void loadPreview();
    void setImporting(bool active);

    IDatabaseAdapter* adapter_;

    // Widgets
    QLabel*        fileLabel_      = nullptr;
    QPushButton*   browseBtn_      = nullptr;
    QLabel*        dialectLabel_   = nullptr;
    QLabel*        stmtCountLabel_ = nullptr;
    QPlainTextEdit* previewEdit_   = nullptr;
    QCheckBox*     transactionChk_ = nullptr;
    QCheckBox*     continueErrChk_ = nullptr;
    QProgressBar*  progressBar_    = nullptr;
    QLabel*        progressLabel_  = nullptr;
    QListWidget*   errorList_      = nullptr;
    QPushButton*   importBtn_      = nullptr;
    QPushButton*   closeBtn_       = nullptr;

    QString                  filePath_;
    std::vector<std::string> statements_;
    std::vector<int>         stmtLines_;   // approx first-line of each stmt
};

}  // namespace gridex
