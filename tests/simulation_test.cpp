#include <QApplication>
#include <QPointF>

#include <cstdio>

#include "cad/cadfile.h"
#include "sim/simulator.h"
#include "ui/symbols.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* expression, const char* file, int line)
{
    if (condition)
        return;
    ++g_failures;
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

cad::Object makeComponent(int id, int type, const QString& label,
                          const QStringList& pins, int x, int y,
                          int width = 6, int height = 15)
{
    cad::Object object;
    object.id = id;
    object.type = type;
    object.label = label;
    object.pins = pins;
    object.pinFlagCount = qMax(cad::kMinPinFlags, pins.size());
    object.typeParts << QString::number(type) << label << QString();
    object.typeParts += pins;

    for (int i = 0; i < object.pinFlagCount + cad::kTailFields; ++i)
        object.fields.append(cad::Field{});

    object.fields[object.pinFlagCount + 0].value = x;
    object.fields[object.pinFlagCount + 1].value = y;
    object.fields[object.pinFlagCount + 2].value = x;
    object.fields[object.pinFlagCount + 3].value = y;
    object.fields[object.pinFlagCount + 4].value = -width / 2;
    object.fields[object.pinFlagCount + 5].value = -height / 2;
    object.fields[object.pinFlagCount + 6].value = width;
    object.fields[object.pinFlagCount + 7].value = height;
    object.fields[object.pinFlagCount + 11].value = 1;
    return object;
}

cad::Object makeWire(int id, int x1, int y1, int x2, int y2)
{
    cad::Object object;
    object.id = id;
    object.type = 4000;
    object.pinFlagCount = cad::kMinPinFlags;
    for (int i = 0; i < cad::kFieldCount; ++i)
        object.fields.append(cad::Field{});
    object.fields[cad::kMinPinFlags + 0].value = x1;
    object.fields[cad::kMinPinFlags + 1].value = y1;
    object.fields[cad::kMinPinFlags + 2].value = x2;
    object.fields[cad::kMinPinFlags + 3].value = y2;
    return object;
}

bool closePoint(const QPointF& a, const QPointF& b)
{
    return qAbs(a.x() - b.x()) < 0.01 && qAbs(a.y() - b.y()) < 0.01;
}

void testTerminals()
{
    const QVector<QPointF> fuse = ui::terminalPositions(
        makeComponent(1, 5000, QStringLiteral("-F1"),
                      {QStringLiteral("1"), QStringLiteral("2")}, 10, 10));
    CHECK(fuse.size() == 2);
    CHECK(closePoint(fuse.value(0), QPointF(0, 0)));
    CHECK(closePoint(fuse.value(1), QPointF(0, 12.0 * ui::kUnit)));

    const QVector<QPointF> contactor = ui::terminalPositions(
        makeComponent(2, 2000, QStringLiteral("-K1"),
                      {QStringLiteral("1"), QStringLiteral("3"),
                       QStringLiteral("2"), QStringLiteral("4")}, 10, 10, 12, 15));
    CHECK(contactor.size() == 4);
    CHECK(closePoint(contactor.value(0), QPointF(0, 0)));
    CHECK(closePoint(contactor.value(1), QPointF(6.0 * ui::kUnit, 0)));
    CHECK(closePoint(contactor.value(2), QPointF(0, 12.0 * ui::kUnit)));
    CHECK(closePoint(contactor.value(3), QPointF(6.0 * ui::kUnit, 12.0 * ui::kUnit)));

    const QVector<QPointF> coil = ui::terminalPositions(
        makeComponent(3, 9000, QStringLiteral("-K1"),
                      {QStringLiteral("A1"), QStringLiteral("A2")}, 10, 10));
    CHECK(coil.size() == 2);
    CHECK(closePoint(coil.value(0), QPointF(0, 0)));
    CHECK(closePoint(coil.value(1), QPointF(0, 12.0 * ui::kUnit)));

    // Palette power feeds carry no pin names; their terminals come from the
    // pole count so that wires can still be attached to them.
    const QVector<QPointF> feed = ui::terminalPositions(
        makeComponent(4, 3000, QString(), {}, 10, 10));
    CHECK(feed.size() == 1);
    CHECK(closePoint(feed.value(0), QPointF(0, 0)));

    // Combined contactor relay: coil and power contacts in one component.
    const QVector<QPointF> relay = ui::terminalPositions(
        makeComponent(5, 1070, QStringLiteral("-K1"),
                      {QStringLiteral("A1"), QStringLiteral("A2"),
                       QStringLiteral("1"), QStringLiteral("2")}, 10, 10, 15, 15));
    CHECK(relay.size() == 4);
    CHECK(closePoint(relay.value(0), QPointF(0, 0)));
    CHECK(closePoint(relay.value(1), QPointF(0, 12.0 * ui::kUnit)));
    CHECK(closePoint(relay.value(2), QPointF(6.0 * ui::kUnit, 0)));
    CHECK(closePoint(relay.value(3), QPointF(6.0 * ui::kUnit, 12.0 * ui::kUnit)));

    const QVector<QPointF> relay3 = ui::terminalPositions(
        makeComponent(6, 1072, QStringLiteral("-K1"),
                      {QStringLiteral("1"), QStringLiteral("3"),
                       QStringLiteral("5"), QStringLiteral("2"),
                       QStringLiteral("4"), QStringLiteral("6"),
                       QStringLiteral("A1"), QStringLiteral("A2")}, 10, 10, 27, 15));
    CHECK(relay3.size() == 8);
    CHECK(closePoint(relay3.value(2), QPointF(12.0 * ui::kUnit, 0)));
    CHECK(closePoint(relay3.value(5), QPointF(12.0 * ui::kUnit, 12.0 * ui::kUnit)));
    CHECK(closePoint(relay3.value(6), QPointF(18.0 * ui::kUnit, 0)));
    CHECK(closePoint(relay3.value(7), QPointF(18.0 * ui::kUnit, 12.0 * ui::kUnit)));

    const QVector<QPointF> threePhase = ui::terminalPositions(
        makeComponent(5, 3004, QString(), {}, 10, 10));
    CHECK(threePhase.size() == 3);
    CHECK(closePoint(threePhase.value(2), QPointF(12.0 * ui::kUnit, 0)));
}

void testButtonAndContactor()
{
    // Live rail -> push button -> coil -> neutral rail.
    QList<cad::Object> objects;
    objects.append(makeComponent(1, 3000, QString(), {QStringLiteral("L1")}, 30, 30));
    objects.append(makeComponent(2, 8000, QStringLiteral("-S1"),
                                 {QStringLiteral("13"), QStringLiteral("14")}, 75, 30));
    objects.append(makeComponent(3, 9000, QStringLiteral("-K1"),
                                 {QStringLiteral("A1"), QStringLiteral("A2")}, 75, 42));
    objects.append(makeComponent(4, 3001, QString(), {QStringLiteral("N")}, 165, 54));
    objects.append(makeComponent(5, 7000, QStringLiteral("-K1"),
                                 {QStringLiteral("13"), QStringLiteral("14")}, 200, 30));

    objects.append(makeWire(10, 30, 30, 75, 30));
    objects.append(makeWire(11, 75, 54, 165, 54));

    sim::Simulator simulator;
    simulator.setObjects(objects);

    CHECK(simulator.isOperable(2));
    CHECK(simulator.isMomentary(2));
    CHECK(!simulator.isOperable(3));

    sim::State state = simulator.compute();
    CHECK(!state.activeLoads.value(3, false));
    CHECK(!state.closedContacts.value(2, true));
    CHECK(!state.closedContacts.value(5, true));

    // A momentary button only conducts while it is held.
    simulator.press(2);
    state = simulator.compute();
    CHECK(state.activeLoads.value(3, false));
    CHECK(state.closedContacts.value(2, false));
    CHECK(state.closedContacts.value(5, false));
    CHECK(state.highlighted.contains(10));
    CHECK(state.highlighted.contains(11));
    CHECK(state.highlighted.contains(3));

    simulator.release(2);
    state = simulator.compute();
    CHECK(!state.activeLoads.value(3, false));
    CHECK(!state.closedContacts.value(2, true));
    CHECK(closePoint(ui::terminalPositions(objects.at(2)).value(0), QPointF(0, 0)));
}

void testSealInLatch()
{
    // Classic start/stop station. -K1's auxiliary contact sits across the
    // start button, and the stop button is normally closed.
    QList<cad::Object> objects;
    objects.append(makeComponent(1, 3000, QString(), {QStringLiteral("L1")}, 30, 30));
    objects.append(makeComponent(2, 3001, QString(), {QStringLiteral("N")}, 210, 66));
    objects.append(makeComponent(3, 8000, QStringLiteral("-S1"),
                                 {QStringLiteral("13"), QStringLiteral("14")}, 90, 30));
    objects.append(makeComponent(4, 7000, QStringLiteral("-K1"),
                                 {QStringLiteral("13"), QStringLiteral("14")}, 120, 30));
    objects.append(makeComponent(5, 9000, QStringLiteral("-K1"),
                                 {QStringLiteral("A1"), QStringLiteral("A2")}, 150, 42, 9, 15));
    objects.append(makeComponent(6, 8001, QStringLiteral("-S2"),
                                 {QStringLiteral("11"), QStringLiteral("12")}, 180, 54));

    objects.append(makeWire(10, 30, 30, 120, 30));
    objects.append(makeWire(11, 90, 42, 150, 42));
    objects.append(makeWire(12, 150, 54, 180, 54));
    objects.append(makeWire(13, 180, 66, 210, 66));

    sim::Simulator simulator;
    simulator.setObjects(objects);

    CHECK(!simulator.compute().activeLoads.value(5, false));

    // Start is momentary; the auxiliary contact seals the contactor in.
    simulator.press(3);
    CHECK(simulator.compute().activeLoads.value(5, false));

    // Releasing the start button must not drop the contactor.
    simulator.release(3);
    CHECK(simulator.compute().activeLoads.value(5, false));

    // The normally-closed stop button breaks the seal-in path while held.
    simulator.press(6);
    CHECK(!simulator.compute().activeLoads.value(5, false));

    simulator.release(6);
    CHECK(!simulator.compute().activeLoads.value(5, false));
}

void testNormallyClosedContact()
{
    QList<cad::Object> objects;
    objects.append(makeComponent(1, 7001, QStringLiteral("-K2"),
                                 {QStringLiteral("11"), QStringLiteral("12")}, 30, 30));

    sim::Simulator simulator;
    simulator.setObjects(objects);
    sim::State state = simulator.compute();
    CHECK(state.closedContacts.value(1, false));
    CHECK(!state.hasSource);
    CHECK(!state.warnings.isEmpty());
}

void testRotationallyPlacedCoil()
{
    // A rotated coil still energises when its rotated pins are wired up.
    QList<cad::Object> objects;
    objects.append(makeComponent(1, 3000, QString(), {QStringLiteral("L1")}, 30, 30));
    cad::Object coil = makeComponent(2, 9000, QStringLiteral("-K1"),
                                     {QStringLiteral("A1"), QStringLiteral("A2")}, 30, 60);
    coil.rotation = 90;
    objects.append(coil);
    objects.append(makeComponent(3, 3001, QString(), {QStringLiteral("N")}, -6, 60));

    // A1 stays at the origin; A2 swings 12 units to the left after a 90 degree
    // turn, so the coil lies horizontally between x = 18 and x = 30.
    objects.append(makeWire(10, 30, 30, 30, 60));
    objects.append(makeWire(11, 18, 60, -6, 60));

    sim::Simulator simulator;
    simulator.setObjects(objects);
    const sim::State state = simulator.compute();
    CHECK(state.activeLoads.value(2, false));
}

void testThreePhaseMotor()
{
    // Direct-on-line starter: three phase feeds -> fuse -> contactor -> motor,
    // with a start/stop station driving the contactor coil.
    QList<cad::Object> objects;
    for (int phase = 0; phase < 3; ++phase) {
        cad::Object feed = makeComponent(1 + phase, 3000, QString(),
                                         {QStringLiteral("L%1").arg(phase + 1)},
                                         30 + phase * 6, 30);
        feed.rotation = 180;
        objects.append(feed);
    }

    objects.append(makeComponent(4, 5003, QStringLiteral("-F1"),
                                 {QStringLiteral("1"), QStringLiteral("3"),
                                  QStringLiteral("5"), QStringLiteral("2"),
                                  QStringLiteral("4"), QStringLiteral("6")},
                                 30, 42, 18, 15));
    objects.append(makeComponent(5, 2001, QStringLiteral("-KM"),
                                 {QStringLiteral("1"), QStringLiteral("3"),
                                  QStringLiteral("5"), QStringLiteral("2"),
                                  QStringLiteral("4"), QStringLiteral("6")},
                                 30, 54, 17, 15));
    objects.append(makeComponent(6, 1000, QStringLiteral("-M1"),
                                 {QStringLiteral("U1"), QStringLiteral("V1"),
                                  QStringLiteral("W1"), QStringLiteral("PE")},
                                 30, 66, 24, 24));

    objects.append(makeComponent(20, 3000, QString(), {QStringLiteral("L1")}, 100, 30));
    objects.append(makeComponent(21, 8001, QStringLiteral("-S1"),
                                 {QStringLiteral("11"), QStringLiteral("12")}, 100, 42));
    objects.append(makeComponent(22, 8000, QStringLiteral("-S2"),
                                 {QStringLiteral("13"), QStringLiteral("14")}, 120, 54));
    objects.append(makeComponent(23, 7000, QStringLiteral("-KM"),
                                 {QStringLiteral("13"), QStringLiteral("14")}, 140, 54));
    objects.append(makeComponent(24, 9000, QStringLiteral("-KM"),
                                 {QStringLiteral("A1"), QStringLiteral("A2")}, 120, 66, 9, 15));
    objects.append(makeComponent(25, 3001, QString(), {QStringLiteral("N")}, 120, 78));

    objects.append(makeWire(30, 30, 30, 30, 42));
    objects.append(makeWire(31, 36, 30, 36, 42));
    objects.append(makeWire(32, 42, 30, 42, 42));
    objects.append(makeWire(33, 100, 30, 100, 42));
    objects.append(makeWire(34, 100, 54, 120, 54));
    objects.append(makeWire(35, 120, 54, 140, 54));
    objects.append(makeWire(36, 140, 66, 120, 66));

    sim::Simulator simulator;
    simulator.setObjects(objects);

    sim::State state = simulator.compute();
    CHECK(!state.activeLoads.value(6, false));
    CHECK(!state.closedContacts.value(5, true));

    simulator.press(22);
    state = simulator.compute();
    CHECK(state.activeLoads.value(24, false));
    CHECK(state.closedContacts.value(5, false));
    CHECK(state.activeLoads.value(6, false));

    // Releasing the momentary start button keeps the contactor sealed in.
    simulator.release(22);
    state = simulator.compute();
    CHECK(state.activeLoads.value(6, false));

    simulator.press(21);
    state = simulator.compute();
    CHECK(!state.activeLoads.value(24, false));
    CHECK(!state.activeLoads.value(6, false));

    simulator.release(21);
    state = simulator.compute();
    CHECK(!state.activeLoads.value(24, false));
    CHECK(!state.activeLoads.value(6, false));
}

void testMaintainedSwitch()
{
    // A section-4 switch latches on a click; it is not momentary.
    QList<cad::Object> objects;
    objects.append(makeComponent(1, 3000, QString(), {QStringLiteral("L1")}, 30, 30));
    objects.append(makeComponent(2, 2007, QStringLiteral("-S1"),
                                 {QStringLiteral("1"), QStringLiteral("2")}, 60, 30));
    objects.append(makeComponent(3, 9000, QStringLiteral("-K1"),
                                 {QStringLiteral("A1"), QStringLiteral("A2")}, 60, 42));
    objects.append(makeComponent(4, 3001, QString(), {QStringLiteral("N")}, 60, 54));
    objects.append(makeWire(10, 30, 30, 60, 30));

    sim::Simulator simulator;
    simulator.setObjects(objects);

    CHECK(simulator.isOperable(2));
    CHECK(!simulator.isMomentary(2));
    CHECK(!simulator.compute().activeLoads.value(3, false));

    simulator.toggle(2);
    CHECK(simulator.compute().activeLoads.value(3, false));

    simulator.toggle(2);
    CHECK(!simulator.compute().activeLoads.value(3, false));
}

void testChangeoverForms()
{
    // Three-terminal momentary button: common on top, NC and NO throws below.
    {
        QList<cad::Object> objects;
        objects.append(makeComponent(1, 3000, QString(), {QStringLiteral("L1")}, 30, 30));
        objects.append(makeComponent(2, 8003, QStringLiteral("-S1"),
                                     {QStringLiteral("11"), QStringLiteral("12"),
                                      QStringLiteral("14")}, 60, 30));
        objects.append(makeComponent(3, 9008, QStringLiteral("-H1"),
                                     {QStringLiteral("X1"), QStringLiteral("X2")}, 54, 42));
        objects.append(makeComponent(4, 3001, QString(), {QStringLiteral("N")}, 54, 54));
        objects.append(makeWire(10, 30, 30, 60, 30));

        sim::Simulator simulator;
        simulator.setObjects(objects);
        CHECK(simulator.compute().activeLoads.value(3, false));

        simulator.press(2);
        CHECK(!simulator.compute().activeLoads.value(3, false));

        simulator.release(2);
        CHECK(simulator.compute().activeLoads.value(3, false));
    }

    // Four-terminal button: one NC contact (11-12) and one NO contact (13-14).
    {
        QList<cad::Object> objects;
        objects.append(makeComponent(1, 3000, QString(), {QStringLiteral("L1")}, 30, 30));
        objects.append(makeComponent(2, 8002, QStringLiteral("-S2"),
                                     {QStringLiteral("11"), QStringLiteral("13"),
                                      QStringLiteral("12"), QStringLiteral("14")}, 60, 30));
        objects.append(makeComponent(3, 9008, QStringLiteral("-H1"),
                                     {QStringLiteral("X1"), QStringLiteral("X2")}, 60, 42));
        objects.append(makeComponent(4, 9008, QStringLiteral("-H2"),
                                     {QStringLiteral("X1"), QStringLiteral("X2")}, 66, 42));
        objects.append(makeComponent(5, 3001, QString(), {QStringLiteral("N")}, 60, 54));
        objects.append(makeComponent(6, 3001, QString(), {QStringLiteral("N")}, 66, 54));
        objects.append(makeWire(10, 30, 30, 60, 30));
        objects.append(makeWire(11, 60, 30, 66, 30));

        sim::Simulator simulator;
        simulator.setObjects(objects);
        sim::State state = simulator.compute();
        CHECK(state.activeLoads.value(3, false));
        CHECK(!state.activeLoads.value(4, false));

        simulator.press(2);
        state = simulator.compute();
        CHECK(!state.activeLoads.value(3, false));
        CHECK(state.activeLoads.value(4, false));

        simulator.release(2);
        state = simulator.compute();
        CHECK(state.activeLoads.value(3, false));
        CHECK(!state.activeLoads.value(4, false));
    }
}

void testThermalContacts()
{
    // 8017 is the fixed NC trip contact (95-96) and 8016 the NO contact
    // (97-98). Neither can be clicked as a push button.
    QList<cad::Object> objects;
    objects.append(makeComponent(1, 3000, QString(), {QStringLiteral("L1")}, 30, 30));
    objects.append(makeComponent(2, 8017, QStringLiteral("-F1"),
                                 {QStringLiteral("95"), QStringLiteral("96")}, 60, 30));
    objects.append(makeComponent(3, 8016, QStringLiteral("-F1"),
                                 {QStringLiteral("97"), QStringLiteral("98")}, 72, 30));
    objects.append(makeComponent(4, 9008, QStringLiteral("-H1"),
                                 {QStringLiteral("X1"), QStringLiteral("X2")}, 60, 42));
    objects.append(makeComponent(5, 9008, QStringLiteral("-H2"),
                                 {QStringLiteral("X1"), QStringLiteral("X2")}, 72, 42));
    objects.append(makeComponent(6, 3001, QString(), {QStringLiteral("N")}, 60, 54));
    objects.append(makeComponent(7, 3001, QString(), {QStringLiteral("N")}, 72, 54));
    objects.append(makeWire(10, 30, 30, 60, 30));
    objects.append(makeWire(11, 30, 30, 72, 30));

    sim::Simulator simulator;
    simulator.setObjects(objects);

    CHECK(!simulator.isOperable(2));
    CHECK(!simulator.isOperable(3));

    sim::State state = simulator.compute();
    CHECK(state.activeLoads.value(4, false));
    CHECK(!state.activeLoads.value(5, false));

    simulator.toggle(2);
    simulator.toggle(3);
    state = simulator.compute();
    CHECK(state.activeLoads.value(4, false));
    CHECK(!state.activeLoads.value(5, false));
}

void testCoilTerminals()
{
    // A 9001 coil has an extra B1 terminal; the load must be measured across
    // A1/A2, not across the first two pins.
    QList<cad::Object> objects;
    objects.append(makeComponent(1, 3000, QString(), {QStringLiteral("L1")}, 30, 30));
    objects.append(makeComponent(2, 9001, QStringLiteral("-K1"),
                                 {QStringLiteral("A1"), QStringLiteral("B1"),
                                  QStringLiteral("A2")}, 60, 30, 8, 15));
    objects.append(makeComponent(3, 3001, QString(), {QStringLiteral("N")}, 66, 42));
    objects.append(makeWire(10, 30, 30, 60, 30));

    sim::Simulator simulator;
    simulator.setObjects(objects);
    CHECK(simulator.compute().activeLoads.value(2, false));
}

void testCombinedContactorRelay()
{
    // 1072 is a three-pole contactor with its own A1/A2 coil. Energising the
    // coil closes the power contacts.
    QList<cad::Object> objects;
    objects.append(makeComponent(1, 3000, QString(), {QStringLiteral("L1")}, 30, 30));
    objects.append(makeComponent(2, 1072, QStringLiteral("-K1"),
                                 {QStringLiteral("1"), QStringLiteral("3"),
                                  QStringLiteral("5"), QStringLiteral("2"),
                                  QStringLiteral("4"), QStringLiteral("6"),
                                  QStringLiteral("A1"), QStringLiteral("A2")},
                                 60, 30, 27, 15));
    objects.append(makeComponent(3, 9008, QStringLiteral("-H1"),
                                 {QStringLiteral("X1"), QStringLiteral("X2")}, 60, 42));
    objects.append(makeComponent(4, 3001, QString(), {QStringLiteral("N")}, 60, 54));
    objects.append(makeComponent(5, 3001, QString(), {QStringLiteral("N")}, 78, 42));
    objects.append(makeWire(10, 30, 30, 60, 30));
    objects.append(makeWire(11, 30, 30, 78, 30));

    sim::Simulator simulator;
    simulator.setObjects(objects);

    const sim::State state = simulator.compute();
    CHECK(state.activeLoads.value(2, false));
    CHECK(state.activeLoads.value(3, false));
    CHECK(state.closedContacts.value(2, false));
}

void testBusbarIsolation()
{
    // A phase busbar has one pin per conductor (L1/L2/L3/N). Different names
    // must stay separate; joining them would short the phases together.
    QList<cad::Object> objects;
    objects.append(makeComponent(1, 9040, QStringLiteral("-X1"),
                                 {QStringLiteral("L1"), QStringLiteral("L2"),
                                  QStringLiteral("L3"), QStringLiteral("N")},
                                 30, 30, 18, 15));
    objects.append(makeComponent(2, 3000, QString(), {QStringLiteral("L1")}, 30, 20));
    objects.append(makeComponent(3, 3001, QString(), {QStringLiteral("N")}, 30, 42));
    objects.append(makeComponent(4, 3001, QString(), {QStringLiteral("N")}, 36, 42));
    objects.append(makeComponent(5, 9008, QStringLiteral("-H1"),
                                 {QStringLiteral("X1"), QStringLiteral("X2")}, 30, 30));
    objects.append(makeComponent(6, 9008, QStringLiteral("-H2"),
                                 {QStringLiteral("X1"), QStringLiteral("X2")}, 36, 30));
    objects.append(makeWire(10, 30, 20, 30, 30));

    sim::Simulator simulator;
    simulator.setObjects(objects);
    sim::State state = simulator.compute();
    CHECK(state.activeLoads.value(5, false));  // L1 lamp is live
    CHECK(!state.activeLoads.value(6, false)); // L2 lamp must stay dead

    // Pins that share a name are still joined, which is how a terminal strip
    // builds a common node.
    QList<cad::Object> common;
    common.append(makeComponent(1, 9040, QStringLiteral("-X1"),
                                {QStringLiteral("L1"), QStringLiteral("L1")},
                                30, 30, 18, 15));
    common.append(makeComponent(2, 3000, QString(), {QStringLiteral("L1")}, 30, 20));
    common.append(makeComponent(3, 3001, QString(), {QStringLiteral("N")}, 36, 42));
    common.append(makeComponent(4, 9008, QStringLiteral("-H1"),
                                {QStringLiteral("X1"), QStringLiteral("X2")}, 36, 30));
    common.append(makeWire(10, 30, 20, 30, 30));

    sim::Simulator commonSimulator;
    commonSimulator.setObjects(common);
    CHECK(commonSimulator.compute().activeLoads.value(4, false));
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    testTerminals();
    testButtonAndContactor();
    testSealInLatch();
    testNormallyClosedContact();
    testRotationallyPlacedCoil();
    testThreePhaseMotor();
    testMaintainedSwitch();
    testChangeoverForms();
    testThermalContacts();
    testCoilTerminals();
    testCombinedContactorRelay();
    testBusbarIsolation();

    if (g_failures) {
        std::fprintf(stderr, "%d simulation check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("simulation_test: all checks passed\n");
    return 0;
}
