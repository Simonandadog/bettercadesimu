#include "sim/simulator.h"

#include <QPair>
#include <QPointF>
#include <QTransform>
#include <QtMath>

#include "ui/symbols.h"

namespace sim {
namespace {

using PointKey = QPair<int, int>;

PointKey keyFor(const QPointF& point)
{
    return qMakePair(qRound(point.x()), qRound(point.y()));
}

// Two-terminal normally-closed forms. The numbering follows IEC auxiliary
// contact marking: 11-12 and 55-56 are NC, 13-14 and 67-68 / 97-98 are NO.
bool isNormallyClosed(int type)
{
    const bool contact = type == 7001 || type == 7005 || type == 7009 || type == 7013;
    const bool button = type == 8001 || type == 8005 || type == 8009 || type == 8013;
    const bool thermal = type == 8017;
    return contact || button || thermal;
}

bool isContactor(int type)
{
    return (type >= 2000 && type <= 2002) || type == 2006 || type == 2008;
}
// Section-11 contactor relays carry their coil (A1/A2) and power contacts in
// one component.
bool isCombinedContactor(int type) { return type >= 1070 && type <= 1073; }
bool isSwitch(int type)
{
    // Section-4 manual switches. 2006 is the single-pole contactor and is not
    // a manual device.
    return type == 2003 || type == 2004 || type == 2005 || type == 2007;
}
bool isContact(int type) { return type >= 7000 && type <= 7015; }
bool isPushButton(int type) { return type >= 8000 && type <= 8023; }
// Push buttons are momentary: they conduct only while held. 8020-8022 are
// maintained rotary/cam switches and latch like the section-4 switches.
bool isMomentaryButton(int type) { return type >= 8000 && type <= 8015; }
bool isMaintainedSwitch(int type) { return isSwitch(type) || (type >= 8020 && type <= 8022); }
bool isManualInput(int type) { return isMomentaryButton(type) || isMaintainedSwitch(type); }
bool isFuse(int type) { return type >= 5000 && type <= 5009; }
bool isBreaker(int type) { return type >= 6000 && type <= 6014; }
bool isCoil(int type) { return (type >= 9000 && type <= 9007) || (type >= 9014 && type <= 9016); }
bool isLamp(int type) { return type >= 9008 && type <= 9013; }
bool isMotor(int type) { return type >= 1000 && type <= 1010; }
bool isTimer(int type) { return type >= 7500 && type <= 7588; }
bool isResistor(int type) { return type >= 1050 && type <= 1052; }
bool isTerminalBlock(int type) { return type >= 10100 && type <= 10112; }
bool isBusbar(int type) { return type >= 9040 && type <= 9043; }
bool isCoilDevice(int type)
{
    return isCoil(type) || isTimer(type) || isCombinedContactor(type);
}
bool isLoad(int type)
{
    return isCoilDevice(type) || isLamp(type) || isMotor(type) || isResistor(type);
}

// Terminal indices used by a two-terminal load, taken from the pin names when
// possible. Coils are A1/A2, lamps X1/X2 and timers 1/2; a coil such as 9001
// (A1, B1, A2) must not be measured across A1/B1.
QPair<int, int> loadTerminals(const cad::Object& object, const QStringList& names)
{
    auto find = [&names](const QString& a, const QString& b) -> QPair<int, int> {
        const int ia = names.indexOf(a);
        const int ib = names.indexOf(b);
        if (ia >= 0 && ib >= 0)
            return qMakePair(ia, ib);
        return qMakePair(-1, -1);
    };

    QPair<int, int> pair(-1, -1);
    if (isCoilDevice(object.type))
        pair = find(QStringLiteral("A1"), QStringLiteral("A2"));
    if (pair.first < 0 && (isLamp(object.type) || isCoil(object.type)))
        pair = find(QStringLiteral("X1"), QStringLiteral("X2"));
    if (pair.first < 0 && (isTimer(object.type) || isResistor(object.type)))
        pair = find(QStringLiteral("1"), QStringLiteral("2"));
    if (pair.first < 0 && isResistor(object.type))
        pair = find(QStringLiteral("A1"), QStringLiteral("A2"));
    if (pair.first < 0 && names.size() >= 2)
        pair = qMakePair(0, 1);
    return pair;
}

QStringList namedPins(const cad::Object& object)
{
    QStringList names;
    for (const QString& pin : object.pins) {
        if (!pin.trimmed().isEmpty())
            names.append(pin.trimmed());
    }
    return names;
}

int topPinCount(const cad::Object& object, int count)
{
    if (isContactor(object.type))
        return qBound(1, count / 2, count);
    if (isContact(object.type) || isPushButton(object.type)) {
        const int poles = isNormallyClosed(object.type) ? 1 : qMax(1, count / 2);
        return qMin(count, qMax(poles, (count + 1) / 2));
    }
    if (isSwitch(object.type) || isFuse(object.type) || isBreaker(object.type))
        return qBound(1, count / 2, count);
    return count;
}

bool onSegment(const QPointF& point, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const QPointF ap = point - a;
    const double lengthSquared = QPointF::dotProduct(ab, ab);
    if (lengthSquared < 0.01)
        return QPointF::dotProduct(ap, ap) < 2.0;

    const double t = QPointF::dotProduct(ap, ab) / lengthSquared;
    if (t < -0.01 || t > 1.01)
        return false;

    const QPointF closest = a + ab * t;
    const QPointF delta = point - closest;
    return QPointF::dotProduct(delta, delta) <= 2.25;
}

class DisjointSet
{
public:
    void add(const PointKey& key)
    {
        if (!m_parent.contains(key))
            m_parent.insert(key, key);
    }

    PointKey find(PointKey key)
    {
        auto it = m_parent.find(key);
        if (it == m_parent.end())
            return key;
        if (it.value() != key) {
            it.value() = find(it.value());
            return it.value();
        }
        return key;
    }

    void unite(const PointKey& a, const PointKey& b)
    {
        const PointKey rootA = find(a);
        const PointKey rootB = find(b);
        if (rootA != rootB)
            m_parent.insert(rootA, rootB);
    }

private:
    QHash<PointKey, PointKey> m_parent;
};

struct SourceTag
{
    bool live = true;
    int group = 0;
};

State evaluate(const QList<cad::Object>& objects, const QHash<int, bool>& manual,
               const QHash<QString, bool>& coils)
{
    State state;

    QHash<int, QStringList> names;
    QHash<int, QVector<QPointF>> pins;
    QHash<int, const cad::Object*> byId;
    QList<const cad::Object*> wires;

    QSet<PointKey> points;

    for (const cad::Object& object : objects) {
        if (object.type <= 0 || object.id < 0)
            continue;

        byId.insert(object.id, &object);

        if (object.isLine()) {
            if (object.isWireGlyph())
                continue;
            const QPointF a(object.x() * ui::kUnit, object.y() * ui::kUnit);
            const QPointF b(object.x2() * ui::kUnit, object.y2() * ui::kUnit);
            wires.append(&object);
            points.insert(keyFor(a));
            points.insert(keyFor(b));
            continue;
        }

        if (object.isText())
            continue;

        if (object.isJunction()) {
            points.insert(keyFor(QPointF(object.x() * ui::kUnit, object.y() * ui::kUnit)));
            continue;
        }

        const QStringList pinNames = namedPins(object);
        QVector<QPointF> local = ui::terminalPositions(object);

        QTransform transform;
        if (object.rotation != 0)
            transform.rotate(object.rotation);
        const QPointF origin(object.x() * ui::kUnit, object.y() * ui::kUnit);

        QVector<QPointF> scene;
        scene.reserve(local.size());
        for (const QPointF& point : local) {
            scene.append(origin + transform.map(point));
            points.insert(keyFor(scene.last()));
        }

        names.insert(object.id, pinNames);
        pins.insert(object.id, scene);
    }

    DisjointSet sets;
    for (const PointKey& point : points)
        sets.add(point);

    // Wires connect their endpoints and any terminal, junction or other wire
    // endpoint that lies on the segment. Crossings without a junction stay
    // isolated because neither wire has a node there.
    for (const cad::Object* wire : wires) {
        const QPointF a(wire->x() * ui::kUnit, wire->y() * ui::kUnit);
        const QPointF b(wire->x2() * ui::kUnit, wire->y2() * ui::kUnit);
        const PointKey ka = keyFor(a);
        const PointKey kb = keyFor(b);
        sets.unite(ka, kb);

        for (const PointKey& point : points) {
            if (point == ka || point == kb)
                continue;
            const QPointF position(point.first, point.second);
            if (onSegment(position, a, b))
                sets.unite(point, ka);
        }
    }

    // Internal device connections.
    for (const cad::Object& object : objects) {
        if (object.type <= 0 || object.id < 0 || object.isLine() || object.isText()
            || object.isJunction()) {
            continue;
        }

        const QVector<QPointF> devicePins = pins.value(object.id);
        const int count = qMin(names.value(object.id).size(), devicePins.size());
        if (count < 1)
            continue;

        // Contacts (7000-7015) follow their relay/contactor label. Push buttons
        // and switches follow the manual state; thermal relay contacts
        // (8016-8019) are fixed because tripping is not modelled yet.
        bool operated = false;
        if (isContactor(object.type) || isContact(object.type)
            || isCombinedContactor(object.type))
            operated = coils.value(object.label, false);
        else if (isManualInput(object.type))
            operated = manual.value(object.id, false);

        if (isFuse(object.type) || isBreaker(object.type)) {
            state.closedContacts.insert(object.id, true);
            const int top = qMin(topPinCount(object, count), count);
            const int pairCount = qMin(top, count - top);
            for (int i = 0; i < pairCount; ++i)
                sets.unite(keyFor(devicePins.at(i)), keyFor(devicePins.at(top + i)));
        } else if (isCombinedContactor(object.type)) {
            state.closedContacts.insert(object.id, operated);
            if (operated) {
                QVector<int> powerPins;
                const QStringList pinNames = names.value(object.id);
                for (int i = 0; i < pinNames.size(); ++i) {
                    if (!pinNames.at(i).startsWith(QLatin1Char('A')))
                        powerPins.append(i);
                }
                const int top = powerPins.size() / 2;
                for (int i = 0; i < top; ++i)
                    sets.unite(keyFor(devicePins.at(powerPins.at(i))),
                               keyFor(devicePins.at(powerPins.at(i + top))));
            }
        } else if (isContactor(object.type) || isSwitch(object.type)) {
            // All poles are normally open and close together when operated.
            state.closedContacts.insert(object.id, operated);
            if (operated) {
                const int top = qMin(topPinCount(object, count), count);
                const int pairCount = qMin(top, count - top);
                for (int i = 0; i < pairCount; ++i)
                    sets.unite(keyFor(devicePins.at(i)), keyFor(devicePins.at(top + i)));
            }
        } else if (isContact(object.type) || isPushButton(object.type)) {
            if (count == 3) {
                // Changeover: common, normally-closed, normally-open.
                sets.unite(keyFor(devicePins.at(0)),
                           keyFor(devicePins.at(operated ? 2 : 1)));
                state.closedContacts.insert(object.id, operated);
            } else if (count >= 4) {
                // Two independent contacts on one actuator: NC (0-2) and
                // NO (1-3).
                sets.unite(keyFor(devicePins.at(operated ? 1 : 0)),
                           keyFor(devicePins.at(operated ? 3 : 2)));
                state.closedContacts.insert(object.id, operated);
            } else {
                const bool closed = isNormallyClosed(object.type) ? !operated : operated;
                state.closedContacts.insert(object.id, closed);
                if (closed)
                    sets.unite(keyFor(devicePins.at(0)), keyFor(devicePins.at(1)));
            }
        } else if (isBusbar(object.type) || isTerminalBlock(object.type)) {
            // Busbars and terminal strips only join pins that carry the same
            // terminal name (e.g. repeated L1 or 0). Distinct names such as
            // L1/L2/L3/N stay electrically separate so the block cannot short
            // phases together.
            const QStringList pinNames = names.value(object.id);
            QHash<QString, int> firstPin;
            for (int i = 0; i < count; ++i) {
                const QString name = pinNames.at(i).trimmed();
                if (name.isEmpty())
                    continue;
                if (firstPin.contains(name)) {
                    sets.unite(keyFor(devicePins.at(firstPin.value(name))),
                               keyFor(devicePins.at(i)));
                } else {
                    firstPin.insert(name, i);
                }
            }
        }
    }

    // Tag the nets that carry a supply rail.
    QHash<PointKey, QList<SourceTag>> tags;
    auto tagPin = [&](int objectId, int index, bool live, int group) {
        const QVector<QPointF> devicePins = pins.value(objectId);
        if (index < 0 || index >= devicePins.size())
            return;
        const PointKey root = sets.find(keyFor(devicePins.at(index)));
        tags[root].append(SourceTag{live, group});
    };

    for (const cad::Object& object : objects) {
        if (object.type <= 0 || object.id < 0)
            continue;
        const int type = object.type;
        const int id = object.id;
        const QStringList pinNames = names.value(id);

        if (type >= 3000 && type <= 3011) {
            const int base = id * 16;
            switch (type) {
            case 3000: tagPin(id, 0, true, base); break;
            case 3001: tagPin(id, 0, false, base); break;
            case 3003: tagPin(id, 0, true, base); tagPin(id, 1, false, base); break;
            case 3004:
                for (int i = 0; i < 3; ++i)
                    tagPin(id, i, true, base + i);
                break;
            case 3005:
                for (int i = 0; i < 3; ++i)
                    tagPin(id, i, true, base + i);
                break;
            case 3006:
                for (int i = 0; i < 3; ++i)
                    tagPin(id, i, true, base + i);
                tagPin(id, 3, false, base);
                break;
            case 3007:
                for (int i = 0; i < 3; ++i)
                    tagPin(id, i, true, base + i);
                tagPin(id, 3, false, base);
                break;
            case 3008: tagPin(id, 0, true, base); break;
            case 3009: tagPin(id, 0, false, base); break;
            case 3010: tagPin(id, 0, true, base); tagPin(id, 1, false, base); break;
            case 3011: tagPin(id, 0, true, base); tagPin(id, 1, false, base); break;
            default: break;
            }
        } else if (type == 3014 || type == 3018) {
            const int named = pinNames.size();
            const bool protective = named > 0
                && pinNames.last().compare(QStringLiteral("PE"), Qt::CaseInsensitive) == 0;
            const int branch = qMax(1, named - (protective ? 1 : 0));
            const int bottom = (branch + 1) / 2;
            tagPin(id, bottom, true, id * 16);
            tagPin(id, bottom + 1, false, id * 16);
        } else if (type == 3019) {
            bool tagged = false;
            for (int i = 0; i < pinNames.size(); ++i) {
                const QString pin = pinNames.at(i);
                if (pin == QLatin1String("+")) {
                    tagPin(id, i, true, id * 16);
                    tagged = true;
                } else if (pin == QLatin1String("-")) {
                    tagPin(id, i, false, id * 16);
                    tagged = true;
                }
            }
            if (!tagged) {
                tagPin(id, 0, true, id * 16);
                tagPin(id, 1, false, id * 16);
            }
        }
    }

    QSet<PointKey> liveRoots;
    QSet<PointKey> returnRoots;
    bool anyLive = false;
    bool anyReturn = false;
    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) {
        for (const SourceTag& tag : it.value()) {
            if (tag.live) {
                liveRoots.insert(it.key());
                anyLive = true;
            } else {
                returnRoots.insert(it.key());
                anyReturn = true;
            }
        }
    }
    state.hasSource = anyLive || anyReturn;

    auto isLive = [&](const PointKey& key) {
        return liveRoots.contains(sets.find(key));
    };
    auto isReturn = [&](const PointKey& key) {
        return returnRoots.contains(sets.find(key));
    };

    for (const cad::Object* wire : wires) {
        const PointKey key = sets.find(keyFor(QPointF(wire->x() * ui::kUnit,
                                                       wire->y() * ui::kUnit)));
        const bool live = liveRoots.contains(key);
        const bool ret = returnRoots.contains(key);
        state.liveLines.insert(wire->id, live || ret);
        if (live || ret)
            state.highlighted.insert(wire->id);
    }

    for (const cad::Object& object : objects) {
        if (!isLoad(object.type))
            continue;
        const QVector<QPointF> devicePins = pins.value(object.id);
        if (devicePins.isEmpty())
            continue;

        bool active = false;
        if (isMotor(object.type)) {
            QSet<int> groups;
            bool hasReturn = false;
            for (const QPointF& point : devicePins) {
                const PointKey root = sets.find(keyFor(point));
                for (const SourceTag& tag : tags.value(root)) {
                    if (tag.live)
                        groups.insert(tag.group);
                    else
                        hasReturn = true;
                }
            }
            active = (!groups.isEmpty() && hasReturn) || groups.size() >= 2;
        } else if (devicePins.size() >= 2) {
            const QPair<int, int> terminals =
                loadTerminals(object, names.value(object.id));
            if (terminals.first >= 0 && terminals.second < devicePins.size()) {
                const QPointF a = devicePins.at(terminals.first);
                const QPointF b = devicePins.at(terminals.second);
                active = (isLive(keyFor(a)) && isReturn(keyFor(b)))
                      || (isReturn(keyFor(a)) && isLive(keyFor(b)));
            }
        }

        state.activeLoads.insert(object.id, active);
        if (active)
            state.highlighted.insert(object.id);
    }

    for (const cad::Object& object : objects) {
        if (isContact(object.type) || isContactor(object.type)) {
            if (coils.value(object.label, false) && !object.label.isEmpty())
                state.highlighted.insert(object.id);
        } else if (isSwitch(object.type) || isPushButton(object.type)) {
            if (manual.value(object.id, false))
                state.highlighted.insert(object.id);
        }
    }

    if (!anyLive && !anyReturn) {
        state.warnings.append(QStringLiteral("No power source in the circuit."));
    } else if (!anyLive) {
        state.warnings.append(QStringLiteral("No live supply found."));
    } else if (!anyReturn) {
        state.warnings.append(QStringLiteral("No return (neutral) connection found."));
    }

    return state;
}

} // namespace

void Simulator::setObjects(const QList<cad::Object>& objects)
{
    m_objects = objects;

    QSet<int> ids;
    for (const cad::Object& object : m_objects)
        ids.insert(object.id);

    for (auto it = m_manual.begin(); it != m_manual.end();) {
        if (!ids.contains(it.key()))
            it = m_manual.erase(it);
        else
            ++it;
    }
}

State Simulator::compute()
{
    QHash<QString, bool> coils = m_coilStates;
    State state = evaluate(m_objects, m_manual, coils);
    bool settled = false;

    for (int iteration = 0; iteration < 24; ++iteration) {
        QHash<QString, bool> next;
        for (const cad::Object& object : m_objects) {
            if (!isCoilDevice(object.type))
                continue;
            if (object.label.isEmpty())
                continue;
            if (state.activeLoads.value(object.id, false))
                next.insert(object.label, true);
        }

        if (next == coils) {
            state = evaluate(m_objects, m_manual, coils);
            settled = true;
            break;
        }

        coils = next;
        state = evaluate(m_objects, m_manual, coils);
    }

    if (!settled)
        state.warnings.append(QStringLiteral("Simulation did not settle; check for oscillating contacts."));

    m_coilStates = coils;
    return state;
}

bool Simulator::isOperable(int objectId) const
{
    for (const cad::Object& object : m_objects) {
        if (object.id == objectId)
            return isManualInput(object.type);
    }
    return false;
}

bool Simulator::isMomentary(int objectId) const
{
    for (const cad::Object& object : m_objects) {
        if (object.id == objectId)
            return isMomentaryButton(object.type);
    }
    return false;
}

bool Simulator::manualState(int objectId) const
{
    return m_manual.value(objectId, false);
}

void Simulator::setManualState(int objectId, bool operated)
{
    if (isOperable(objectId))
        m_manual.insert(objectId, operated);
}

void Simulator::press(int objectId)
{
    if (isOperable(objectId))
        m_manual.insert(objectId, true);
}

void Simulator::release(int objectId)
{
    if (isMomentary(objectId))
        m_manual.insert(objectId, false);
}

void Simulator::toggle(int objectId)
{
    if (isOperable(objectId) && !isMomentary(objectId))
        m_manual.insert(objectId, !m_manual.value(objectId, false));
}

void Simulator::operate(int objectId)
{
    if (isMomentary(objectId))
        press(objectId);
    else
        toggle(objectId);
}

void Simulator::resetManual()
{
    m_manual.clear();
}

void Simulator::reset()
{
    m_manual.clear();
    m_coilStates.clear();
}

} // namespace sim
