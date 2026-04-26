#pragma once

#include <QGraphicsView>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Models/Schema/SchemaSnapshot.h"

class QGraphicsScene;
class QMenu;
class QPushButton;
class QResizeEvent;
class QWheelEvent;

namespace gridex {

class IDatabaseAdapter;

// ER Diagram viewer using QGraphicsView + QGraphicsScene.
// Table cards are draggable nodes; FK edges drawn as bezier arrows.
class ERDiagramView : public QGraphicsView {
    Q_OBJECT

public:
    explicit ERDiagramView(QWidget* parent = nullptr);

    // Loads schema: calls listTables + describeTable, builds nodes + edges.
    void loadSchema(IDatabaseAdapter* adapter,
                    const std::optional<std::string>& schema);

    void clear();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void fitAll();
    void exportAsPng();
    void exportAsSvg();
    void exportAsPdf();
    void exportAsDbml();
    void exportAsMermaid();

private:
    struct TableNode {
        std::string name;
        std::vector<ColumnInfo> columns;
        qreal x = 0;
        qreal y = 0;
    };

    void buildScene(const std::vector<TableDescription>& tables);
    void layoutGrid(std::vector<TableNode>& nodes);

    QGraphicsScene* scene_      = nullptr;
    QPushButton*    fitBtn_     = nullptr;
    QPushButton*    exportBtn_  = nullptr;
    QMenu*          exportMenu_ = nullptr;

    std::unordered_map<std::string, QPointF> nodePositions_;
    std::unordered_map<std::string, int> nodeColumnCounts_;
    std::vector<TableDescription> tables_;
};

}
