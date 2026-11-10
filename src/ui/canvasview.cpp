#include "ui/canvasview.h"

#include <QGraphicsScene>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QVarLengthArray>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>

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
    setFocusPolicy(Qt::StrongFocus);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &CanvasView::emitSelection);

    m_scene->setSceneRect(0, 0, 400 * kUnit, 300 * kUnit);
}

void CanvasView::setDocument(const cad::Document& document)
{
    setObjects(document.objects);
}

void CanvasView::setObjects(const QList<cad::Object>& objects)
{
    clearDocument();

    for (const cad::Object& object : objects) {
        if (object.type <= 0)
            continue;
        addCadItem(object);
    }

    QRectF bounds = m_scene->itemsBoundingRect();
    if (bounds.isEmpty())
        bounds = QRectF(0, 0, 400 * kUnit, 300 * kUnit);
    m_scene->setSceneRect(bounds.adjusted(-60, -60, 60, 60));
    zoomAll();
}

void CanvasView::addCadItem(const cad::Object& object)
{
    auto* item = new CadItem(object);
    item->setEditable(true);
    m_scene->addItem(item);
    m_nextId = qMax(m_nextId, object.id + 1);
}

QList<cad::Object> CanvasView::objects() const
{
    QList<cad::Object> result;
    for (QGraphicsItem* item : m_scene->items()) {
        if (auto* cadItem = qgraphicsitem_cast<CadItem*>(item))
            result.append(cadItem->object());
    }

    std::sort(result.begin(), result.end(), [](const cad::Object& a, const cad::Object& b) {
        return a.id < b.id;
    });
    return result;
}

void CanvasView::clearDocument()
{
    m_scene->clear();
    resetTransform();
    cancelInsert();
    m_nextId = 1;
}

void CanvasView::insertObject(cad::Object object)
{
    if (object.id < 0)
        object.id = m_nextId++;
    else
        m_nextId = qMax(m_nextId, object.id + 1);

    auto* item = new CadItem(object);
    item->setEditable(true);
    m_scene->addItem(item);

    m_scene->clearSelection();
    item->setSelected(true);
    emit documentEdited();

    const QRectF bounds = m_scene->itemsBoundingRect().adjusted(-60, -60, 60, 60);
    if (bounds != m_scene->sceneRect())
        m_scene->setSceneRect(bounds);
}

void CanvasView::beginInsert(const cad::Object& prototype)
{
    m_pending = prototype;
    m_hasPending = true;
    viewport()->setCursor(Qt::CrossCursor);
}

void CanvasView::cancelInsert()
{
    m_hasPending = false;
    viewport()->unsetCursor();
}

void CanvasView::deleteSelection()
{
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty())
        return;

    for (QGraphicsItem* item : selected) {
        m_scene->removeItem(item);
        delete item;
    }

    emit selectionChanged(nullptr);
    emit documentEdited();
}

void CanvasView::selectAll()
{
    const QList<QGraphicsItem*> items = m_scene->items();
    for (QGraphicsItem* item : items)
        item->setSelected(true);
}

QPointF CanvasView::viewCenterInUnits() const
{
    const QPointF scenePosition = mapToScene(viewport()->rect().center());
    return scenePosition / kUnit;
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
    if (m_hasPending && event->button() == Qt::LeftButton) {
        const QPointF scenePosition = mapToScene(event->pos());
        const double step = kGridStepUnits;
        const int x = qRound(scenePosition.x() / kUnit / step) * int(step);
        const int y = qRound(scenePosition.y() / kUnit / step) * int(step);

        cad::Object object = m_pending;
        object.fields[object.pinFlagCount + 0].value = x;
        object.fields[object.pinFlagCount + 1].value = y;
        object.id = -1;

        cancelInsert();
        insertObject(object);
        event->accept();
        return;
    }

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

void CanvasView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && m_hasPending) {
        cancelInsert();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Delete && !m_scene->selectedItems().isEmpty()) {
        deleteSelection();
        event->accept();
        return;
    }

    QGraphicsView::keyPressEvent(event);
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
