#pragma once

#include <QWidget>

#include "Core/Models/Database/ConnectionConfig.h"

class QLabel;

namespace gridex {

// Single connection row matching macOS ConnectionRow layout:
//   [colorBar 3x28] [DBIcon 32x32] [Name + envBadge + subtitle] [spacer] [TYPE]
// Selection + hover are driven by the parent QListWidget's state through
// setSelected(bool).
class ConnectionRowWidget : public QWidget {
    Q_OBJECT

public:
    explicit ConnectionRowWidget(const ConnectionConfig& config, QWidget* parent = nullptr);

    void setSelected(bool selected);
    [[nodiscard]] QString connectionId() const { return connectionId_; }

private:
    void buildUi(const ConnectionConfig& config);
    void applyPalette();

    QString connectionId_;
    bool    selected_ = false;

    QLabel* colorBar_  = nullptr;
    QWidget* iconBox_  = nullptr;
    QLabel* iconLetter_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    QLabel* envBadge_  = nullptr;
    QLabel* subtitle_  = nullptr;
    QLabel* typeLabel_ = nullptr;
};

}
