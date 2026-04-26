#pragma once

#include <QString>
#include <vector>

#include "Core/Models/Schema/SchemaSnapshot.h"

class QGraphicsScene;
class QWidget;

namespace gridex {

class ERDiagramExporter {
public:
    static void exportAsSvg(QGraphicsScene* scene, QWidget* parent);
    static void exportAsPdf(QGraphicsScene* scene, QWidget* parent);
    static void exportAsDbml(const std::vector<TableDescription>& tables, QWidget* parent);
    static void exportAsMermaid(const std::vector<TableDescription>& tables, QWidget* parent);

private:
    static QString dbmlFor(const std::vector<TableDescription>& tables);
    static QString mermaidFor(const std::vector<TableDescription>& tables);
    static QString dbmlType(const std::string& sqlType);
};

}
