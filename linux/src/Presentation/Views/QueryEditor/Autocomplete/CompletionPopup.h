#pragma once

#include "Presentation/Views/QueryEditor/Autocomplete/CompletionModels.h"

#include <QFrame>
#include <vector>

class QListWidget;
class QListWidgetItem;
class QLabel;

namespace gridex {

// Borderless popup showing up to 10 completion rows + footer hint.
// Arrow keys navigate; Enter/Tab emits accepted() with the selected item.
// Escape emits dismissed(). Mouse click also accepts.
class CompletionPopup : public QFrame {
    Q_OBJECT
public:
    explicit CompletionPopup(QWidget* parent = nullptr);

    void setItems(const std::vector<CompletionItem>& items);
    bool isEmpty() const { return items_.empty(); }
    int  selectedIndex() const;
    void moveSelection(int delta);
    const CompletionItem* selectedItem() const;

signals:
    void accepted(const CompletionItem& item);
    void dismissed();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildUi();
    QString typeLabel(CompletionType t) const;

    QListWidget* list_   = nullptr;
    QLabel*      footer_ = nullptr;
    std::vector<CompletionItem> items_;
};

}  // namespace gridex
