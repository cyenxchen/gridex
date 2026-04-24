#include "Presentation/Views/ERDiagram/ERDiagramExporter.h"

#include <QFile>
#include <QFileDialog>
#include <QGraphicsScene>
#include <QMessageBox>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QSvgGenerator>
#include <QTextStream>

namespace gridex {

namespace {

QString toUpperName(const std::string& name) {
    return QString::fromStdString(name).toUpper();
}

}

void ERDiagramExporter::exportAsSvg(QGraphicsScene* scene, QWidget* parent) {
    if (!scene || scene->items().isEmpty()) {
        QMessageBox::information(parent, QObject::tr("Export ER Diagram"),
                                 QObject::tr("No diagram to export."));
        return;
    }

    const QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(
        parent, QObject::tr("Export ER Diagram as SVG"),
        defaultDir + QStringLiteral("/er-diagram.svg"),
        QObject::tr("SVG Image (*.svg)"));
    if (path.isEmpty()) return;

    const QString outPath = path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)
        ? path : path + QStringLiteral(".svg");

    const QRectF source = scene->itemsBoundingRect().adjusted(-40, -40, 40, 40);

    QSvgGenerator generator;
    generator.setFileName(outPath);
    generator.setSize(source.size().toSize());
    generator.setViewBox(source);
    generator.setTitle(QStringLiteral("ER Diagram"));

    QPainter painter;
    if (!painter.begin(&generator)) {
        QMessageBox::warning(parent, QObject::tr("Export ER Diagram"),
                             QObject::tr("Failed to write SVG to:\n%1").arg(outPath));
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    scene->render(&painter, QRectF(), source);
    painter.end();
}

void ERDiagramExporter::exportAsPdf(QGraphicsScene* scene, QWidget* parent) {
    if (!scene || scene->items().isEmpty()) {
        QMessageBox::information(parent, QObject::tr("Export ER Diagram"),
                                 QObject::tr("No diagram to export."));
        return;
    }

    const QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(
        parent, QObject::tr("Export ER Diagram as PDF"),
        defaultDir + QStringLiteral("/er-diagram.pdf"),
        QObject::tr("PDF Document (*.pdf)"));
    if (path.isEmpty()) return;

    const QString outPath = path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)
        ? path : path + QStringLiteral(".pdf");

    const QRectF source = scene->itemsBoundingRect().adjusted(-40, -40, 40, 40);

    QPdfWriter writer(outPath);
    writer.setPageSize(QPageSize(source.size(), QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter painter;
    if (!painter.begin(&writer)) {
        QMessageBox::warning(parent, QObject::tr("Export ER Diagram"),
                             QObject::tr("Failed to write PDF to:\n%1").arg(outPath));
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    scene->render(&painter, QRectF(), source);
    painter.end();
}

void ERDiagramExporter::exportAsDbml(const std::vector<TableDescription>& tables,
                                      QWidget* parent) {
    if (tables.empty()) {
        QMessageBox::information(parent, QObject::tr("Export ER Diagram"),
                                 QObject::tr("No schema to export."));
        return;
    }

    const QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(
        parent, QObject::tr("Export Schema as DBML"),
        defaultDir + QStringLiteral("/schema.dbml"),
        QObject::tr("DBML File (*.dbml)"));
    if (path.isEmpty()) return;

    const QString outPath = path.endsWith(QStringLiteral(".dbml"), Qt::CaseInsensitive)
        ? path : path + QStringLiteral(".dbml");

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(parent, QObject::tr("Export ER Diagram"),
                             QObject::tr("Failed to write DBML to:\n%1").arg(outPath));
        return;
    }
    QTextStream out(&file);
    out << dbmlFor(tables);
}

void ERDiagramExporter::exportAsMermaid(const std::vector<TableDescription>& tables,
                                         QWidget* parent) {
    if (tables.empty()) {
        QMessageBox::information(parent, QObject::tr("Export ER Diagram"),
                                 QObject::tr("No schema to export."));
        return;
    }

    const QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(
        parent, QObject::tr("Export Schema as Mermaid"),
        defaultDir + QStringLiteral("/er-diagram.md"),
        QObject::tr("Markdown File (*.md)"));
    if (path.isEmpty()) return;

    const QString outPath = path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)
        ? path : path + QStringLiteral(".md");

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(parent, QObject::tr("Export ER Diagram"),
                             QObject::tr("Failed to write Mermaid to:\n%1").arg(outPath));
        return;
    }
    QTextStream out(&file);
    out << mermaidFor(tables);
}

QString ERDiagramExporter::dbmlType(const std::string& sqlType) {
    const QString t = QString::fromStdString(sqlType).toLower();
    if (t.startsWith(QStringLiteral("character varying")) ||
        t.startsWith(QStringLiteral("varchar")))       return QStringLiteral("varchar");
    if (t.startsWith(QStringLiteral("timestamp")))     return QStringLiteral("timestamp");
    if (t == QStringLiteral("integer") ||
        t == QStringLiteral("int4") ||
        t == QStringLiteral("int"))                    return QStringLiteral("int");
    if (t == QStringLiteral("bigint") ||
        t == QStringLiteral("int8"))                   return QStringLiteral("bigint");
    if (t == QStringLiteral("smallint") ||
        t == QStringLiteral("int2"))                   return QStringLiteral("smallint");
    if (t == QStringLiteral("boolean") ||
        t == QStringLiteral("bool"))                   return QStringLiteral("boolean");
    if (t == QStringLiteral("text"))                   return QStringLiteral("text");
    if (t == QStringLiteral("uuid"))                   return QStringLiteral("uuid");
    if (t.startsWith(QStringLiteral("numeric")) ||
        t.startsWith(QStringLiteral("decimal")))       return QStringLiteral("decimal");
    if (t.startsWith(QStringLiteral("double")) ||
        t == QStringLiteral("float8"))                 return QStringLiteral("float");
    if (t == QStringLiteral("real") ||
        t == QStringLiteral("float4"))                 return QStringLiteral("float");
    if (t == QStringLiteral("jsonb") ||
        t == QStringLiteral("json"))                   return QStringLiteral("json");
    if (t == QStringLiteral("date"))                   return QStringLiteral("date");
    if (t == QStringLiteral("time") ||
        t.startsWith(QStringLiteral("time ")))         return QStringLiteral("time");
    return QString::fromStdString(sqlType).left(32);
}

QString ERDiagramExporter::dbmlFor(const std::vector<TableDescription>& tables) {
    QString out;
    QTextStream s(&out);

    for (const auto& td : tables) {
        s << "Table " << QString::fromStdString(td.name) << " {\n";
        for (const auto& col : td.columns) {
            s << "  " << QString::fromStdString(col.name)
              << " " << dbmlType(col.dataType);
            QStringList notes;
            if (col.isPrimaryKey)      notes << QStringLiteral("pk");
            if (!col.isNullable)       notes << QStringLiteral("not null");
            if (col.isAutoIncrement)   notes << QStringLiteral("increment");
            if (!notes.isEmpty())
                s << " [" << notes.join(QStringLiteral(", ")) << "]";
            s << "\n";
        }
        s << "}\n\n";
    }

    for (const auto& td : tables) {
        for (const auto& fk : td.foreignKeys) {
            if (fk.columns.empty() || fk.referencedColumns.empty()) continue;
            s << "Ref: "
              << QString::fromStdString(td.name) << "."
              << QString::fromStdString(fk.columns[0])
              << " > "
              << QString::fromStdString(fk.referencedTable) << "."
              << QString::fromStdString(fk.referencedColumns[0])
              << "\n";
        }
    }

    return out;
}

QString ERDiagramExporter::mermaidFor(const std::vector<TableDescription>& tables) {
    QString out;
    QTextStream s(&out);

    s << "erDiagram\n";

    for (const auto& td : tables) {
        for (const auto& fk : td.foreignKeys) {
            s << "  " << toUpperName(td.name)
              << " ||--o{ "
              << toUpperName(fk.referencedTable)
              << " : \"\"\n";
        }
    }

    s << "\n";

    for (const auto& td : tables) {
        s << "  " << toUpperName(td.name) << " {\n";
        for (const auto& col : td.columns) {
            const QString typePart = dbmlType(col.dataType).replace(
                QStringLiteral(" "), QStringLiteral("_"));
            s << "    " << typePart
              << " " << QString::fromStdString(col.name);
            if (col.isPrimaryKey) s << " PK";
            s << "\n";
        }
        s << "  }\n";
    }

    return out;
}

}  // namespace gridex
