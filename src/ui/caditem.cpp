#include "ui/caditem.h"

#include <QGraphicsSceneHoverEvent>
#include <QPainterPathStroker>

#include "ui/symbols.h"

namespace ui {
namespace {

double zFor(const cad::Object& object)
{
    if (object.isLine())
        return -10.0;
    if (object.isJunction())
        return -5.0;
    if (object.isText())
        return 10.0;
    return 0.0;
}

} // namespace

CadItem::CadItem(const cad::Object& object, QGraphicsItem* parent)
    : QGraphicsItem(parent)
    , m_object(object)
    , m_bounds(ui::localBounds(object))
{
    setFlag(ItemIsSelectable, true);
    setAcceptHoverEvents(true);
    setZValue(zFor(object));

    setPos(object.x() * ui::kUnit, object.y() * ui::kUnit);
}

QRectF CadItem::boundingRect() const
{
    return m_bounds;
}

QPainterPath CadItem::shape() const
{
    if (m_object.isLine()) {
        QPainterPath path;
        path.moveTo(0, 0);
        path.lineTo((m_object.x2() - m_object.x()) * ui::kUnit,
                    (m_object.y2() - m_object.y()) * ui::kUnit);
        QPainterPathStroker stroker;
        stroker.setWidth(9.0);
        return stroker.createStroke(path);
    }

    QPainterPath path;
    path.addRect(m_bounds);
    return path;
}

void CadItem::paint(QPainter* painter,
                    const QStyleOptionGraphicsItem* option,
                    QWidget* widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)
    ui::paintObject(painter, m_object, isSelected() || m_hovered);
}

void CadItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    m_hovered = true;
    update();

    QString tooltip = QStringLiteral("#%1 · %2").arg(m_object.id).arg(cad::typeName(m_object.type));
    if (!m_object.label.isEmpty())
        tooltip += QStringLiteral(" · %1").arg(m_object.label);
    if (!m_object.text().isEmpty())
        tooltip += QStringLiteral(" · %1").arg(m_object.text());
    setToolTip(tooltip);

    QGraphicsItem::hoverEnterEvent(event);
}

void CadItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    m_hovered = false;
    update();
    QGraphicsItem::hoverLeaveEvent(event);
}

} // namespace ui
