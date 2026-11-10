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
    Q_UNUSED(object)
    const double count = info.poles;
    const double width = (count - 1) * pitchPx() + 6.0 * kUnit;
    const QRectF core(-width / 2.0, 6.0 * kUnit, 1.2 * kUnit, 6.0 * kUnit);
    const double coilX = core.center().x();

    painter->drawLine(core.topLeft(), core.bottomLeft());
    painter->drawLine(core.topRight(), core.bottomRight());

    for (int i = 0; i < 3; ++i) {
        const double y = 6.5 * kUnit + i * 1.6 * kUnit;
        painter->drawArc(QRectF(coilX - 4.0 * kUnit, y, 3.0 * kUnit, 1.6 * kUnit),
                         90 * 16, 180 * 16);
        painter->drawArc(QRectF(coilX + 1.0 * kUnit, y, 3.0 * kUnit, 1.6 * kUnit),
                         -90 * 16, 180 * 16);
    }
}

void drawDiode(QPainter* painter)
{
    drawPinStub(painter, 0, 0.0, 4.0 * kUnit);
    drawPinStub(painter, 0, 8.0 * kUnit, spanPx());
    QPolygonF triangle;
    triangle << QPointF(-2.5 * kUnit, 4.0 * kUnit) << QPointF(2.5 * kUnit, 4.0 * kUnit)
             << QPointF(0, 8.0 * kUnit);
    painter->drawPolygon(triangle);
    painter->drawLine(QPointF(-2.5 * kUnit, 8.0 * kUnit), QPointF(2.5 * kUnit, 8.0 * kUnit));
}

void drawRectifier(QPainter* painter)
{
    const QRectF box(-3.0 * kUnit, 3.0 * kUnit, 6.0 * kUnit, 6.0 * kUnit);
    painter->drawRect(box);
    // Four diodes in a bridge, indicated by a diamond.
    QPolygonF diamond;
    diamond << QPointF(0, 4.0 * kUnit) << QPointF(2.0 * kUnit, 6.0 * kUnit)
            << QPointF(0, 8.0 * kUnit) << QPointF(-2.0 * kUnit, 6.0 * kUnit);
    painter->drawPolygon(diamond);
    painter->drawLine(QPointF(0, 0), QPointF(0, 3.0 * kUnit));
    painter->drawLine(QPointF(0, 9.0 * kUnit), QPointF(0, spanPx()));
}

void drawSupply(QPainter* painter, const Info& info)
{
    const double width = info.poles > 1 ? (info.poles - 1) * pitchPx() : 0.0;
    const QRectF box(-3.0 * kUnit, 3.0 * kUnit,
                     width + 6.0 * kUnit, 6.0 * kUnit);
    painter->drawRect(box);
    painter->drawText(box, Qt::AlignCenter, QStringLiteral("~"));
}

void drawBattery(QPainter* painter)
{
    drawPinStub(painter, 0, 0.0, 4.0 * kUnit);
    drawPinStub(painter, 0, 8.0 * kUnit, spanPx());
    painter->drawLine(QPointF(-2.5 * kUnit, 4.0 * kUnit), QPointF(2.5 * kUnit, 4.0 * kUnit));
    painter->drawLine(QPointF(-1.2 * kUnit, 6.0 * kUnit), QPointF(1.2 * kUnit, 6.0 * kUnit));
    painter->drawLine(QPointF(-2.5 * kUnit, 8.0 * kUnit), QPointF(2.5 * kUnit, 8.0 * kUnit));
}

void drawOutlet(QPainter* painter)
{
    const QRectF box(-3.0 * kUnit, 3.0 * kUnit, 6.0 * kUnit, 6.0 * kUnit);
    painter->drawRect(box);
    painter->drawEllipse(box.center(), 1.2 * kUnit, 1.2 * kUnit);
    painter->drawLine(QPointF(0, 0), QPointF(0, 3.0 * kUnit));
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
        painter->drawEllipse(QPointF(0, 0), 1.5 * kUnit, 1.5 * kUnit);
        painter->restore();
        return;
    }

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
        drawDiode(painter);
        break;
    case Family::Rectifier:
        drawRectifier(painter);
        break;
    case Family::Supply:
        drawSupply(painter, info);
        break;
    case Family::Battery:
        drawBattery(painter);
        break;
    case Family::Outlet:
        drawOutlet(painter);
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

    if (!object.label.isEmpty()) {
        const QRectF rect = labelRect(object, symbolBox(object));
        painter->setFont(labelFont());
        painter->setPen(QColor(0, 0, 0));
        painter->drawText(rect, Qt::AlignRight | Qt::AlignVCenter, object.label);
    }

    drawPinNames(painter, object, info);

    painter->restore();
}

} // namespace ui
