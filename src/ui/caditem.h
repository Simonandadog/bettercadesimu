#pragma once

#include <QGraphicsItem>

#include "cad/cadfile.h"

namespace ui {

class CadItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 1 };

    explicit CadItem(const cad::Object& object, QGraphicsItem* parent = nullptr);

    const cad::Object& object() const { return m_object; }

    void setEditable(bool editable);
    bool isEditable() const { return m_editable; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    int type() const override { return Type; }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    cad::Object m_object;
    QRectF m_bounds;
    bool m_hovered = false;
    bool m_editable = false;
};

} // namespace ui
