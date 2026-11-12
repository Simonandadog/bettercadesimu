#include "ui/canvasview.h"

#include <QGraphicsScene>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVarLengthArray>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <utility>

#include "ui/caditem.h"
#include "ui/symbols.h"

namespace ui {
namespace {

// CADe SIMU draws a green snap dot every 3 logical units.
constexpr double kGridStepUnits = 3.0;

cad::Object makeWireObject(const QPoint& start, const QPoint& end)
{
    cad::Object object;
    object.type = 4000;
    object.pinFlagCount = cad::kMinPinFlags;
    object.typeParts << QString::number(object.type) << QString() << QString();
    for (int i = 0; i < cad::kMinPinFlags; ++i)
        object.typeParts << QString();

    object.fields.resize(object.pinFlagCount + cad::kTailFields);
    const int tail = object.pinFlagCount;
    object.fields[tail + 0].value = start.x();
    object.fields[tail + 1].value = start.y();
    object.fields[tail + 2].value = end.x();
    object.fields[tail + 3].value = end.y();
    return object;
}

cad::Object makeJunctionObject(const QPoint& position)
{
    cad::Object object = makeWireObject(position, position);
    object.type = 4001;
    object.typeParts[0] = QString::number(object.type);

    const int tail = object.pinFlagCount;
    object.fields[tail + 4].value = -1;
    object.fields[tail + 5].value = -1;
    object.fields[tail + 6].value = 3;
    object.fields[tail + 7].value = 3;
    return object;
}

cad::Object makeTextObject(const QPoint& position, const QString& text)
{
    cad::Object object;
    object.type = 8;
    object.pinFlagCount = cad::kMinPinFlags;
    object.typeParts << QString::number(object.type) << QString() << QString();
    for (int i = 0; i < cad::kMinPinFlags; ++i)
        object.typeParts << QString();

    object.fields.resize(object.pinFlagCount + cad::kTailFields);
    const int tail = object.pinFlagCount;
    object.fields[tail + 0].value = position.x();
    object.fields[tail + 1].value = position.y();
    object.fields[tail + 2].value = position.x() + 16;
    object.fields[tail + 3].value = position.y() + 4;
    object.fields[tail + 14].text = text;
    return object;
}

class AddObjectCommand : public QUndoCommand
{
public:
    AddObjectCommand(CanvasView* view, const cad::Object& object)
        : QUndoCommand(QObject::tr("Add object"))
        , m_view(view)
        , m_object(object)
    {
    }

    void redo() override
    {
        m_id = m_view->appendObject(m_object, m_id);

        if (CadItem* item = m_view->itemById(m_id)) {
            m_view->scene()->clearSelection();
            item->setSelected(true);
        }
    }

    void undo() override { m_view->removeObjectById(m_id); }

private:
    CanvasView* m_view;
    cad::Object m_object;
    int m_id = -1;
};

class AddObjectsCommand : public QUndoCommand
{
public:
    AddObjectsCommand(CanvasView* view, const QList<cad::Object>& objects)
        : QUndoCommand(objects.size() == 1 ? QObject::tr("Add object")
                                           : QObject::tr("Paste objects"))
        , m_view(view)
        , m_objects(objects)
    {
    }

    void redo() override
    {
        if (m_ids.isEmpty()) {
            for (const cad::Object& object : std::as_const(m_objects))
                m_ids.append(m_view->appendObject(object));
        } else {
            for (int i = 0; i < m_objects.size() && i < m_ids.size(); ++i)
                m_view->appendObject(m_objects.at(i), m_ids.at(i));
        }

        m_view->scene()->clearSelection();
        for (int id : std::as_const(m_ids)) {
            if (CadItem* item = m_view->itemById(id))
                item->setSelected(true);
        }
    }

    void undo() override
    {
        for (int id : std::as_const(m_ids))
            m_view->removeObjectById(id);
    }

private:
    CanvasView* m_view;
    QList<cad::Object> m_objects;
    QList<int> m_ids;
};

class RemoveObjectsCommand : public QUndoCommand
{
public:
    RemoveObjectsCommand(CanvasView* view, const QList<cad::Object>& objects)
        : QUndoCommand(objects.size() == 1 ? QObject::tr("Delete object")
                                           : QObject::tr("Delete objects"))
        , m_view(view)
        , m_objects(objects)
    {
    }

    void redo() override
    {
        for (const cad::Object& object : m_objects)
            m_view->removeObjectById(object.id);
    }

    void undo() override
    {
        for (const cad::Object& object : m_objects)
            m_view->appendObject(object, object.id);
    }

private:
    CanvasView* m_view;
    QList<cad::Object> m_objects;
};

class MoveObjectsCommand : public QUndoCommand
{
public:
    MoveObjectsCommand(CanvasView* view,
                       const QList<int>& ids,
                       const QList<QPointF>& from,
                       const QList<QPointF>& to)
        : QUndoCommand(ids.size() == 1 ? QObject::tr("Move object")
                                       : QObject::tr("Move objects"))
        , m_view(view)
        , m_ids(ids)
        , m_from(from)
        , m_to(to)
    {
    }

    void redo() override
    {
        // The drag already put the items where they belong.
        if (m_skipFirstRedo) {
            m_skipFirstRedo = false;
            return;
        }
        apply(m_to);
    }

    void undo() override { apply(m_from); }

private:
    void apply(const QList<QPointF>& positions)
    {
        for (int i = 0; i < m_ids.size() && i < positions.size(); ++i) {
            if (CadItem* item = m_view->itemById(m_ids.at(i)))
                item->setPos(positions.at(i));
        }
    }

    CanvasView* m_view;
    QList<int> m_ids;
    QList<QPointF> m_from;
    QList<QPointF> m_to;
    bool m_skipFirstRedo = true;
};

class EditObjectCommand : public QUndoCommand
{
public:
    EditObjectCommand(CanvasView* view, const cad::Object& before, const cad::Object& after)
        : QUndoCommand(QObject::tr("Edit object"))
        , m_view(view)
        , m_before(before)
        , m_after(after)
    {
    }

    void redo() override { m_view->replaceObject(m_after); }
    void undo() override { m_view->replaceObject(m_before); }

private:
    CanvasView* m_view;
    cad::Object m_before;
    cad::Object m_after;
};

class RotateObjectsCommand : public QUndoCommand
{
public:
    RotateObjectsCommand(CanvasView* view, const QList<cad::Object>& before,
                         const QList<cad::Object>& after)
        : QUndoCommand(before.size() == 1 ? QObject::tr("Rotate object")
                                          : QObject::tr("Rotate objects"))
        , m_view(view)
        , m_before(before)
        , m_after(after)
    {
    }

    void redo() override
    {
        for (const cad::Object& object : std::as_const(m_after))
            m_view->replaceObject(object);
    }

    void undo() override
    {
        for (const cad::Object& object : std::as_const(m_before))
            m_view->replaceObject(object);
    }

private:
    CanvasView* m_view;
    QList<cad::Object> m_before;
    QList<cad::Object> m_after;
};

} // namespace

CanvasView::CanvasView(QWidget* parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
    , m_undoStack(new QUndoStack(this))
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
    connect(m_undoStack, &QUndoStack::indexChanged, this, [this] {
        updateSceneRect();
        emit documentEdited();
    });

    m_scene->setSceneRect(0, 0, 400 * kUnit, 300 * kUnit);
}

CanvasView::~CanvasView()
{
    // The scene is a child of this widget and is destroyed before the undo
    // stack. Tear the stack down first, without emitting, so clearing it
    // cannot touch a half-destroyed scene or window.
    m_undoStack->blockSignals(true);
    delete m_undoStack;
    m_undoStack = nullptr;
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

int CanvasView::appendObject(const cad::Object& object, int id)
{
    cad::Object copy = object;
    if (id >= 0)
        copy.id = id;
    if (copy.id < 0)
        copy.id = m_nextId;
    m_nextId = qMax(m_nextId, copy.id + 1);

    addCadItem(copy);
    return copy.id;
}

void CanvasView::removeObjectById(int id)
{
    if (CadItem* item = itemById(id)) {
        m_scene->removeItem(item);
        delete item;
    }
}

CadItem* CanvasView::itemById(int id) const
{
    for (QGraphicsItem* item : m_scene->items()) {
        if (auto* cadItem = qgraphicsitem_cast<CadItem*>(item)) {
            if (cadItem->object().id == id)
                return cadItem;
        }
    }
    return nullptr;
}

void CanvasView::updateSceneRect()
{
    QRectF bounds = m_scene->itemsBoundingRect();
    if (bounds.isEmpty())
        bounds = QRectF(0, 0, 400 * kUnit, 300 * kUnit);
    else
        bounds = bounds.adjusted(-60, -60, 60, 60);

    // Never shrink below what is already on screen: panning far away and then
    // editing an object should not snap the view back.
    m_scene->setSceneRect(m_scene->sceneRect().united(bounds));
}

// QGraphicsView can only scroll inside its scene rect. Grow that rect when a
// middle-drag approaches the edge so the canvas can always be moved freely.
void CanvasView::ensurePanRoom()
{
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    if (visible.isEmpty())
        return;

    const QRectF wanted = visible.adjusted(-visible.width(), -visible.height(),
                                           visible.width(), visible.height());
    const QRectF current = m_scene->sceneRect();
    if (current.contains(wanted))
        return;

    const QPointF centre = visible.center();
    m_scene->setSceneRect(current.united(wanted));
    centerOn(centre);
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
    m_undoStack->clear();
    m_dragStart.clear();
    m_wireActive = false;
    resetTransform();
    cancelInsert();
    m_nextId = 1;
}

void CanvasView::insertObject(cad::Object object)
{
    object.id = -1;
    m_undoStack->push(new AddObjectCommand(this, object));
}

void CanvasView::insertText(const QString& text, const QPoint& position)
{
    if (text.trimmed().isEmpty())
        return;
    insertObject(makeTextObject(position, text));
}

void CanvasView::beginInsert(const cad::Object& prototype)
{
    m_pending = prototype;
    m_hasPending = true;
    updateCursor();
}

void CanvasView::cancelInsert()
{
    m_hasPending = false;
    updateCursor();
}

void CanvasView::deleteSelection()
{
    QList<cad::Object> objects;
    for (QGraphicsItem* item : m_scene->selectedItems()) {
        if (auto* cadItem = qgraphicsitem_cast<CadItem*>(item))
            objects.append(cadItem->object());
    }

    if (objects.isEmpty())
        return;

    std::sort(objects.begin(), objects.end(),
              [](const cad::Object& a, const cad::Object& b) { return a.id < b.id; });

    m_undoStack->push(new RemoveObjectsCommand(this, objects));
    emit selectionChanged(nullptr);
}

QList<cad::Object> CanvasView::selectedObjects() const
{
    QList<cad::Object> objects;
    for (QGraphicsItem* item : m_scene->selectedItems()) {
        if (auto* cadItem = qgraphicsitem_cast<CadItem*>(item))
            objects.append(cadItem->object());
    }

    std::sort(objects.begin(), objects.end(),
              [](const cad::Object& a, const cad::Object& b) { return a.id < b.id; });
    return objects;
}

void CanvasView::editObject(const cad::Object& object)
{
    CadItem* item = itemById(object.id);
    if (!item)
        return;

    const cad::Object before = item->object();
    if (before.label == object.label && before.sublabel == object.sublabel
        && before.text() == object.text() && before.typeParts == object.typeParts) {
        return;
    }

    m_undoStack->push(new EditObjectCommand(this, before, object));
}

void CanvasView::rotateSelection(int degrees)
{
    QList<cad::Object> before;
    QList<cad::Object> after;
    for (const cad::Object& object : selectedObjects()) {
        if (!object.isComponent())
            continue;

        cad::Object rotated = object;
        rotated.rotation = ((rotated.rotation + degrees) % 360 + 360) % 360;
        before.append(object);
        after.append(rotated);
    }

    if (before.isEmpty())
        return;

    m_undoStack->push(new RotateObjectsCommand(this, before, after));
}

void CanvasView::replaceObject(const cad::Object& object)
{
    CadItem* item = itemById(object.id);
    if (!item)
        return;

    item->setObject(object);
    updateSceneRect();
    if (item->isSelected())
        emit selectionChanged(&item->object());
    else
        emitSelection();
}

void CanvasView::copySelection()
{
    m_clipboard.clear();
    for (QGraphicsItem* item : m_scene->selectedItems()) {
        if (auto* cadItem = qgraphicsitem_cast<CadItem*>(item))
            m_clipboard.append(cadItem->object());
    }

    std::sort(m_clipboard.begin(), m_clipboard.end(),
              [](const cad::Object& a, const cad::Object& b) { return a.id < b.id; });
}

void CanvasView::paste()
{
    if (m_clipboard.isEmpty())
        return;

    QList<cad::Object> copies = m_clipboard;
    constexpr int offset = 3;
    for (cad::Object& object : copies) {
        object.id = -1;
        const int base = object.pinFlagCount;
        object.fields[base + 0].value += offset;
        object.fields[base + 1].value += offset;
        if (object.isLine() || object.isText()) {
            object.fields[base + 2].value += offset;
            object.fields[base + 3].value += offset;
        }
    }

    m_undoStack->push(new AddObjectsCommand(this, copies));
}

void CanvasView::duplicateSelection()
{
    QList<cad::Object> copies;
    for (QGraphicsItem* item : m_scene->selectedItems()) {
        if (auto* cadItem = qgraphicsitem_cast<CadItem*>(item))
            copies.append(cadItem->object());
    }
    if (copies.isEmpty())
        return;

    std::sort(copies.begin(), copies.end(),
              [](const cad::Object& a, const cad::Object& b) { return a.id < b.id; });

    constexpr int offset = 3;
    for (cad::Object& object : copies) {
        object.id = -1;
        const int base = object.pinFlagCount;
        object.fields[base + 0].value += offset;
        object.fields[base + 1].value += offset;
        if (object.isLine() || object.isText()) {
            object.fields[base + 2].value += offset;
            object.fields[base + 3].value += offset;
        }
    }

    m_undoStack->push(new AddObjectsCommand(this, copies));
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

void CanvasView::setTool(Tool tool)
{
    if (m_tool == tool)
        return;

    m_tool = tool;
    m_wireActive = false;
    cancelInsert();
    viewport()->update();
    emit toolChanged(m_tool);
}

void CanvasView::updateCursor()
{
    if (m_hasPending || m_tool != Tool::Select)
        viewport()->setCursor(Qt::CrossCursor);
    else
        viewport()->unsetCursor();
}

QPoint CanvasView::snapUnits(const QPointF& scenePosition) const
{
    const int step = int(kGridStepUnits);
    return QPoint(qRound(scenePosition.x() / kUnit / step) * step,
                  qRound(scenePosition.y() / kUnit / step) * step);
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

void CanvasView::drawForeground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawForeground(painter, rect);

    if (!m_wireActive)
        return;

    const QPointF start(m_wireStart.x() * kUnit, m_wireStart.y() * kUnit);
    const QPointF end(m_wireEnd.x() * kUnit, m_wireEnd.y() * kUnit);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(QColor(0, 120, 215), 1.5, Qt::DashLine));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(start, end);
    painter->drawRect(QRectF(start.x() - 4.0, start.y() - 4.0, 8.0, 8.0));
    painter->restore();
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
        const QPoint point = snapUnits(mapToScene(event->pos()));

        cad::Object object = m_pending;
        object.fields[object.pinFlagCount + 0].value = point.x();
        object.fields[object.pinFlagCount + 1].value = point.y();
        object.id = -1;

        cancelInsert();
        insertObject(object);
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = event->pos();
        ensurePanRoom();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_tool != Tool::Select) {
        const QPoint point = snapUnits(mapToScene(event->pos()));

        if (m_tool == Tool::Wire) {
            if (!m_wireActive) {
                m_wireStart = point;
                m_wireEnd = point;
                m_wireActive = true;
            } else {
                m_wireActive = false;
                if (point != m_wireStart)
                    insertObject(makeWireObject(m_wireStart, point));
            }
            viewport()->update();
        } else if (m_tool == Tool::Junction) {
            insertObject(makeJunctionObject(point));
        } else if (m_tool == Tool::Text) {
            emit textInsertionRequested(point);
        }

        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);

    if (event->button() == Qt::LeftButton) {
        m_dragStart.clear();
        for (QGraphicsItem* item : m_scene->selectedItems()) {
            if (auto* cadItem = qgraphicsitem_cast<CadItem*>(item)) {
                if (cadItem->isEditable())
                    m_dragStart.append({cadItem->object().id, cadItem->pos()});
            }
        }
    }
}

void CanvasView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF scenePosition = mapToScene(event->pos());
    emit cursorMoved(scenePosition / kUnit);

    if (m_panning) {
        const QPoint delta = event->pos() - m_panStart;
        m_panStart = event->pos();
        ensurePanRoom();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    if (m_wireActive) {
        const QPoint point = snapUnits(scenePosition);
        if (point != m_wireEnd) {
            m_wireEnd = point;
            viewport()->update();
        }
    }

    QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        updateCursor();
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton || m_dragStart.isEmpty())
        return;

    QList<int> ids;
    QList<QPointF> from;
    QList<QPointF> to;
    for (const auto& start : std::as_const(m_dragStart)) {
        CadItem* item = itemById(start.first);
        if (!item || item->pos() == start.second)
            continue;
        ids.append(start.first);
        from.append(start.second);
        to.append(item->pos());
    }
    m_dragStart.clear();

    if (!ids.isEmpty())
        m_undoStack->push(new MoveObjectsCommand(this, ids, from, to));
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_tool == Tool::Select) {
        if (auto* item = qgraphicsitem_cast<CadItem*>(itemAt(event->pos()))) {
            m_scene->clearSelection();
            item->setSelected(true);
            emit objectActivated();
            event->accept();
            return;
        }
    }

    QGraphicsView::mouseDoubleClickEvent(event);
}

void CanvasView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && (m_hasPending || m_wireActive)) {
        cancelInsert();
        if (m_wireActive) {
            m_wireActive = false;
            viewport()->update();
        }
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
