#include "ui/symbols.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QtMath>

namespace ui {
namespace {

QFont labelFont()
{
    QFont font;
    font.setPixelSize(16);
    return font;
}

QFont textFont()
{
    QFont font;
    font.setPixelSize(12);
    return font;
}

// The file's insertion point is the first pin of the symbol. For multi-pole
// parts, the remaining poles sit kPitch units to the right.
int poleCount(const cad::Object& object)
{
    switch (object.type) {
    case 1000: // motor: U1, V1, W1, PE
        return 4;
    case 2004:
    case 2008:
    case 5003:
        return 3;
    default:
        return 1;
    }
}

double pitchPx()
{
    return kPitch * kUnit;
}

double spanPx()
{
    return 12.0 * kUnit;
}

double poleX(int index)
{
    return index * pitchPx();
}

// Logical extent of the drawn symbol in local pixels, before the label.
QRectF symbolBox(const cad::Object& object)
{
    if (object.type == 1000) {
        // Circle centred under the three phase pins, plus the PE pin.
        return QRectF(-3.0 * kUnit, 0, 21.0 * kUnit, 20.0 * kUnit);
    }

    if (poleCount(object) > 1)
        return QRectF(-3.0 * kUnit, 0, (poleCount(object) - 1) * pitchPx() + 6.0 * kUnit, spanPx());

    return QRectF(-3.0 * kUnit, 0, 6.0 * kUnit, spanPx());
}

QSizeF labelSize(const cad::Object& object)
{
    if (object.label.isEmpty())
        return {};
    const QFontMetricsF metrics(labelFont());
    return QSizeF(metrics.horizontalAdvance(object.label) + 4.0, metrics.height());
}

// Labels sit to the left of the symbol, vertically centred, like the original.
QRectF labelRect(const cad::Object& object, const QRectF& symbol)
{
    const QSizeF size = labelSize(object);
    if (size.isEmpty())
        return {};
    return QRectF(-3.0 - size.width(), symbol.center().y() - size.height() / 2.0,
                  size.width(), size.height());
}

void drawPinStub(QPainter* painter, double x, double fromY, double toY)
{
    painter->drawLine(QPointF(x, fromY), QPointF(x, toY));
}

void drawContact(QPainter* painter, double x, bool normallyClosed, bool button)
{
    const double top = 0.0;
    const double bottom = spanPx();
    const double gapTop = 3.0 * kUnit;
    const double gapBottom = 9.0 * kUnit;

    drawPinStub(painter, x, top, gapTop);
    drawPinStub(painter, x, gapBottom, bottom);

    if (normallyClosed) {
        painter->drawLine(QPointF(x, gapTop), QPointF(x, gapBottom));
    } else {
        painter->drawLine(QPointF(x, gapBottom), QPointF(x - 3.0 * kUnit, gapTop));
    }

    if (button) {
        const double y = 6.0 * kUnit;
        painter->drawLine(QPointF(x, y), QPointF(x - 5.0 * kUnit, y));
        painter->drawLine(QPointF(x - 5.0 * kUnit, y - 2.0 * kUnit),
                          QPointF(x - 5.0 * kUnit, y + 2.0 * kUnit));
    }
}

void drawContactor(QPainter* painter, const cad::Object& object)
{
    const int poles = poleCount(object);
    for (int pole = 0; pole < poles; ++pole) {
        const double x = poleX(pole);
        drawPinStub(painter, x, 0.0, 3.0 * kUnit);
        drawPinStub(painter, x, 9.0 * kUnit, spanPx());
        painter->drawLine(QPointF(x, 9.0 * kUnit),
                          QPointF(x + 3.0 * kUnit, 3.0 * kUnit));
    }

    // Mechanical link between the poles.
    painter->drawLine(QPointF(0, 6.0 * kUnit),
                      QPointF(poleX(poles - 1), 6.0 * kUnit));
}

void drawFuse(QPainter* painter, const cad::Object& object)
{
    const int poles = poleCount(object);
    for (int pole = 0; pole < poles; ++pole) {
        const double x = poleX(pole);
        drawPinStub(painter, x, 0.0, spanPx());
        const QRectF body(x - 2.0 * kUnit, 3.0 * kUnit,
                          4.0 * kUnit, 6.0 * kUnit);
        painter->drawRect(body);
    }
}

void drawCoil(QPainter* painter)
{
    drawPinStub(painter, 0, 0.0, 4.0 * kUnit);
    drawPinStub(painter, 0, 8.0 * kUnit, spanPx());
    painter->drawRect(QRectF(-4.5 * kUnit, 4.0 * kUnit, 9.0 * kUnit, 4.0 * kUnit));
}

void drawLamp(QPainter* painter)
{
    drawPinStub(painter, 0, 0.0, 2.0 * kUnit);
    drawPinStub(painter, 0, 10.0 * kUnit, spanPx());

    const QPointF center(0, 6.0 * kUnit);
    const double radius = 4.0 * kUnit;
    painter->drawEllipse(center, radius, radius);

    const double d = radius * 0.7;
    painter->drawLine(center + QPointF(-d, -d), center + QPointF(d, d));
    painter->drawLine(center + QPointF(-d, d), center + QPointF(d, -d));
}

void drawMotor(QPainter* painter)
{
    const QPointF center(6.0 * kUnit, 11.0 * kUnit);
    const double radius = 9.0 * kUnit;

    for (int pin = 0; pin < 3; ++pin)
        drawPinStub(painter, poleX(pin), 0.0, 2.0 * kUnit);

    // The PE terminal drops down the right-hand side.
    drawPinStub(painter, poleX(3), 0.0, 11.0 * kUnit);
    painter->drawLine(QPointF(poleX(3), 11.0 * kUnit),
                      QPointF(center.x() + radius, 11.0 * kUnit));

    painter->drawEllipse(center, radius, radius);

    QFont font = labelFont();
    font.setBold(true);
    painter->setFont(font);
    painter->drawText(QRectF(center.x() - radius, center.y() - radius,
                             radius * 2.0, radius * 2.0),
                      Qt::AlignCenter | Qt::AlignHCenter, QStringLiteral("M"));
}

void drawUnknown(QPainter* painter, const cad::Object& object, const QRectF& box)
{
    painter->setPen(QPen(QColor(150, 150, 150), 1.2, Qt::DashLine));
    painter->setBrush(QColor(250, 250, 250));
    painter->drawRoundedRect(box, 3, 3);

    QFont font = labelFont();
    font.setPixelSize(10);
    painter->setFont(font);
    painter->setPen(QColor(110, 110, 110));
    painter->drawText(box.adjusted(3, 2, -3, -2),
                      Qt::AlignCenter,
                      QString::number(object.type));
}

void drawPinNames(QPainter* painter, const cad::Object& object)
{
    QStringList names;
    for (const QString& pin : object.pins) {
        if (!pin.trimmed().isEmpty())
            names.append(pin.trimmed());
    }
    if (names.isEmpty())
        return;

    QFont font = labelFont();
    font.setPixelSize(9);
    painter->setFont(font);
    painter->setPen(QColor(70, 70, 70));

    const bool topRow = object.type == 1000;
    const bool horizontal = topRow || poleCount(object) > 1;
    const double above = -12.0;
    const double below = spanPx() + 1.0;

    if (horizontal && !topRow) {
        const int poles = poleCount(object);
        for (int i = 0; i < poles && i < names.size(); ++i) {
            painter->drawText(QRectF(poleX(i) - 14.0, above, 28.0, 12.0),
                              Qt::AlignCenter, names.at(i));
        }
        for (int i = 0; i < poles && poles + i < names.size(); ++i) {
            painter->drawText(QRectF(poleX(i) - 14.0, below, 28.0, 12.0),
                              Qt::AlignCenter, names.at(poles + i));
        }
        return;
    }

    if (topRow) {
        for (int i = 0; i < names.size(); ++i) {
            painter->drawText(QRectF(poleX(i) - 14.0, above, 28.0, 12.0),
                              Qt::AlignCenter, names.at(i));
        }
        return;
    }

    painter->drawText(QRectF(3.0 * kUnit, -2.0, 26.0, 12.0),
                      Qt::AlignLeft | Qt::AlignVCenter, names.value(0));
    if (names.size() > 1) {
        painter->drawText(QRectF(3.0 * kUnit, spanPx() - 10.0, 26.0, 12.0),
                          Qt::AlignLeft | Qt::AlignVCenter, names.at(1));
    }
}

} // namespace

QRectF localBounds(const cad::Object& object)
{
    if (object.isLine()) {
        const QPointF start(0, 0);
        const QPointF end((object.x2() - object.x()) * kUnit,
                          (object.y2() - object.y()) * kUnit);
        return QRectF(start, end).normalized().adjusted(-6, -6, 6, 6);
    }

    if (object.isText()) {
        const QFontMetricsF metrics(textFont());
        const QString text = object.text();
        const double boxWidth = qMax(1, object.x2() - object.x()) * kUnit;
        const double width = qMax(boxWidth, metrics.horizontalAdvance(text) + 2.0);
        const double height = qMax(metrics.height(), qMax(1, object.y2() - object.y()) * kUnit);
        return QRectF(0, 0, width, height).adjusted(-2, -2, 2, 2);
    }

    if (object.isJunction())
        return QRectF(-6, -6, 12, 12);

    const QRectF box = symbolBox(object);
    const QRectF label = labelRect(object, box);
    const QRectF united = label.isEmpty() ? box : box.united(label);
    return united.adjusted(-3, -3, 3, 3);
}

QColor lineColor(const cad::Object& object)
{
    switch (object.type) {
    case 4000: // power / phase conductor
    case 4001:
        return QColor(126, 0, 0);
    case 4018: // control conductor
        return QColor(251, 0, 0);
    case 4019: // neutral / protective conductor
        return QColor(0, 0, 0);
    case 1:
        return QColor(0, 0, 0);
    default:
        return QColor(0, 0, 0);
    }
}

QColor symbolColor(const cad::Object& object)
{
    Q_UNUSED(object)
    return QColor(0, 0, 0);
}

void paintObject(QPainter* painter, const cad::Object& object, bool selected)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (selected) {
        painter->setPen(QPen(QColor(0, 120, 215), 1.2, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(localBounds(object).adjusted(-2, -2, 2, 2));
    }

    if (object.isLine()) {
        QPen pen(lineColor(object), object.type == 1 ? 1.5 : 2.0);
        pen.setCapStyle(Qt::RoundCap);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(QPointF(0, 0),
                          QPointF((object.x2() - object.x()) * kUnit,
                                  (object.y2() - object.y()) * kUnit));
        painter->restore();
        return;
    }

    if (object.isText()) {
        painter->setPen(QColor(0, 0, 0));
        painter->setFont(textFont());
        const QRectF bounds = localBounds(object);
        painter->drawText(bounds.adjusted(2, 2, -2, -2),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          object.text());
        painter->restore();
        return;
    }

    if (object.isJunction()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0));
        painter->drawEllipse(QPointF(0, 0), 1.8, 1.8);
        painter->restore();
        return;
    }

    painter->setPen(QPen(symbolColor(object), 2.0));
    painter->setBrush(Qt::NoBrush);

    switch (object.type) {
    case 1000:
        drawMotor(painter);
        break;
    case 2004:
    case 2008:
        drawContactor(painter, object);
        break;
    case 5003:
        drawFuse(painter, object);
        break;
    case 7000:
        drawContact(painter, 0, false, false);
        break;
    case 7001:
        drawContact(painter, 0, true, false);
        break;
    case 8000:
        drawContact(painter, 0, false, true);
        break;
    case 8001:
        drawContact(painter, 0, true, true);
        break;
    case 9000:
        drawCoil(painter);
        break;
    case 9008:
        drawLamp(painter);
        break;
    default:
        drawUnknown(painter, object, symbolBox(object));
        break;
    }

    if (!object.label.isEmpty()) {
        const QRectF rect = labelRect(object, symbolBox(object));
        painter->setFont(labelFont());
        painter->setPen(QColor(0, 0, 0));
        painter->drawText(rect, Qt::AlignRight | Qt::AlignVCenter, object.label);
    }

    drawPinNames(painter, object);

    painter->restore();
}

} // namespace ui
