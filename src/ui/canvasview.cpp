#include "ui/canvasview.h"

#include <QGraphicsScene>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QVarLengthArray>
#include <QWheelEvent>
#include <QtMath>

#include "ui/caditem.h"
#include "ui/symbols.h"

namespace ui {
namespace {

// CADe SIMU draws a green snap dot every 3 logical units.
constexpr double kGridStepUnits = 3.0;

} // namespace

CanvasView::CanvasView(QWidget* parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);

    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                   | QPainter::SmoothPixmapTransform);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setCacheMode(QGraphicsView::CacheBackground);
    setDragMode(QGraphicsView::RubberBandDrag);
    setInteractive(true);
    setRubberBandSelectionMode(Qt::IntersectsItemShape);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setBackgroundBrush(QColor(251, 251, 251));
    setMouseTracking(true);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &CanvasView::emitSelection);

    m_scene->setSceneRect(0, 0, 400 * kUnit, 300 * kUnit);
}

void CanvasView::setDocument(const cad::Document& document)
{
    clearDocument();

    for (const cad::Object& object : document.objects) {
        if (object.type <= 0)
            continue;
        m_scene->addItem(new CadItem(object));
    }

    QRectF bounds = m_scene->itemsBoundingRect();
    if (bounds.isEmpty())
        bounds = QRectF(0, 0, 400 * kUnit, 300 * kUnit);
    m_scene->setSceneRect(bounds.adjusted(-60, -60, 60, 60));
    zoomAll();
}

void CanvasView::clearDocument()
{
    m_scene->clear();
    resetTransform();
}

void CanvasView::zoomIn()
{
    applyZoom(1.2);
}

void CanvasView::zoomOut()
{
    applyZoom(1.0 / 1.2);
}

void CanvasView::zoomAll()
{
    QRectF bounds = m_scene->itemsBoundingRect();
    if (bounds.isEmpty())
        bounds = m_scene->sceneRect();
    if (bounds.isEmpty())
        return;

    bounds = bounds.adjusted(-40, -40, 40, 40);
    fitInView(bounds, Qt::KeepAspectRatio);

    // Do not let a tiny drawing blow up to a silly zoom level.
    const double scale = transform().m11();
    if (scale > 3.0)
        resetTransform();
}

void CanvasView::setGridVisible(bool visible)
{
    if (m_grid == visible)
        return;
    m_grid = visible;
    resetCachedContent();
    viewport()->update();
}

void CanvasView::emitSelection()
{
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    for (QGraphicsItem* item : selected) {
        if (auto* cadItem = qgraphicsitem_cast<CadItem*>(item)) {
            emit selectionChanged(&cadItem->object());
            return;
        }
    }
    emit selectionChanged(nullptr);
}

void CanvasView::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, QColor(251, 251, 251));

    if (!m_grid)
        return;

    const double step = kGridStepUnits * kUnit;
    const double zoom = transform().m11();
    if (step * zoom < 4.0)
        return;

    const int firstX = static_cast<int>(qFloor(rect.left() / step));
    const int lastX = static_cast<int>(qCeil(rect.right() / step));
    const int firstY = static_cast<int>(qFloor(rect.top() / step));
    const int lastY = static_cast<int>(qCeil(rect.bottom() / step));

    // Guard against pathological zoom-out requests.
    const qint64 count = qint64(lastX - firstX + 1) * qint64(lastY - firstY + 1);
    if (count <= 0 || count > 40000)
        return;

    QVarLengthArray<QPointF, 4096> dots;
    dots.reserve(static_cast<int>(count));
    for (int x = firstX; x <= lastX; ++x) {
        for (int y = firstY; y <= lastY; ++y)
            dots.append(QPointF(x * step, y * step));
    }

    painter->setPen(QPen(QColor(0, 126, 0, 160), 1.0));
    painter->drawPoints(dots.constData(), static_cast<int>(dots.size()));
}

void CanvasView::wheelEvent(QWheelEvent* event)
{
    if (event->angleDelta().y() == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    applyZoom(event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15);
    event->accept();
}

void CanvasView::applyZoom(double factor)
{
    const double current = transform().m11();
    const double target = current * factor;

    if (target < 0.03 || target > 40.0)
        return;

    scale(factor, factor);
    resetCachedContent();
}

void CanvasView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF scenePosition = mapToScene(event->pos());
    emit cursorMoved(scenePosition / kUnit);

    if (m_panning) {
        const QPoint delta = event->pos() - m_panStart;
        m_panStart = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

bool renderToPng(const cad::Document& document,
                 const QString& outputPath,
                 QSize maximumSize,
                 QString* error)
{
    QGraphicsScene scene;
    scene.setBackgroundBrush(QColor(251, 251, 251));

    for (const cad::Object& object : document.objects) {
        if (object.type <= 0)
            continue;
        scene.addItem(new CadItem(object));
    }

    QRectF source = scene.itemsBoundingRect();
    if (source.isEmpty())
        source = QRectF(0, 0, 400 * kUnit, 300 * kUnit);
    source = source.adjusted(-30, -30, 30, 30);

    QSize imageSize = source.size().toSize();
    if (imageSize.isEmpty())
        imageSize = QSize(1200, 900);

    if (maximumSize.isValid()) {
        imageSize.scale(maximumSize, Qt::KeepAspectRatio);
    }

    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("Could not allocate a %1x%2 image")
                         .arg(imageSize.width())
                         .arg(imageSize.height());
        return false;
    }
    image.fill(QColor(251, 251, 251));

    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                           | QPainter::SmoothPixmapTransform);

    // Light page border so the export has a defined edge.
    painter.setPen(QPen(QColor(210, 210, 210), 1));
    painter.drawRect(QRectF(0.5, 0.5, image.width() - 1.0, image.height() - 1.0));

    scene.render(&painter, QRectF(image.rect()), source, Qt::KeepAspectRatio);
    painter.end();

    if (!image.save(outputPath, "PNG")) {
        if (error)
            *error = QStringLiteral("Could not write %1").arg(outputPath);
        return false;
    }

    return true;
}

} // namespace ui
