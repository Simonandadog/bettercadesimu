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

bool isNormallyClosed(int type)
{
    return type == 7001 || type == 7004 || type == 7007 || type == 7010 || type == 7013
        || type == 8001 || type == 8004 || type == 8007 || type == 8010 || type == 8013
        || type == 8016 || type == 8019 || type == 8022;
}

bool isContactor(int type)
{
    return (type >= 2000 && type <= 2002) || type == 2006 || type == 2008;
}
bool isSwitch(int type) { return type >= 2003 && type <= 2007; }
bool isContact(int type) { return type >= 7000 && type <= 7015; }
bool isPushButton(int type) { return type >= 8000 && type <= 8023; }
bool isFuse(int type) { return type >= 5000 && type <= 5009; }
bool isBreaker(int type) { return type >= 6000 && type <= 6014; }
bool isCoil(int type) { return (type >= 9000 && type <= 9007) || (type >= 9014 && type <= 9016); }
bool isLamp(int type) { return type >= 9008 && type <= 9013; }
bool isMotor(int type) { return type >= 1000 && type <= 1010; }
bool isTimer(int type) { return type >= 7500 && type <= 7588; }
bool isResistor(int type) { return type >= 1050 && type <= 1052; }
bool isTerminalBlock(int type) { return type >= 10100 && type <= 10112; }
bool isBusbar(int type) { return type >= 9040 && type <= 9043; }
bool isLoad(int type)
{
    return isCoil(type) || isLamp(type) || isMotor(type) || isTimer(type) || isResistor(type);
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

        bool closed = true;
        const bool normallyClosed = isNormallyClosed(object.type);
        if (isContactor(object.type)) {
            closed = coils.value(object.label, false);
        } else if (isContact(object.type)) {
            const bool operated = coils.value(object.label, false);
            closed = normallyClosed ? !operated : operated;
        } else if (isSwitch(object.type) || isPushButton(object.type)) {
            const bool operated = manual.value(object.id, false);
            closed = normallyClosed ? !operated : operated;
        }

        state.closedContacts.insert(object.id, closed);

        if (isFuse(object.type) || isBreaker(object.type) || isContactor(object.type)
            || isContact(object.type) || isSwitch(object.type) || isPushButton(object.type)) {
            if (!closed)
                continue;
            const int top = qMin(topPinCount(object, count), count);
            const int pairCount = qMin(top, count - top);
            for (int i = 0; i < pairCount; ++i)
                sets.unite(keyFor(devicePins.at(i)), keyFor(devicePins.at(top + i)));
        } else if (isBusbar(object.type)) {
            for (int i = 1; i < count; ++i)
                sets.unite(keyFor(devicePins.at(0)), keyFor(devicePins.at(i)));
        } else if (isTerminalBlock(object.type)) {
            for (int i = 1; i < count; ++i)
                sets.unite(keyFor(devicePins.at(0)), keyFor(devicePins.at(i)));
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
            const QPointF a = devicePins.at(0);
            const QPointF b = devicePins.at(1);
            active = (isLive(keyFor(a)) && isReturn(keyFor(b)))
                  || (isReturn(keyFor(a)) && isLive(keyFor(b)));
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
            if (!isCoil(object.type) && !isTimer(object.type))
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

bool Simulator::isToggleable(int objectId) const
{
    for (const cad::Object& object : m_objects) {
        if (object.id == objectId)
            return isSwitch(object.type) || isPushButton(object.type);
    }
    return false;
}

bool Simulator::manualState(int objectId) const
{
    return m_manual.value(objectId, false);
}

void Simulator::setManualState(int objectId, bool operated)
{
    m_manual.insert(objectId, operated);
}

void Simulator::toggle(int objectId)
{
    if (isToggleable(objectId))
        m_manual.insert(objectId, !m_manual.value(objectId, false));
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
