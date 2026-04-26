#include "Presentation/Views/ERDiagram/ERDiagramView.h"
#include "Presentation/Views/ERDiagram/ERDiagramExporter.h"

#include <cmath>

#include <QAction>
#include <QFileDialog>
#include <QFont>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QImage>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QStyleOptionGraphicsItem>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "Core/Errors/GridexError.h"
#include "Core/Models/Schema/SchemaSnapshot.h"
#include "Core/Protocols/Database/IDatabaseAdapter.h"

namespace gridex {

namespace {

// Card geometry constants (matches macOS ERDiagramCanvas proportions)
constexpr qreal kCardWidth      = 220.0;
constexpr qreal kHeaderHeight   = 32.0;
constexpr qreal kRowHeight      = 22.0;
constexpr qreal kCornerRadius   = 8.0;
constexpr qreal kGridCols       = 4.0;    // columns in grid layout
constexpr qreal kHGap           = 80.0;
constexpr qreal kVGap           = 60.0;
constexpr qreal kPadding        = 60.0;

inline qreal cardHeight(int columnCount) {
    return kHeaderHeight + columnCount * kRowHeight + 4.0;
}

// Abbreviate SQL type to short display form.
QString abbreviateType(const std::string& type) {
    const QString t = QString::fromStdString(type).toLower();
    if (t.startsWith(QStringLiteral("character varying")) ||
        t.startsWith(QStringLiteral("varchar"))) {
        const int p1 = t.indexOf(QLatin1Char('('));
        const int p2 = t.indexOf(QLatin1Char(')'));
        if (p1 != -1 && p2 > p1)
            return QStringLiteral("varchar") + t.mid(p1, p2 - p1 + 1);
        return QStringLiteral("varchar");
    }
    if (t.startsWith(QStringLiteral("timestamp")))  return QStringLiteral("timestamp");
    if (t.startsWith(QStringLiteral("double")))     return QStringLiteral("double");
    if (t == QStringLiteral("integer"))             return QStringLiteral("int");
    if (t == QStringLiteral("boolean"))             return QStringLiteral("bool");
    if (t == QStringLiteral("bigint"))              return QStringLiteral("bigint");
    if (t == QStringLiteral("smallint"))            return QStringLiteral("smallint");
    if (t == QStringLiteral("text"))                return QStringLiteral("text");
    if (t == QStringLiteral("uuid"))                return QStringLiteral("uuid");
    if (t == QStringLiteral("jsonb"))               return QStringLiteral("jsonb");
    if (t == QStringLiteral("json"))                return QStringLiteral("json");
    return QString::fromStdString(type).left(14);
}

// Build a table card QGraphicsItem group (returns the root rect item).
// The item is added to the scene inside this function; position is (0,0) —
// caller translates via QGraphicsItem::setPos.
QGraphicsRectItem* buildTableCard(QGraphicsScene* scene,
                                  const TableDescription& td) {
    const int   cols  = static_cast<int>(td.columns.size());
    const qreal cw    = kCardWidth;
    const qreal ch    = cardHeight(cols);

    // --- outer card rect (movable) ---
    auto* card = new QGraphicsRectItem(0, 0, cw, ch);
    card->setFlag(QGraphicsItem::ItemIsMovable, true);
    card->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    card->setZValue(1.0);

    QPen borderPen(QColor(0x55, 0x55, 0x55, 180));
    borderPen.setWidthF(1.0);
    card->setPen(borderPen);
    card->setBrush(QBrush(QColor(0x2a, 0x2a, 0x2a)));

    // Drop shadow via a slightly-offset duplicate behind the card.
    auto* shadow = new QGraphicsRectItem(2, 3, cw, ch, card);
    shadow->setPen(Qt::NoPen);
    shadow->setBrush(QColor(0, 0, 0, 60));
    shadow->setZValue(-0.1);

    // --- header background ---
    auto* header = new QGraphicsRectItem(0, 0, cw, kHeaderHeight, card);
    header->setPen(Qt::NoPen);
    header->setBrush(QColor(0x1a, 0x6a, 0xc8, 50));  // accent blue tint

    // Header separator line
    auto* hSep = new QGraphicsLineItem(0, kHeaderHeight, cw, kHeaderHeight, card);
    hSep->setPen(QPen(QColor(0x55, 0x55, 0x55, 120), 0.5));

    // --- table name ---
    auto* nameItem = new QGraphicsTextItem(card);
    nameItem->setPos(10, 6);
    QFont nameFont;
    nameFont.setPointSize(9);
    nameFont.setBold(true);
    nameItem->setFont(nameFont);
    nameItem->setDefaultTextColor(QColor(0xec, 0xec, 0xec));
    nameItem->setPlainText(QString::fromStdString(td.name));
    nameItem->setTextWidth(cw - 20);

    // Column count badge (top-right of header)
    const QString countText = QString::number(cols);
    auto* countItem = new QGraphicsTextItem(card);
    QFont countFont;
    countFont.setPointSize(7);
    countItem->setFont(countFont);
    countItem->setDefaultTextColor(QColor(0x88, 0x88, 0x88));
    countItem->setPlainText(countText);
    const qreal cw2 = countItem->boundingRect().width();
    countItem->setPos(cw - cw2 - 8, 9);

    // --- column rows ---
    for (int i = 0; i < cols; ++i) {
        const auto& col  = td.columns[static_cast<std::size_t>(i)];
        const qreal rowY = kHeaderHeight + 2 + i * kRowHeight;

        // Alternating row background
        if (i % 2 == 1) {
            auto* rowBg = new QGraphicsRectItem(0, rowY, cw, kRowHeight, card);
            rowBg->setPen(Qt::NoPen);
            rowBg->setBrush(QColor(0xff, 0xff, 0xff, 6));
        }

        // PK / FK badge
        if (col.isPrimaryKey) {
            auto* badge = new QGraphicsTextItem(card);
            QFont bf; bf.setPointSize(7); bf.setBold(true);
            badge->setFont(bf);
            badge->setDefaultTextColor(QColor(0xd4, 0xa0, 0x17));  // gold
            badge->setPlainText(QStringLiteral("PK"));
            badge->setPos(8, rowY + 3);
        } else {
            // Check if this column is a FK column
            bool isFk = false;
            for (const auto& fk : td.foreignKeys) {
                for (const auto& fcol : fk.columns) {
                    if (fcol == col.name) { isFk = true; break; }
                }
                if (isFk) break;
            }
            if (isFk) {
                auto* badge = new QGraphicsTextItem(card);
                QFont bf; bf.setPointSize(7); bf.setBold(true);
                badge->setFont(bf);
                badge->setDefaultTextColor(QColor(0x5b, 0x9b, 0xd5));  // blue
                badge->setPlainText(QStringLiteral("FK"));
                badge->setPos(8, rowY + 3);
            }
        }

        // Column name
        auto* colName = new QGraphicsTextItem(card);
        QFont cf; cf.setPointSize(9);
        if (col.isPrimaryKey) cf.setBold(true);
        colName->setFont(cf);
        colName->setDefaultTextColor(QColor(0xd8, 0xd8, 0xd8));
        colName->setPlainText(QString::fromStdString(col.name));
        colName->setTextWidth(kCardWidth - 70);
        colName->setPos(28, rowY + 2);

        // Data type (right-aligned)
        auto* typeItem = new QGraphicsTextItem(card);
        QFont tf; tf.setPointSize(8); tf.setFamily(QStringLiteral("monospace"));
        typeItem->setFont(tf);
        typeItem->setDefaultTextColor(QColor(0x77, 0x99, 0x77));
        const QString shortType = abbreviateType(col.dataType);
        typeItem->setPlainText(shortType);
        const qreal tw = typeItem->boundingRect().width();
        typeItem->setPos(cw - tw - 6, rowY + 3);
    }

    scene->addItem(card);
    return card;
}

// Dynamic FK edge that recalculates its path every paint based on current
// positions of the source and target card items. Follows nodes when dragged.
class FKEdgeItem : public QGraphicsItem {
public:
    FKEdgeItem(QGraphicsRectItem* srcCard, int srcRowIdx,
               QGraphicsRectItem* tgtCard, int tgtRowIdx)
        : srcCard_(srcCard), tgtCard_(tgtCard),
          srcRowIdx_(srcRowIdx), tgtRowIdx_(tgtRowIdx) {
        setZValue(0.0);
    }

    QRectF boundingRect() const override {
        const auto s = srcPoint();
        const auto t = tgtPoint();
        return QRectF(std::min(s.x(), t.x()) - 20,
                      std::min(s.y(), t.y()) - 20,
                      std::abs(t.x() - s.x()) + 40,
                      std::abs(t.y() - s.y()) + 40);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        const auto src = srcPoint();
        const auto tgt = tgtPoint();
        const qreal midX = (src.x() + tgt.x()) / 2.0;

        QPainterPath path;
        path.moveTo(src);
        path.cubicTo(QPointF(midX, src.y()), QPointF(midX, tgt.y()), tgt);

        QPen p(QColor(0x5b, 0x9b, 0xd5, 160));
        p.setWidthF(1.5);
        painter->setPen(p);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);

        // Arrowhead
        const double angle = std::atan2(tgt.y() - src.y(), tgt.x() - src.x());
        const double aLen = 8.0, aAng = M_PI / 6.0;
        QPolygonF arrow;
        arrow << tgt
              << QPointF(tgt.x() - aLen * std::cos(angle - aAng),
                         tgt.y() - aLen * std::sin(angle - aAng))
              << QPointF(tgt.x() - aLen * std::cos(angle + aAng),
                         tgt.y() - aLen * std::sin(angle + aAng));
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0x5b, 0x9b, 0xd5, 160));
        painter->drawPolygon(arrow);
    }

private:
    QPointF srcPoint() const {
        const auto pos = srcCard_->pos();
        const bool leftOfTarget = pos.x() < tgtCard_->pos().x();
        const qreal y = pos.y() + kHeaderHeight + (srcRowIdx_ + 0.5) * kRowHeight;
        return QPointF(leftOfTarget ? pos.x() + kCardWidth : pos.x(), y);
    }
    QPointF tgtPoint() const {
        const auto pos = tgtCard_->pos();
        const bool leftOfSource = pos.x() < srcCard_->pos().x();
        const qreal y = pos.y() + kHeaderHeight + (tgtRowIdx_ + 0.5) * kRowHeight;
        return QPointF(leftOfSource ? pos.x() + kCardWidth : pos.x(), y);
    }

    QGraphicsRectItem* srcCard_;
    QGraphicsRectItem* tgtCard_;
    int srcRowIdx_;
    int tgtRowIdx_;
};

}  // namespace

// ---------------------------------------------------------------------------

ERDiagramView::ERDiagramView(QWidget* parent)
    : QGraphicsView(parent) {
    scene_ = new QGraphicsScene(this);
    setScene(scene_);

    setRenderHint(QPainter::Antialiasing);
    // Update the scene whenever items move so FKEdgeItem repaints.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setBackgroundBrush(QColor(0x18, 0x18, 0x18));
    setFrameShape(QFrame::NoFrame);

    // Hide the dropdown arrow only — colors come from the global style.qss.
    const QString hideMenuArrow =
        QStringLiteral("QPushButton::menu-indicator { image: none; width: 0; }");

    // Fit-all button (top-right corner)
    fitBtn_ = new QPushButton(QStringLiteral("⤢ Fit"), this);
    fitBtn_->setFixedSize(60, 26);
    fitBtn_->setToolTip(tr("Fit all tables in view"));
    connect(fitBtn_, &QPushButton::clicked, this, &ERDiagramView::fitAll);
    fitBtn_->raise();

    // Export button with dropdown menu (placed left of Fit).
    exportMenu_ = new QMenu(this);
    auto* exportPngAct = exportMenu_->addAction(tr("Export as PNG..."));
    connect(exportPngAct, &QAction::triggered, this, &ERDiagramView::exportAsPng);
    auto* exportSvgAct = exportMenu_->addAction(tr("Export as SVG..."));
    connect(exportSvgAct, &QAction::triggered, this, &ERDiagramView::exportAsSvg);
    auto* exportPdfAct = exportMenu_->addAction(tr("Export as PDF..."));
    connect(exportPdfAct, &QAction::triggered, this, &ERDiagramView::exportAsPdf);
    exportMenu_->addSeparator();
    auto* exportDbmlAct = exportMenu_->addAction(tr("Export as DBML..."));
    connect(exportDbmlAct, &QAction::triggered, this, &ERDiagramView::exportAsDbml);
    auto* exportMermaidAct = exportMenu_->addAction(tr("Export as Mermaid (.md)..."));
    connect(exportMermaidAct, &QAction::triggered, this, &ERDiagramView::exportAsMermaid);

    exportBtn_ = new QPushButton(QStringLiteral("⇩ Export ▾"), this);
    exportBtn_->setFixedSize(84, 26);
    exportBtn_->setToolTip(tr("Export ER diagram"));
    exportBtn_->setStyleSheet(hideMenuArrow);
    exportBtn_->setMenu(exportMenu_);
    exportBtn_->raise();

    // Initial placement. resizeEvent repositions on every resize.
    fitBtn_->move(width() - fitBtn_->width() - 12, 8);
    exportBtn_->move(width() - fitBtn_->width() - exportBtn_->width() - 20, 8);
}

void ERDiagramView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    fitBtn_->move(width() - fitBtn_->width() - 12, 8);
    exportBtn_->move(width() - fitBtn_->width() - exportBtn_->width() - 20, 8);
}

void ERDiagramView::wheelEvent(QWheelEvent* event) {
    const qreal factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    scale(factor, factor);
    event->accept();
}

void ERDiagramView::fitAll() {
    if (scene_->items().isEmpty()) return;
    fitInView(scene_->itemsBoundingRect().adjusted(-40, -40, 40, 40),
              Qt::KeepAspectRatio);
}

void ERDiagramView::clear() {
    scene_->clear();
    nodePositions_.clear();
    nodeColumnCounts_.clear();
    tables_.clear();
}

void ERDiagramView::exportAsPng() {
    if (scene_->items().isEmpty()) {
        QMessageBox::information(this, tr("Export ER Diagram"),
                                 tr("No diagram to export."));
        return;
    }

    const QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString defaultPath = defaultDir + QStringLiteral("/er-diagram.png");

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export ER Diagram as PNG"), defaultPath,
        tr("PNG Image (*.png)"));
    if (path.isEmpty()) return;

    // Render full scene bounding rect (not viewport) so offscreen tables
    // are included. 2x scale keeps text sharp on high-DPI displays.
    const QRectF source = scene_->itemsBoundingRect().adjusted(-40, -40, 40, 40);
    constexpr qreal scaleFactor = 2.0;
    const QSize imageSize(
        static_cast<int>(std::ceil(source.width()  * scaleFactor)),
        static_cast<int>(std::ceil(source.height() * scaleFactor)));

    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(backgroundBrush().color());

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    scene_->render(&painter, QRectF(QPointF(0, 0), imageSize), source);
    painter.end();

    const QString outPath = path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)
        ? path
        : path + QStringLiteral(".png");

    if (!image.save(outPath, "PNG")) {
        QMessageBox::warning(this, tr("Export ER Diagram"),
                             tr("Failed to write PNG to:\n%1").arg(outPath));
    }
}

void ERDiagramView::exportAsSvg() {
    ERDiagramExporter::exportAsSvg(scene_, this);
}

void ERDiagramView::exportAsPdf() {
    ERDiagramExporter::exportAsPdf(scene_, this);
}

void ERDiagramView::exportAsDbml() {
    ERDiagramExporter::exportAsDbml(tables_, this);
}

void ERDiagramView::exportAsMermaid() {
    ERDiagramExporter::exportAsMermaid(tables_, this);
}

void ERDiagramView::loadSchema(IDatabaseAdapter* adapter,
                               const std::optional<std::string>& schema) {
    clear();
    if (!adapter) return;

    std::vector<TableDescription> tables;
    try {
        const auto tableInfos = adapter->listTables(schema);
        for (const auto& info : tableInfos) {
            if (info.type != TableKind::Table) continue;
            try {
                tables.push_back(adapter->describeTable(info.name, schema));
            } catch (...) {
                // Skip tables that fail to describe
            }
        }
    } catch (const GridexError&) {
        return;
    }

    buildScene(tables);
}

void ERDiagramView::buildScene(const std::vector<TableDescription>& tables) {
    if (tables.empty()) return;
    tables_ = tables;

    // Build layout nodes
    std::vector<TableNode> nodes;
    nodes.reserve(tables.size());
    for (const auto& td : tables) {
        TableNode n;
        n.name    = td.name;
        n.columns = td.columns;
        nodes.push_back(std::move(n));
    }
    layoutGrid(nodes);

    // Place table cards in scene and record card pointers.
    std::unordered_map<std::string, QGraphicsRectItem*> cardMap;
    for (std::size_t i = 0; i < tables.size(); ++i) {
        const auto& td   = tables[i];
        const auto& node = nodes[i];

        auto* card = buildTableCard(scene_, td);
        card->setPos(node.x, node.y);
        // ItemSendsGeometryChanges already set in buildTableCard.
        // Trigger scene update when card moves so edges repaint.
        card->setFlag(QGraphicsItem::ItemSendsScenePositionChanges, true);

        cardMap[td.name]            = card;
        nodePositions_[td.name]     = QPointF(node.x, node.y);
        nodeColumnCounts_[td.name]  = static_cast<int>(td.columns.size());
    }

    // Draw FK edges as dynamic FKEdgeItem (follows nodes on drag).
    for (const auto& td : tables) {
        auto srcCardIt = cardMap.find(td.name);
        if (srcCardIt == cardMap.end()) continue;

        for (const auto& fk : td.foreignKeys) {
            auto tgtCardIt = cardMap.find(fk.referencedTable);
            if (tgtCardIt == cardMap.end()) continue;

            // Find FK column row index in source table.
            int srcRowIdx = 0;
            for (std::size_t c = 0; c < td.columns.size(); ++c) {
                if (!fk.columns.empty() && td.columns[c].name == fk.columns[0]) {
                    srcRowIdx = static_cast<int>(c);
                    break;
                }
            }

            auto* edge = new FKEdgeItem(srcCardIt->second, srcRowIdx,
                                        tgtCardIt->second, /*tgtRowIdx*/ 0);
            scene_->addItem(edge);
        }
    }

    fitAll();
}

void ERDiagramView::layoutGrid(std::vector<TableNode>& nodes) {
    const int cols = static_cast<int>(kGridCols);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const int row = static_cast<int>(i) / cols;
        const int col = static_cast<int>(i) % cols;

        // Compute max height of this row to stack cleanly
        qreal rowH = 0;
        const int rowStart = row * cols;
        const int rowEnd   = std::min(rowStart + cols, static_cast<int>(nodes.size()));
        for (int j = rowStart; j < rowEnd; ++j) {
            rowH = std::max(rowH,
                cardHeight(static_cast<int>(nodes[static_cast<std::size_t>(j)].columns.size())));
        }

        // X: sum of all card widths + gaps up to this column
        nodes[i].x = kPadding + col * (kCardWidth + kHGap);

        // Y: sum of max-heights of previous rows + gaps
        qreal y = kPadding;
        for (int r = 0; r < row; ++r) {
            const int rs = r * cols;
            const int re = std::min(rs + cols, static_cast<int>(nodes.size()));
            qreal maxH = 0;
            for (int j = rs; j < re; ++j) {
                maxH = std::max(maxH,
                    cardHeight(static_cast<int>(nodes[static_cast<std::size_t>(j)].columns.size())));
            }
            y += maxH + kVGap;
        }
        nodes[i].y = y;
    }
}

}  // namespace gridex
