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

    CHECK(simulator.isToggleable(2));
    CHECK(!simulator.isToggleable(3));

    sim::State state = simulator.compute();
    CHECK(!state.activeLoads.value(3, false));
    CHECK(!state.closedContacts.value(2, true));
    CHECK(!state.closedContacts.value(5, true));

    simulator.toggle(2);
    state = simulator.compute();
    CHECK(state.activeLoads.value(3, false));
    CHECK(state.closedContacts.value(2, false));
    CHECK(state.closedContacts.value(5, false));
    CHECK(state.highlighted.contains(10));
    CHECK(state.highlighted.contains(11));
    CHECK(state.highlighted.contains(3));
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

    simulator.toggle(3);
    CHECK(simulator.compute().activeLoads.value(5, false));

    // Releasing the start button must not drop the contactor.
    simulator.toggle(3);
    CHECK(simulator.compute().activeLoads.value(5, false));

    // The normally-closed stop button breaks the seal-in path.
    simulator.toggle(6);
    CHECK(!simulator.compute().activeLoads.value(5, false));

    simulator.toggle(6);
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

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    testTerminals();
    testButtonAndContactor();
    testSealInLatch();
    testNormallyClosedContact();
    testRotationallyPlacedCoil();

    if (g_failures) {
        std::fprintf(stderr, "%d simulation check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("simulation_test: all checks passed\n");
    return 0;
}
