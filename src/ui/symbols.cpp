#include "ui/symbols.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QTransform>
#include <QVector>
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

QRectF recordedBox(const cad::Object& object, double defLeft, double defTop,
                   double defWidth, double defHeight);
int transformerTaps(const cad::Object& object);

int normalisedRotation(int rotation)
{
    rotation %= 360;
    return rotation < 0 ? rotation + 360 : rotation;
}

QRectF rotatedRect(const QRectF& rect, int rotation)
{
    if (rotation == 0)
        return rect;
    QTransform transform;
    transform.rotate(rotation);
    return transform.mapRect(rect);
}

// Number of pin names actually present. The parser keeps the full pin flag
// row, so empty entries must not be counted.
int namedPinCount(const cad::Object& object)
{
    int count = 0;
    for (const QString& pin : object.pins) {
        if (!pin.trimmed().isEmpty())
            ++count;
    }
    return count;
}

enum class Family {
    Unknown,
    PowerFeed,
    Fuse,
    FuseSwitch,
    Breaker,
    Differential,
    Contactor,
    Switch,
    Motor,
    Resistor,
    Drive,
    Contact,
    PushButton,
    Coil,
    Lamp,
    Timer,
    Terminal,
    Plc,
    Logic,
    Transformer,
    Diode,
    Rectifier,
    Supply,
    Battery,
    Outlet,
    Busbar,
    Analogue,
    Actuator,
};

struct Info {
    Family family = Family::Unknown;
    int poles = 1;
    bool neutral = false;    // last pole is a neutral link
    bool protective = false; // includes a PE terminal
    bool normallyClosed = false;
    bool twoWay = false;
    bool timed = false;
};

int polesFromPins(const cad::Object& object)
{
    const int pins = object.pins.size();
    if (pins >= 2 && pins <= 8 && pins % 2 == 0)
        return pins / 2;
    return 1;
}

Info describe(const cad::Object& object)
{
    Info info;
    const int type = object.type;

    if (type >= 3000 && type <= 3020) {
        info.family = Family::PowerFeed;
        switch (type) {
        case 3000: info.poles = 1; break;
        case 3001: info.poles = 1; info.neutral = true; break;
        case 3002: info.poles = 1; info.protective = true; break;
        case 3003: info.poles = 2; info.neutral = true; break;
        case 3011: info.poles = 3; info.neutral = true; info.protective = true; break;
        case 3004: info.poles = 3; break;
        case 3005: info.poles = 4; info.protective = true; break;
        case 3006: info.poles = 4; info.neutral = true; break;
        case 3007: info.poles = 5; info.neutral = true; info.protective = true; break;
        case 3008: info.poles = 1; break; // positive
        case 3009: info.poles = 1; break; // negative
        case 3010: info.poles = 2; break; // + / -
        case 3015: info.family = Family::PowerFeed; info.poles = 1; info.protective = true; break;
        case 3012: info.family = Family::Transformer; info.poles = 1; break;
        case 3017: info.family = Family::Transformer; info.poles = 3; break;
        case 3016: info.family = Family::Diode; break;
        case 3013: info.family = Family::Rectifier; break;
        case 3014: info.family = Family::Supply; break;
        case 3018: info.family = Family::Supply; info.poles = 3; break;
        case 3019: info.family = Family::Battery; break;
        case 3020: info.family = Family::Outlet; break;
        default: break;
        }
        return info;
    }

    if (type >= 5000 && type <= 5009) {
        info.family = type >= 5005 ? Family::FuseSwitch : Family::Fuse;
        switch (type) {
        case 5000: info.poles = 1; break;
        case 5001: info.poles = 2; info.neutral = true; break;
        case 5002: info.poles = 2; break;
        case 5003: info.poles = 3; break;
        case 5004: info.poles = 4; info.neutral = true; break;
        case 5005: info.poles = 1; break;
        case 5006: info.poles = 2; info.neutral = true; break;
        case 5007: info.poles = 2; break;
        case 5008: info.poles = 3; break;
        case 5009: info.poles = 4; info.neutral = true; break;
        default: break;
        }
        return info;
    }

    if (type >= 6000 && type <= 6014) {
        info.family = Family::Breaker;
        if (type == 6005 || type == 6006)
            info.family = Family::Differential;
        switch (type) {
        case 6000: info.poles = 1; break;
        case 6001: info.poles = 2; break;
        case 6002: info.poles = 2; break;
        case 6003: info.poles = 3; break;
        case 6004: info.poles = 4; break;
        case 6005: info.poles = 2; break;
        case 6006: info.poles = 4; break;
        case 6007: info.poles = 3; break;
        case 6008: info.poles = 2; break;
        case 6009: info.poles = 3; break;
        case 6010: info.poles = 1; break;
        case 6011: info.poles = 2; break;
        case 6012: info.poles = 4; break;
        case 6013: info.poles = 2; break;
        case 6014: info.poles = 4; break;
        default: break;
        }
        return info;
    }

    if (type >= 2000 && type <= 2008) {
        info.family = type == 2000 || type == 2001 || type == 2002 || type == 2006
                              || type == 2008
                          ? Family::Contactor
                          : Family::Switch;
        info.poles = polesFromPins(object);
        return info;
    }

    if (type == 10550) {
        info.family = Family::Actuator;
        return info;
    }

    if (type >= 1000 && type <= 1010) {
        info.family = Family::Motor;
        switch (type) {
        case 1000: info.poles = 4; break; // U1 V1 W1 PE
        case 1001: info.poles = 6; break; // Y/D
        default: info.poles = 3; break;
        }
        return info;
    }

    if (type >= 1050 && type <= 1062) {
        info.family = Family::Drive;
        if (type == 1050 || type == 1051 || type == 1052)
            info.family = Family::Resistor;
        info.poles = type == 1051 ? 2 : (type == 1052 ? 3 : 1);
        return info;
    }

    if (type >= 7000 && type <= 7015) {
        info.family = Family::Contact;
        info.normallyClosed = type == 7001 || type == 7004 || type == 7007
                              || type == 7010 || type == 7013;
        info.twoWay = type == 7002 || type == 7005 || type == 7008
                      || type == 7011 || type == 7014;
        info.timed = type >= 7012;
        return info;
    }

    if (type >= 7500 && type <= 7588) {
        info.family = Family::Timer;
        info.timed = true;
        return info;
    }

    if (type >= 8000 && type <= 8023) {
        info.family = Family::PushButton;
        info.normallyClosed = type == 8001 || type == 8004 || type == 8007
                              || type == 8010 || type == 8013 || type == 8016
                              || type == 8019 || type == 8022;
        info.twoWay = type == 8002 || type == 8005 || type == 8008
                      || type == 8011 || type == 8014 || type == 8017
                      || type == 8020 || type == 8023;
        return info;
    }

    if (type >= 9000 && type <= 9016) {
        info.family = type == 9008 || (type >= 9010 && type <= 9012) ? Family::Lamp
                                                                     : Family::Coil;
        return info;
    }

    if (type >= 1070 && type <= 1073) {
        info.family = Family::Contactor;
        int contactPins = 0;
        for (const QString& pin : object.pins) {
            if (!pin.isEmpty() && !pin.startsWith(QLatin1Char('A')))
                ++contactPins;
        }
        info.poles = qMax(1, contactPins / 2);
        return info;
    }

    if (type >= 9040 && type <= 9043) {
        info.family = Family::Busbar;
        info.poles = qMax(3, object.pins.size());
        return info;
    }

    if (type >= 10500 && type <= 10509) {
        info.family = Family::Analogue;
        return info;
    }

    if (type >= 10100 && type <= 10112) {
        info.family = Family::Terminal;
        return info;
    }

    if (type >= 10200 && type <= 10208) {
        info.family = Family::Plc;
        return info;
    }

    if (type >= 10000 && type <= 10075) {
        info.family = Family::Logic;
        return info;
    }

    return info;
}

// Logical extent of the drawn symbol in local pixels, before the label.
QRectF symbolBox(const cad::Object& object)
{
    const Info info = describe(object);

    switch (info.family) {
    case Family::PowerFeed:
        return QRectF(-3.0 * kUnit, 0, (info.poles - 1) * pitchPx() + 6.0 * kUnit,
                      7.0 * kUnit);

    case Family::Motor:
        return QRectF(-3.0 * kUnit, 0, ((info.poles > 1 ? info.poles - 1 : 0)) * pitchPx()
                                              + 6.0 * kUnit,
                      24.0 * kUnit);

    case Family::Terminal:
    case Family::Plc:
    case Family::Logic:
    case Family::Analogue:
    case Family::Actuator: {
        if (object.boxWidth() > 0 && object.boxHeight() > 0) {
            return QRectF(object.boxLeft() * kUnit, object.boxTop() * kUnit,
                          object.boxWidth() * kUnit, object.boxHeight() * kUnit);
        }
        return QRectF(-3.0 * kUnit, 0, 12.0 * kUnit, 12.0 * kUnit);
    }

    case Family::Transformer: {
        const int taps = transformerTaps(object);
        return recordedBox(object, -6.0, -9.0, 12.0 + qMax(0, taps - 2) * 3.0, 18.0);
    }

    case Family::Diode:
        return recordedBox(object, -3.0, -7.5, 6.0, 15.0);

    case Family::Rectifier:
        return recordedBox(object, -7.5, -10.5, 15.0, 21.0);

    case Family::Supply:
        return recordedBox(object, -7.5, -10.5, 15.0, 21.0);

    case Family::Battery:
        return recordedBox(object, -4.5, -6.0, 9.0, 12.0);

    case Family::Outlet:
        return recordedBox(object, -4.5, -7.5, 9.0, 15.0);

    default:
        return QRectF(-3.0 * kUnit, 0,
                      (info.poles - 1) * pitchPx() + 6.0 * kUnit,
                      spanPx());
    }
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

// The moving contact arm shared by contactors, switches and breakers.
void drawSwitchArm(QPainter* painter, double x, bool normallyClosed)
{
    const double gapTop = 3.0 * kUnit;
    const double gapBottom = 9.0 * kUnit;

    drawPinStub(painter, x, 0.0, gapTop);
    drawPinStub(painter, x, gapBottom, spanPx());

    // Fixed contact.
    painter->drawEllipse(QPointF(x, gapTop), 0.6 * kUnit, 0.6 * kUnit);

    if (normallyClosed) {
        painter->drawLine(QPointF(x, gapTop), QPointF(x, gapBottom));
        painter->drawLine(QPointF(x - 1.5 * kUnit, gapTop),
                          QPointF(x + 1.5 * kUnit, gapTop));
    } else {
        painter->drawLine(QPointF(x, gapBottom), QPointF(x + 3.0 * kUnit, gapTop));
    }
}

void drawContactorPoles(QPainter* painter, const Info& info)
{
    for (int pole = 0; pole < info.poles; ++pole) {
        const double x = poleX(pole);
        drawSwitchArm(painter, x, false);

        // The moving contact carries a short bar in IEC contactor symbols.
        painter->drawLine(QPointF(x + 1.0 * kUnit, 5.0 * kUnit),
                          QPointF(x + 3.4 * kUnit, 7.4 * kUnit));
    }

    painter->drawLine(QPointF(0, 6.0 * kUnit),
                      QPointF(poleX(info.poles - 1), 6.0 * kUnit));
}

void drawFusePole(QPainter* painter, double x, bool link)
{
    if (link) {
        // Neutral link: a straight lead with short terminal bars.
        drawPinStub(painter, x, 0.0, spanPx());
        painter->drawLine(QPointF(x - 1.5 * kUnit, 3.0 * kUnit),
                          QPointF(x + 1.5 * kUnit, 3.0 * kUnit));
        painter->drawLine(QPointF(x - 1.5 * kUnit, 9.0 * kUnit),
                          QPointF(x + 1.5 * kUnit, 9.0 * kUnit));
        return;
    }

    drawPinStub(painter, x, 0.0, 3.0 * kUnit);
    drawPinStub(painter, x, 9.0 * kUnit, spanPx());
    painter->drawRect(QRectF(x - 2.0 * kUnit, 3.0 * kUnit, 4.0 * kUnit, 6.0 * kUnit));
    painter->drawLine(QPointF(x, 3.0 * kUnit), QPointF(x, 9.0 * kUnit));
}

void drawFuses(QPainter* painter, const Info& info, bool disconnect)
{
    for (int pole = 0; pole < info.poles; ++pole) {
        const bool link = info.neutral && pole == info.poles - 1;
        const double x = poleX(pole);
        drawFusePole(painter, x, link);

        if (disconnect && !link) {
            // The disconnector's operating arm crosses the fuse body.
            painter->drawLine(QPointF(x - 2.0 * kUnit, 3.0 * kUnit),
                              QPointF(x + 3.5 * kUnit, 0.0));
        }
    }
}

void drawBreakers(QPainter* painter, const Info& info, bool differential)
{
    for (int pole = 0; pole < info.poles; ++pole) {
        const double x = poleX(pole);
        drawSwitchArm(painter, x, false);

        // Thermal-magnetic release: a small rectangle on the moving arm.
        painter->drawRect(QRectF(x + 0.6 * kUnit, 4.2 * kUnit, 3.2 * kUnit,
                                 3.2 * kUnit));
    }

    if (differential) {
        const double y = 6.0 * kUnit;
        painter->drawArc(QRectF(poleX(info.poles - 1) + 1.0 * kUnit, y - 2.5 * kUnit,
                                5.0 * kUnit, 5.0 * kUnit),
                         0, 180 * 16);
        painter->drawLine(QPointF(poleX(info.poles - 1) + 6.0 * kUnit, y),
                          QPointF(poleX(info.poles - 1) + 4.0 * kUnit, y - 2.0 * kUnit));
    }
}

void drawCoil(QPainter* painter, const cad::Object& object, const Info& info)
{
    Q_UNUSED(object)
    if (info.family == Family::Lamp) {
        drawPinStub(painter, 0, 0.0, 2.0 * kUnit);
        drawPinStub(painter, 0, 10.0 * kUnit, spanPx());

        const QPointF center(0, 6.0 * kUnit);
        const double radius = 4.0 * kUnit;
        painter->drawEllipse(center, radius, radius);

        const double d = radius * 0.7;
        painter->drawLine(center + QPointF(-d, -d), center + QPointF(d, d));
        painter->drawLine(center + QPointF(-d, d), center + QPointF(d, -d));
        return;
    }

    drawPinStub(painter, 0, 0.0, 4.0 * kUnit);
    drawPinStub(painter, 0, 8.0 * kUnit, spanPx());
    painter->drawRect(QRectF(-4.5 * kUnit, 4.0 * kUnit, 9.0 * kUnit, 4.0 * kUnit));
}

void drawTimer(QPainter* painter)
{
    drawCoil(painter, cad::Object{}, Info{Family::Coil});
    // Delay marking: a half-filled hourglass next to the coil.
    const QRectF box(-2.0 * kUnit, 4.4 * kUnit, 4.0 * kUnit, 3.2 * kUnit);
    painter->drawLine(box.topLeft(), box.bottomRight());
    painter->drawLine(box.bottomLeft(), box.topRight());
}

void drawMotor(QPainter* painter, const cad::Object& object, const Info& info)
{
    const double poles = info.poles > 1 ? info.poles : 3;
    const double height = object.boxHeight() > 0 ? object.boxHeight() : 24.0;
    const QPointF center((poles - 1) * pitchPx() / 2.0, height / 2.0 * kUnit);
    const double radius = qMax(4.0, height / 2.0 - 1.5) * kUnit;

    const double topPinEnd = qMax(0.0, center.y() - radius);
    for (int pin = 0; pin < qMin(3, info.poles); ++pin)
        drawPinStub(painter, poleX(pin), 0.0, topPinEnd);

    if (info.poles >= 4) {
        drawPinStub(painter, poleX(3), 0.0, center.y());
        painter->drawLine(QPointF(poleX(3), center.y()),
                          QPointF(center.x() + radius, center.y()));
    }

    // Y/D and two-speed motors bring the second winding out of the bottom.
    if (info.poles >= 6) {
        for (int pin = 0; pin < 3; ++pin) {
            painter->drawLine(QPointF(poleX(pin), center.y() + radius),
                              QPointF(poleX(pin), height * kUnit));
        }
    }

    painter->drawEllipse(center, radius, radius);

    QFont font = labelFont();
    font.setBold(true);
    painter->setFont(font);
    painter->drawText(QRectF(center.x() - radius, center.y() - radius,
                             radius * 2.0, radius * 2.0),
                      Qt::AlignCenter, QStringLiteral("M"));
}

void drawResistor(QPainter* painter, const Info& info)
{
    for (int pole = 0; pole < info.poles; ++pole) {
        const double x = poleX(pole);
        drawPinStub(painter, x, 0.0, 3.0 * kUnit);
        drawPinStub(painter, x, 9.0 * kUnit, spanPx());
        const QRectF body(x - 1.6 * kUnit, 3.0 * kUnit, 3.2 * kUnit, 6.0 * kUnit);
        painter->drawRect(body);
        painter->drawLine(QPointF(body.left(), body.top()),
                          QPointF(body.right(), body.bottom()));
    }
}

void drawDrive(QPainter* painter, const cad::Object& object)
{
    // Converter: input terminals on the left, output on the right, drawn as a
    // square with a diagonal separating the two conversion stages.
    const double half = spanPx() / 2.0;
    const QRectF box(-half * 0.75, kUnit, half * 1.5, spanPx() - 2.0 * kUnit);
    painter->drawRect(box);
    painter->drawLine(box.topLeft(), box.bottomRight());
    if (object.pins.size() > 2) {
        painter->drawLine(QPointF(box.left() + 3.0, box.top() + box.height() * 0.35),
                          QPointF(box.right() - 3.0, box.top() + box.height() * 0.35));
        painter->drawLine(QPointF(box.left() + 3.0, box.top() + box.height() * 0.65),
                          QPointF(box.right() - 3.0, box.top() + box.height() * 0.65));
    }
}

// Recorded symbol cell, in local pixels. Newly placed palette symbols store
// the box centred on the origin; imported records keep the original offsets.
QRectF recordedBox(const cad::Object& object, double defLeft, double defTop,
                   double defWidth, double defHeight)
{
    if (object.boxWidth() > 0 && object.boxHeight() > 0) {
        return QRectF(object.boxLeft() * kUnit, object.boxTop() * kUnit,
                      object.boxWidth() * kUnit, object.boxHeight() * kUnit);
    }
    return QRectF(defLeft * kUnit, defTop * kUnit,
                  defWidth * kUnit, defHeight * kUnit);
}

int transformerTaps(const cad::Object& object)
{
    return qBound(1, namedPinCount(object) / 2, 4);
}

// X positions of the winding taps, spread evenly across the winding bar.
QVector<double> transformerTapXs(const QRectF& box, int taps)
{
    QVector<double> xs;
    const double width = qMax(6.0 * kUnit, box.width() - 5.5 * kUnit);
    const double left = box.center().x() - width / 2.0;
    if (taps <= 1) {
        xs.append(box.center().x());
        return xs;
    }
    for (int i = 0; i < taps; ++i)
        xs.append(left + width * i / (taps - 1));
    return xs;
}

// Lead positions for power-supply symbols: all pins except the last (PE) are
// split into an upper and a lower row.
struct SupplyLeads
{
    QVector<double> top;
    QVector<double> bottom;
};

SupplyLeads supplyLeads(const cad::Object& object, const QRectF& rect)
{
    SupplyLeads leads;
    const int pins = qMax(2, namedPinCount(object));
    const int branch = qMax(1, pins - 1);
    const int topCount = (branch + 1) / 2;
    for (int i = 0; i < topCount; ++i)
        leads.top.append(rect.left() + (3.0 + 6.0 * i) * kUnit);
    for (int i = 0; i < branch - topCount; ++i)
        leads.bottom.append(rect.left() + (3.0 + 6.0 * i) * kUnit);
    return leads;
}

// Two-terminal ladder symbol: the first half of the named pins is on top, the
// rest below, one pole every pitch units.
QVector<QPointF> ladderPins(int count, int topCount, double span)
{
    QVector<QPointF> points;
    topCount = qBound(0, topCount, count);
    for (int i = 0; i < topCount; ++i)
        points.append(QPointF(poleX(i), 0));
    for (int i = topCount; i < count; ++i)
        points.append(QPointF(poleX(i - topCount), span));
    return points;
}

} // namespace

QVector<QPointF> terminalPositions(const cad::Object& object)
{
    if (object.isLine() || object.isText() || object.isJunction() || object.isWireGlyph())
        return {};

    const Info info = describe(object);
    const int count = namedPinCount(object);
    if (count <= 0)
        return {};

    switch (info.family) {
    case Family::PowerFeed:
        return ladderPins(count, count, spanPx());

    case Family::Fuse:
    case Family::FuseSwitch:
    case Family::Breaker:
    case Family::Differential:
    case Family::Contactor:
    case Family::Switch: {
        const int poles = qMax(1, info.poles);
        return ladderPins(count, poles, spanPx());
    }

    case Family::Contact:
    case Family::PushButton: {
        const int poles = qMax(1, info.twoWay ? 2 : 1);
        // Changeover forms carry an extra common terminal; keep the extra pins
        // on the top row so they still line up with the drawn poles.
        const int topCount = qMin(count, qMax(poles, (count + 1) / 2));
        return ladderPins(count, topCount, spanPx());
    }

    case Family::Motor:
        return ladderPins(count, count, spanPx());

    case Family::Coil:
    case Family::Lamp:
    case Family::Timer: {
        QVector<QPointF> points;
        points.append(QPointF(0, 0));
        if (count > 1)
            points.append(QPointF(0, spanPx()));
        for (int i = 2; i < count; ++i)
            points.append(QPointF(poleX(i - 1), spanPx()));
        return points;
    }

    case Family::Transformer: {
        const QRectF box = recordedBox(object, -6.0, -9.0, 12.0, 18.0);
        const double scale = box.height() / (18.0 * kUnit);
        const double leadTop = box.top() + 1.0 * kUnit * scale;
        const double leadBottom = box.top() + 16.0 * kUnit * scale;
        const int taps = transformerTaps(object);
        const QVector<double> xs = transformerTapXs(box, taps);
        QVector<QPointF> points;
        for (int i = 0; i < count; ++i) {
            const int row = i / taps;
            const int column = i % taps;
            const double x = xs.at(qBound(0, column, taps - 1));
            points.append(QPointF(x, row == 0 ? leadTop : leadBottom));
        }
        return points;
    }

    case Family::Supply: {
        const QRectF box = recordedBox(object, -7.5, -10.5, 15.0, 21.0);
        const QRectF rect(box.left(), box.top() + 3.0 * kUnit,
                          box.width(), qMin(12.0 * kUnit, box.height() - 3.0 * kUnit));
        const SupplyLeads leads = supplyLeads(object, rect);
        QVector<QPointF> points;
        for (double x : leads.top)
            points.append(QPointF(x, rect.top() - 3.0 * kUnit));
        for (double x : leads.bottom)
            points.append(QPointF(x, rect.bottom() + 3.0 * kUnit));
        while (points.size() < count)
            points.append(rect.bottomLeft() + QPointF(0, 5.0 * kUnit));
        points.resize(count);
        return points;
    }

    case Family::Battery: {
        const QRectF box = recordedBox(object, -4.5, -6.0, 9.0, 12.0);
        QVector<QPointF> points;
        points.append(QPointF(box.center().x(), box.top()));
        if (count > 1)
            points.append(QPointF(box.center().x(), box.top() + 9.0 * kUnit));
        return points;
    }

    case Family::Outlet: {
        const QRectF box = recordedBox(object, -4.5, -7.5, 9.0, 15.0);
        QVector<QPointF> points;
        points.append(QPointF(box.center().x(), box.top()));
        if (count > 1)
            points.append(QPointF(box.center().x(), box.top() + 12.0 * kUnit));
        return points;
    }

    case Family::Diode: {
        const QRectF box = recordedBox(object, -3.0, -7.5, 6.0, 15.0);
        const double scale = box.height() / (15.0 * kUnit);
        QVector<QPointF> points;
        points.append(QPointF(box.center().x(), box.top() + 1.0 * kUnit * scale));
        if (count > 1)
            points.append(QPointF(box.center().x(), box.top() + 13.0 * kUnit * scale));
        points.resize(count);
        return points;
    }

    case Family::Rectifier: {
        const QRectF box = recordedBox(object, -7.5, -10.5, 15.0, 21.0);
        QVector<QPointF> points;
        points.append(QPointF(box.center().x(), box.top() + 1.0 * kUnit));
        if (count > 1)
            points.append(QPointF(box.center().x(), box.bottom() - 2.0 * kUnit));
        points.resize(count);
        return points;
    }

    case Family::Resistor:
        return ladderPins(count, qMax(1, info.poles), spanPx());

    case Family::Busbar:
        return ladderPins(count, count, spanPx());

    case Family::Terminal:
    case Family::Plc:
    case Family::Logic:
    case Family::Analogue:
    case Family::Actuator:
    case Family::Drive: {
        const QRectF box = symbolBox(object);
        QVector<QPointF> points;
        points.append(QPointF(box.center().x(), box.top()));
        if (count > 1)
            points.append(QPointF(box.center().x(), box.bottom()));
        while (points.size() < count)
            points.append(QPointF(box.left(), box.top() + points.size() * pitchPx()));
        points.resize(count);
        return points;
    }

    case Family::Unknown:
        break;
    }

    return ladderPins(count, qMax(1, count / 2), spanPx());
}

void drawPowerFeed(QPainter* painter, const cad::Object& object, const Info& info)
{
    const double top = 0.0;
    const double circleY = 5.0 * kUnit;
    const double radius = 1.6 * kUnit;

    for (int pole = 0; pole < info.poles; ++pole) {
        const double x = poleX(pole);
        const bool pe = info.protective && pole == info.poles - 1;
        const bool neutral = info.neutral && pole == info.poles - 1;

        if (pe) {
            // Protective earth: the standard three-bar earth symbol.
            drawPinStub(painter, x, top, 2.0 * kUnit);
            painter->drawLine(QPointF(x - 2.5 * kUnit, 2.0 * kUnit),
                              QPointF(x + 2.5 * kUnit, 2.0 * kUnit));
            painter->drawLine(QPointF(x - 1.6 * kUnit, 3.2 * kUnit),
                              QPointF(x + 1.6 * kUnit, 3.2 * kUnit));
            painter->drawLine(QPointF(x - 0.8 * kUnit, 4.4 * kUnit),
                              QPointF(x + 0.8 * kUnit, 4.4 * kUnit));
            painter->drawLine(QPointF(x, 4.4 * kUnit), QPointF(x, 6.0 * kUnit));
            continue;
        }

        drawPinStub(painter, x, top, circleY - radius);
        painter->drawEllipse(QPointF(x, circleY), radius, radius);

        if (neutral) {
            // Neutral: a short bar through the terminal.
            painter->drawLine(QPointF(x - 1.0 * kUnit, circleY + radius + 0.4 * kUnit),
                              QPointF(x + 1.0 * kUnit, circleY + radius + 0.4 * kUnit));
        }
    }

    // Polarity marks for DC feeds.
    if (object.type == 3008) {
        painter->drawLine(QPointF(-1.5 * kUnit, -1.6 * kUnit),
                          QPointF(1.5 * kUnit, -1.6 * kUnit));
        painter->drawLine(QPointF(0, -3.1 * kUnit), QPointF(0, -0.1 * kUnit));
    } else if (object.type == 3009) {
        painter->drawLine(QPointF(-1.5 * kUnit, -1.8 * kUnit),
                          QPointF(1.5 * kUnit, -1.8 * kUnit));
    }
}

void drawTransformer(QPainter* painter, const cad::Object& object, const Info& info)
{
    Q_UNUSED(info)
    const QRectF box = recordedBox(object, -6.0, -9.0, 12.0, 18.0);
    const int taps = transformerTaps(object);
    const QVector<double> xs = transformerTapXs(box, taps);

    const double scale = box.height() / (18.0 * kUnit);
    const double topBar = box.top() + 4.0 * kUnit * scale;
    const double barHeight = 3.3 * kUnit * scale;
    const double coreOne = box.top() + 8.0 * kUnit * scale;
    const double coreTwo = box.top() + 9.0 * kUnit * scale;
    const double bottomBar = box.top() + 10.0 * kUnit * scale;
    const double leadTop = box.top() + 1.0 * kUnit * scale;
    const double leadBottom = box.top() + 16.0 * kUnit * scale;

    const double barLeft = xs.first() - 0.45 * kUnit;
    const double barRight = xs.last() + 0.45 * kUnit;
    const QRectF topWinding(barLeft, topBar, barRight - barLeft, barHeight);
    const QRectF bottomWinding(barLeft, bottomBar, barRight - barLeft, barHeight);
    const QRectF firstCore(barLeft + 0.2 * kUnit, coreOne,
                           barRight - barLeft - 0.4 * kUnit, 0.45 * kUnit);
    const QRectF secondCore(barLeft + 0.2 * kUnit, coreTwo,
                            barRight - barLeft - 0.4 * kUnit, 0.45 * kUnit);

    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0));
    painter->drawRect(topWinding);
    painter->drawRect(bottomWinding);
    painter->drawRect(firstCore);
    painter->drawRect(secondCore);
    painter->restore();

    for (double x : xs) {
        painter->drawLine(QPointF(x, leadTop), QPointF(x, topBar));
        painter->drawLine(QPointF(x, bottomBar + barHeight), QPointF(x, leadBottom));
    }
}

void drawDiode(QPainter* painter, const cad::Object& object)
{
    const QRectF box = recordedBox(object, -3.0, -7.5, 6.0, 15.0);
    const double cx = box.center().x();
    const double scale = box.height() / (15.0 * kUnit);

    const double leadTop = box.top() + 1.0 * kUnit * scale;
    const double triangleTop = box.top() + 4.3 * kUnit * scale;
    const double cathode = box.top() + 9.0 * kUnit * scale;
    const double leadBottom = box.top() + 13.0 * kUnit * scale;
    const double halfWidth = qMin(box.width() / 2.0, 2.7 * kUnit);

    painter->drawLine(QPointF(cx, leadTop), QPointF(cx, triangleTop));

    QPolygonF triangle;
    triangle << QPointF(cx - halfWidth, triangleTop)
             << QPointF(cx + halfWidth, triangleTop)
             << QPointF(cx, cathode);
    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0));
    painter->drawPolygon(triangle);
    painter->restore();

    painter->drawLine(QPointF(cx - halfWidth, cathode),
                      QPointF(cx + halfWidth, cathode));
    painter->drawLine(QPointF(cx, cathode), QPointF(cx, leadBottom));
}

void drawRectifier(QPainter* painter, const cad::Object& object)
{
    const QRectF box = recordedBox(object, -7.5, -10.5, 15.0, 21.0);
    const QPointF centre = box.center();
    const double halfWidth = qMin(box.width() / 2.0 + 1.5 * kUnit, 9.0 * kUnit);
    const double halfHeight = qMin(box.height() / 2.0 - 4.5 * kUnit, 5.8 * kUnit);

    painter->drawLine(QPointF(centre.x(), box.top() + 1.0 * kUnit),
                      QPointF(centre.x(), centre.y() - halfHeight));
    painter->drawLine(QPointF(centre.x(), centre.y() + halfHeight),
                      QPointF(centre.x(), box.bottom() - 2.0 * kUnit));

    QPolygonF diamond;
    diamond << QPointF(centre.x(), centre.y() - halfHeight)
            << QPointF(centre.x() + halfWidth, centre.y())
            << QPointF(centre.x(), centre.y() + halfHeight)
            << QPointF(centre.x() - halfWidth, centre.y());
    painter->drawPolygon(diamond);

    // AC side of the bridge: the centre line runs past the diamond.
    painter->drawLine(QPointF(centre.x() - halfWidth - 0.8 * kUnit, centre.y()),
                      QPointF(centre.x() + halfWidth + 0.8 * kUnit, centre.y()));

    // Rectifier diode inside the bridge.
    const double anode = centre.x() - 1.9 * kUnit;
    const double bar = centre.x() + 1.9 * kUnit;
    const double half = 2.0 * kUnit;
    QPolygonF diode;
    diode << QPointF(anode, centre.y() - half) << QPointF(anode, centre.y() + half)
          << QPointF(bar, centre.y());
    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0));
    painter->drawPolygon(diode);
    painter->restore();
    painter->drawLine(QPointF(bar, centre.y() - half), QPointF(bar, centre.y() + half));
}

void drawSupply(QPainter* painter, const cad::Object& object)
{
    const QRectF box = recordedBox(object, -7.5, -10.5, 15.0, 21.0);
    const QRectF rect(box.left(), box.top() + 3.0 * kUnit,
                      box.width(), qMin(12.0 * kUnit, box.height() - 3.0 * kUnit));

    painter->drawRect(rect);
    painter->drawLine(rect.topRight(), rect.bottomLeft());

    // AC input mark (~) in the upper-left compartment.
    const QPointF tilde(rect.left() + 2.0 * kUnit, rect.top() + 2.6 * kUnit);
    const double wave = 1.6 * kUnit;
    QPainterPath path(tilde);
    path.cubicTo(tilde.x() + wave * 0.4, tilde.y() - wave,
                 tilde.x() + wave * 0.9, tilde.y() - wave,
                 tilde.x() + wave * 1.3, tilde.y());
    path.cubicTo(tilde.x() + wave * 1.7, tilde.y() + wave,
                 tilde.x() + wave * 2.2, tilde.y() + wave,
                 tilde.x() + wave * 2.6, tilde.y());
    painter->drawPath(path);

    // DC output mark (=) in the lower-right compartment.
    const double equalsY = rect.top() + 8.6 * kUnit;
    const double equalsHalf = 1.6 * kUnit;
    painter->drawLine(QPointF(rect.center().x() - equalsHalf, equalsY),
                      QPointF(rect.center().x() + equalsHalf, equalsY));
    painter->drawLine(QPointF(rect.center().x() - equalsHalf, equalsY + kUnit),
                      QPointF(rect.center().x() + equalsHalf, equalsY + kUnit));

    const SupplyLeads leads = supplyLeads(object, rect);
    for (double x : leads.top)
        painter->drawLine(QPointF(x, rect.top() - 3.0 * kUnit), QPointF(x, rect.top()));
    for (double x : leads.bottom)
        painter->drawLine(QPointF(x, rect.bottom()), QPointF(x, rect.bottom() + 3.0 * kUnit));

    // Protective earth on the lower-left corner.
    QString lastPin;
    for (const QString& pin : object.pins) {
        if (!pin.trimmed().isEmpty())
            lastPin = pin.trimmed();
    }
    if (lastPin.compare(QStringLiteral("PE"), Qt::CaseInsensitive) == 0)
        painter->drawLine(rect.bottomLeft(), rect.bottomLeft() + QPointF(0, 5.0 * kUnit));
}

void drawBattery(QPainter* painter, const cad::Object& object)
{
    const QRectF box = recordedBox(object, -4.5, -6.0, 9.0, 12.0);
    const double cx = box.center().x();
    const double leadTop = box.top();
    const double leadBottom = box.top() + 9.0 * kUnit;
    const double longPlate = box.top() + 3.5 * kUnit;
    const double shortPlate = box.top() + 5.3 * kUnit;

    painter->drawLine(QPointF(cx, leadTop), QPointF(cx, leadBottom));
    painter->drawLine(QPointF(cx - 3.0 * kUnit, longPlate),
                      QPointF(cx + 3.0 * kUnit, longPlate));
    painter->drawLine(QPointF(cx - 2.0 * kUnit, shortPlate),
                      QPointF(cx + 2.0 * kUnit, shortPlate));
}

void drawOutlet(QPainter* painter, const cad::Object& object)
{
    const QRectF box = recordedBox(object, -4.5, -7.5, 9.0, 15.0);
    const double cx = box.center().x();
    const double cy = box.top() + 5.8 * kUnit;
    const double radius = 2.9 * kUnit;

    painter->drawLine(QPointF(cx, box.top()), QPointF(cx, cy - radius));
    painter->drawLine(QPointF(cx, cy + radius), QPointF(cx, box.top() + 12.0 * kUnit));
    painter->drawEllipse(QPointF(cx, cy), radius, radius);

    // Three socket holes: two on the left, one on the right.
    const double hole = 0.42 * kUnit;
    painter->drawEllipse(QPointF(cx - 1.0 * kUnit, cy - 1.5 * kUnit), hole, hole);
    painter->drawEllipse(QPointF(cx - 1.0 * kUnit, cy + 1.5 * kUnit), hole, hole);
    painter->drawEllipse(QPointF(cx + 1.5 * kUnit, cy), hole, hole);
}

void drawContactForm(QPainter* painter, const Info& info)
{
    const int poles = info.twoWay ? 2 : 1;
    for (int pole = 0; pole < poles; ++pole)
        drawContact(painter, poleX(pole), info.normallyClosed, false);
}

void drawPushButton(QPainter* painter, const Info& info)
{
    const int poles = info.twoWay ? 2 : 1;
    for (int pole = 0; pole < poles; ++pole) {
        const double x = poleX(pole);
        drawContact(painter, x, info.normallyClosed, true);
    }
    // Common actuator bar joining both contacts.
    painter->drawLine(QPointF(-5.0 * kUnit, 6.0 * kUnit),
                      QPointF(poleX(poles - 1) - 5.0 * kUnit, 6.0 * kUnit));
}

void drawTerminal(QPainter* painter, const cad::Object& object)
{
    const QRectF box(-2.0 * kUnit, 3.0 * kUnit, 4.0 * kUnit, 4.0 * kUnit);
    painter->drawRect(box);
    painter->drawLine(QPointF(0, 0), QPointF(0, 3.0 * kUnit));
    painter->drawLine(QPointF(0, 7.0 * kUnit), QPointF(0, 10.0 * kUnit));
    if (!object.label.isEmpty()) {
        QFont font = labelFont();
        font.setPixelSize(10);
        painter->setFont(font);
        painter->drawText(box, Qt::AlignCenter, object.label);
    }
}

void drawBlock(QPainter* painter, const cad::Object& object, const QString& caption)
{
    QRectF box(-3.0 * kUnit, kUnit, 12.0 * kUnit, 10.0 * kUnit);
    if (object.boxWidth() > 0 && object.boxHeight() > 0) {
        box = QRectF(object.boxLeft() * kUnit, object.boxTop() * kUnit,
                     object.boxWidth() * kUnit, object.boxHeight() * kUnit);
    }
    painter->drawRect(box);

    QFont font = labelFont();
    font.setPixelSize(11);
    painter->setFont(font);
    painter->drawText(box.adjusted(2, 1, -2, -1),
                      Qt::AlignHCenter | Qt::AlignVCenter,
                      object.label.isEmpty() ? caption : object.label);
}

void drawActuator(QPainter* painter, const cad::Object& object)
{
    QRectF box(-15.0 * kUnit, -15.0 * kUnit, 60.0 * kUnit, 30.0 * kUnit);
    if (object.boxWidth() > 0 && object.boxHeight() > 0) {
        box = QRectF(object.boxLeft() * kUnit, object.boxTop() * kUnit,
                     object.boxWidth() * kUnit, object.boxHeight() * kUnit);
    }

    const double size = qMin(box.height(), 24.0 * kUnit);
    const QRectF motor(box.left(), box.center().y() - size / 2.0, size, size);
    painter->drawRect(motor);

    QFont font = labelFont();
    font.setBold(true);
    painter->setFont(font);
    painter->drawText(motor, Qt::AlignCenter, QStringLiteral("M"));

    // Linear actuator: a rod running from the motor to the far end.
    painter->drawLine(QPointF(motor.right(), box.center().y()),
                      QPointF(box.right(), box.center().y()));
    painter->drawLine(QPointF(box.right(), box.center().y() - 1.5 * kUnit),
                      QPointF(box.right(), box.center().y() + 1.5 * kUnit));
}

void drawBusbar(QPainter* painter, const Info& info)
{
    const int poles = qMax(3, info.poles);
    const double top = 4.0 * kUnit;
    painter->drawLine(QPointF(0, top), QPointF(poleX(poles - 1), top));
    for (int pole = 0; pole < poles; ++pole) {
        const double x = poleX(pole);
        drawPinStub(painter, x, 0.0, top);
        painter->drawLine(QPointF(x - 1.0 * kUnit, top),
                          QPointF(x + 1.0 * kUnit, top));
    }
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

QStringList pinNames(const cad::Object& object)
{
    QStringList names;
    for (const QString& pin : object.pins) {
        if (!pin.trimmed().isEmpty())
            names.append(pin.trimmed());
    }
    return names;
}

void drawPinNames(QPainter* painter, const cad::Object& object, const Info& info)
{
    const QStringList names = pinNames(object);
    if (names.isEmpty())
        return;

    QFont font = labelFont();
    font.setPixelSize(9);
    painter->setFont(font);
    painter->setPen(QColor(70, 70, 70));

    const double above = -12.0;
    const double below = spanPx() + 1.0;

    if (info.family == Family::Transformer) {
        const QRectF box = symbolBox(object);
        const int taps = transformerTaps(object);
        const QVector<double> xs = transformerTapXs(box, taps);
        for (int i = 0; i < taps && i < names.size(); ++i)
            painter->drawText(QRectF(xs.at(i) - 8.0, box.top() - 13.0, 16.0, 11.0),
                              Qt::AlignCenter, names.at(i));
        for (int i = 0; i < taps && taps + i < names.size(); ++i)
            painter->drawText(QRectF(xs.at(i) - 8.0, box.bottom() + 2.0, 16.0, 11.0),
                              Qt::AlignCenter, names.at(taps + i));
        return;
    }

    if (info.family == Family::Supply) {
        const QRectF box = symbolBox(object);
        const QRectF rect(box.left(), box.top() + 3.0 * kUnit,
                          box.width(), qMin(12.0 * kUnit, box.height() - 3.0 * kUnit));
        const SupplyLeads leads = supplyLeads(object, rect);
        int index = 0;
        for (double x : leads.top) {
            if (index < names.size())
                painter->drawText(QRectF(x - 8.0, rect.top() - 13.0, 16.0, 11.0),
                                  Qt::AlignCenter, names.at(index));
            ++index;
        }
        for (double x : leads.bottom) {
            if (index < names.size())
                painter->drawText(QRectF(x - 8.0, rect.bottom() + 2.0, 16.0, 11.0),
                                  Qt::AlignCenter, names.at(index));
            ++index;
        }
        if (index < names.size())
            painter->drawText(QRectF(rect.left() - 20.0, rect.bottom() + 6.0, 16.0, 11.0),
                              Qt::AlignCenter, names.at(index));
        return;
    }

    if (info.family == Family::Battery) {
        const QRectF box = symbolBox(object);
        const double x = box.center().x() + 2.5 * kUnit;
        if (names.size() > 0)
            painter->drawText(QRectF(x, box.top() + 2.0 * kUnit, 12.0, 11.0),
                              Qt::AlignLeft | Qt::AlignVCenter, names.at(0));
        if (names.size() > 1)
            painter->drawText(QRectF(x, box.top() + 4.2 * kUnit, 12.0, 11.0),
                              Qt::AlignLeft | Qt::AlignVCenter, names.at(1));
        return;
    }

    if (info.family == Family::Diode) {
        const QRectF box = symbolBox(object);
        const double cx = box.center().x();
        const double scale = box.height() / (15.0 * kUnit);
        if (names.size() > 0)
            painter->drawText(QRectF(cx + 4.0, box.top() + 2.0 * kUnit * scale, 14.0, 11.0),
                              Qt::AlignLeft | Qt::AlignVCenter, names.at(0));
        if (names.size() > 1)
            painter->drawText(QRectF(cx + 4.0, box.top() + 9.0 * kUnit * scale, 14.0, 11.0),
                              Qt::AlignLeft | Qt::AlignVCenter, names.at(1));
        return;
    }

    if (info.family == Family::Rectifier) {
        const QRectF box = symbolBox(object);
        const QPointF centre = box.center();
        if (names.size() > 0)
            painter->drawText(QRectF(centre.x() + 4.0, box.top() + 1.0 * kUnit, 14.0, 11.0),
                              Qt::AlignLeft | Qt::AlignVCenter, names.at(0));
        if (names.size() > 1)
            painter->drawText(QRectF(centre.x() + 4.0, box.bottom() - 3.0 * kUnit, 14.0, 11.0),
                              Qt::AlignLeft | Qt::AlignVCenter, names.at(1));
        if (names.size() > 2)
            painter->drawText(QRectF(box.left() - 6.0, centre.y() - 16.0, 14.0, 11.0),
                              Qt::AlignRight | Qt::AlignVCenter, names.at(2));
        if (names.size() > 3)
            painter->drawText(QRectF(box.right() - 8.0, centre.y() - 16.0, 14.0, 11.0),
                              Qt::AlignLeft | Qt::AlignVCenter, names.at(3));
        return;
    }

    if (info.family == Family::Outlet)
        return;

    if (info.family == Family::Motor) {
        for (int i = 0; i < names.size() && i < 4; ++i) {
            painter->drawText(QRectF(poleX(i) - 14.0, above, 28.0, 12.0),
                              Qt::AlignCenter, names.at(i));
        }
        return;
    }

    if (info.poles > 1 && info.family != Family::Unknown) {
        for (int i = 0; i < info.poles && i < names.size(); ++i) {
            painter->drawText(QRectF(poleX(i) - 14.0, above, 28.0, 12.0),
                              Qt::AlignCenter, names.at(i));
        }
        for (int i = 0; i < info.poles && info.poles + i < names.size(); ++i) {
            painter->drawText(QRectF(poleX(i) - 14.0, below, 28.0, 12.0),
                              Qt::AlignCenter, names.at(info.poles + i));
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

QRectF baseLocalBounds(const cad::Object& object)
{
    if (object.isWireGlyph())
        return QRectF(-9, -9, 18, 18);

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

QRectF localBounds(const cad::Object& object)
{
    const int rotation = normalisedRotation(object.rotation);
    if (rotation == 0 || object.isLine() || object.isText() || object.isJunction()
        || object.isWireGlyph()) {
        return baseLocalBounds(object);
    }

    QRectF bounds = rotatedRect(symbolBox(object), rotation);
    if (!object.label.isEmpty()) {
        // Labels stay upright, centred where the rotated label anchor lands.
        const QRectF baseLabel = labelRect(object, symbolBox(object));
        QTransform transform;
        transform.rotate(rotation);
        const QPointF centre = transform.map(baseLabel.center());
        QRectF upright(baseLabel);
        upright.moveCenter(centre);
        bounds = bounds.united(upright);
    }

    return bounds.adjusted(-3, -3, 3, 3);
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

void paintObject(QPainter* painter, const cad::Object& object, bool selected,
                 bool simulated)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (selected) {
        painter->setPen(QPen(QColor(0, 120, 215), 1.2, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(localBounds(object).adjusted(-2, -2, 2, 2));
    }

    if (simulated) {
        if (object.isLine() && !object.isWireGlyph()) {
            // Glow underneath a live conductor.
            QPen glow(QColor(0, 175, 70, 110), 7.0);
            glow.setCapStyle(Qt::RoundCap);
            painter->setPen(glow);
            painter->drawLine(QPointF(0, 0),
                              QPointF((object.x2() - object.x()) * kUnit,
                                      (object.y2() - object.y()) * kUnit));
        } else if (!object.isText() && !object.isJunction() && !object.isWireGlyph()) {
            painter->setPen(QPen(QColor(0, 150, 60), 2.0));
            painter->setBrush(QColor(0, 180, 80, 45));
            painter->drawRoundedRect(localBounds(object).adjusted(-1, -1, 1, 1), 4, 4);
            painter->setBrush(Qt::NoBrush);
        }
    }

    if (object.isWireGlyph()) {
        // Small cross-reference / connection marker from the drawing palette.
        const bool marker = object.type >= 4011;
        const QColor colour = marker ? QColor(251, 0, 0) : lineColor(object);
        painter->setPen(QPen(colour, 1.8));
        painter->setBrush(marker ? QBrush(colour) : QBrush(Qt::NoBrush));
        if (marker) {
            QPolygonF diamond;
            diamond << QPointF(0, -6.0) << QPointF(6.0, 0)
                    << QPointF(0, 6.0) << QPointF(-6.0, 0);
            painter->drawPolygon(diamond);
        } else {
            const double dir = (object.type == 4003) ? 1.0 : -1.0;
            painter->drawLine(QPointF(-6.0, -6.0 * dir), QPointF(6.0, 6.0 * dir));
        }
        if (!object.label.isEmpty()) {
            painter->setPen(QColor(0, 0, 0));
            painter->drawText(QRectF(-26.0, -18.0, 20.0, 12.0),
                              Qt::AlignRight | Qt::AlignVCenter, object.label);
        }
        painter->restore();
        return;
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
        painter->drawEllipse(QPointF(0, 0), 1.5 * kUnit, 1.5 * kUnit);
        painter->restore();
        return;
    }

    const int rotation = normalisedRotation(object.rotation);
    if (rotation != 0)
        painter->rotate(rotation);

    const Info info = describe(object);
    painter->setPen(QPen(symbolColor(object), 2.0));
    painter->setBrush(Qt::NoBrush);

    switch (info.family) {
    case Family::Motor:
        drawMotor(painter, object, info);
        break;
    case Family::Contactor:
        drawContactorPoles(painter, info);
        break;
    case Family::Switch:
        for (int pole = 0; pole < info.poles; ++pole)
            drawSwitchArm(painter, poleX(pole), false);
        break;
    case Family::Fuse:
    case Family::FuseSwitch:
        drawFuses(painter, info, info.family == Family::FuseSwitch);
        break;
    case Family::Breaker:
    case Family::Differential:
        drawBreakers(painter, info, info.family == Family::Differential);
        break;
    case Family::PowerFeed:
        drawPowerFeed(painter, object, info);
        break;
    case Family::Transformer:
        drawTransformer(painter, object, info);
        break;
    case Family::Diode:
        drawDiode(painter, object);
        break;
    case Family::Rectifier:
        drawRectifier(painter, object);
        break;
    case Family::Supply:
        drawSupply(painter, object);
        break;
    case Family::Battery:
        drawBattery(painter, object);
        break;
    case Family::Outlet:
        drawOutlet(painter, object);
        break;
    case Family::Resistor:
        drawResistor(painter, info);
        break;
    case Family::Drive:
        drawDrive(painter, object);
        break;
    case Family::Contact:
        drawContactForm(painter, info);
        break;
    case Family::PushButton:
        drawPushButton(painter, info);
        break;
    case Family::Coil:
    case Family::Lamp:
        drawCoil(painter, object, info);
        break;
    case Family::Timer:
        drawTimer(painter);
        break;
    case Family::Terminal:
        drawTerminal(painter, object);
        break;
    case Family::Plc:
        drawBlock(painter, object, QStringLiteral("PLC"));
        break;
    case Family::Logic:
        drawBlock(painter, object, QStringLiteral("&"));
        break;
    case Family::Busbar:
        drawBusbar(painter, info);
        break;
    case Family::Actuator:
        drawActuator(painter, object);
        break;
    case Family::Analogue:
        drawBlock(painter, object, QStringLiteral("A"));
        break;
    case Family::Unknown:
        drawUnknown(painter, object, symbolBox(object));
        break;
    }

    drawPinNames(painter, object, info);

    if (rotation != 0)
        painter->rotate(-rotation);

    if (!object.label.isEmpty()) {
        QRectF rect = labelRect(object, symbolBox(object));
        if (rotation != 0) {
            QTransform transform;
            transform.rotate(rotation);
            rect.moveCenter(transform.map(rect.center()));
        }

        painter->setFont(labelFont());
        painter->setPen(QColor(0, 0, 0));
        painter->drawText(rect, Qt::AlignRight | Qt::AlignVCenter, object.label);
    }

    painter->restore();
}

} // namespace ui
