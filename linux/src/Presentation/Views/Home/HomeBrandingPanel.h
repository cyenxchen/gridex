#pragma once

#include <QWidget>

namespace gridex {

// Left panel of the HomeView. Matches macOS brandingPanel 260px:
//   [centered logo + title]
//   Spacer
//   Bottom action list: Backup, Restore, divider, New Connection, New Group
class HomeBrandingPanel : public QWidget {
    Q_OBJECT

public:
    explicit HomeBrandingPanel(QWidget* parent = nullptr);

signals:
    void newConnectionRequested();
    void newGroupRequested();
    void backupRequested();
    void restoreRequested();

private:
    void buildUi();
};

}
