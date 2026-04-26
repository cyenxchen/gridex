#pragma once

#include <optional>
#include <string>

#include <QDialog>
#include <QString>

#include "Core/Models/Database/ConnectionConfig.h"

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QProcess;

namespace gridex {

// Modal dialog that runs pg_dump / mysqldump / sqlite3 (or their restore
// counterparts) asynchronously and streams stderr into a log area.
class BackupProgressDialog : public QDialog {
    Q_OBJECT

public:
    enum class Mode { Backup, Restore };

    explicit BackupProgressDialog(Mode mode,
                                  const ConnectionConfig& cfg,
                                  const std::optional<std::string>& password,
                                  const QString& filePath,
                                  QWidget* parent = nullptr);
    ~BackupProgressDialog();

    // Start the process. Call once after showing the dialog.
    void start();

    bool succeeded() const { return succeeded_; }

private slots:
    void onReadyRead();
    void onFinished(int exitCode, int exitStatus);
    void onCancel();

private:
    void buildUi(const QString& dbName);
    void appendLog(const QString& line);
    void markDone(bool ok, const QString& extra = {});

    Mode                       mode_;
    ConnectionConfig           cfg_;
    std::optional<std::string> password_;
    QString                    filePath_;
    bool                       succeeded_ = false;

    QLabel*        headerLabel_  = nullptr;
    QProgressBar*  progressBar_  = nullptr;
    QPlainTextEdit* logView_     = nullptr;
    QPushButton*   cancelBtn_    = nullptr;
    QPushButton*   closeBtn_     = nullptr;

    QProcess* process_ = nullptr;
};

}  // namespace gridex
