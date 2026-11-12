#include <QApplication>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointF>
#include <QScrollBar>
#include <QUndoStack>

#include <cstdio>

#include "cad/cadfile.h"
#include "ui/caditem.h"
#include "ui/canvasview.h"
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

cad::Object makeSymbol(int x, int y)
{
    cad::Object object;
    object.id = -1;
    object.type = 5000;
    object.label = QStringLiteral("-F1");
    object.pins = QStringList{QStringLiteral("1"), QStringLiteral("2")};
    object.pinFlagCount = cad::kMinPinFlags;
    object.typeParts = QStringList{QStringLiteral("5000"), QStringLiteral("-F1"), QString(),
                                   QStringLiteral("1"), QStringLiteral("2")};
    object.fields.reserve(cad::kFieldCount);
    for (int i = 0; i < cad::kFieldCount; ++i)
        object.fields.append(cad::Field{});
    object.fields[cad::kMinPinFlags + 0].value = x;
    object.fields[cad::kMinPinFlags + 1].value = y;
    object.fields[cad::kMinPinFlags + 6].value = 6;
    object.fields[cad::kMinPinFlags + 7].value = 15;
    return object;
}

cad::Object makePart(int id, int type, const QString& label, const QStringList& pins,
                     int x, int y, int width = 6, int height = 15)
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

cad::Object makeSegment(int id, int x1, int y1, int x2, int y2)
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

void sendMouse(QWidget* target, QEvent::Type type, const QPoint& position,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(position), QPointF(target->mapToGlobal(position)),
                      button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

void testAddUndoRedo(ui::CanvasView& view)
{
    view.insertObject(makeSymbol(30, 30));
    CHECK(view.objects().size() == 1);
    CHECK(view.objects().first().id == 1);
    CHECK(view.undoStack()->canUndo());

    const QUndoCommand* add = view.undoStack()->command(0);
    CHECK(add != nullptr && add->text() == QStringLiteral("Add object"));

    view.undoStack()->undo();
    CHECK(view.objects().isEmpty());
    CHECK(view.itemById(1) == nullptr);

    view.undoStack()->redo();
    CHECK(view.objects().size() == 1);
    CHECK(view.objects().first().id == 1);
    CHECK(view.objects().first().x() == 30);
    CHECK(view.objects().first().y() == 30);
}

void testDeleteUndo(ui::CanvasView& view)
{
    CHECK(view.itemById(1) != nullptr);
    view.itemById(1)->setSelected(true);
    view.deleteSelection();
    CHECK(view.objects().isEmpty());
    CHECK(!view.undoStack()->canRedo());

    view.undoStack()->undo();
    CHECK(view.objects().size() == 1);
    CHECK(view.objects().first().id == 1);
    CHECK(view.undoStack()->canRedo());
}

void testDragUndo(ui::CanvasView& view)
{
    ui::CadItem* item = view.itemById(1);
    CHECK(item != nullptr);
    if (!item)
        return;

    view.resize(640, 480);
    view.show();
    view.zoomAll();
    QApplication::processEvents();

    // Start from an empty history so this test only checks the drag command.
    view.undoStack()->clear();

    view.scene()->clearSelection();
    item->setSelected(true);
    QApplication::processEvents();

    const QPoint start = view.mapFromScene(
        item->mapToScene(item->boundingRect().center()));
    const QPoint end = start + QPoint(24, 24);
    const int beforeX = item->object().x();
    const int beforeY = item->object().y();
    const int undoCount = view.undoStack()->count();

    sendMouse(view.viewport(), QEvent::MouseButtonPress, start, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseMove, end, Qt::NoButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, end, Qt::LeftButton,
              Qt::NoButton);

    CHECK(view.undoStack()->count() == undoCount + 1);
    ui::CadItem* moved = view.itemById(1);
    CHECK(moved != nullptr);
    if (!moved)
        return;

    const int afterX = moved->object().x();
    const int afterY = moved->object().y();
    CHECK(afterX != beforeX || afterY != beforeY);
    CHECK(afterX % 3 == 0);
    CHECK(afterY % 3 == 0);

    view.undoStack()->undo();
    const ui::CadItem* restored = view.itemById(1);
    CHECK(restored != nullptr);
    if (restored) {
        CHECK(restored->object().x() == beforeX);
        CHECK(restored->object().y() == beforeY);
    }

    view.undoStack()->redo();
    const ui::CadItem* redone = view.itemById(1);
    CHECK(redone != nullptr);
    if (redone) {
        CHECK(redone->object().x() == afterX);
        CHECK(redone->object().y() == afterY);
    }
}

void testClipboard(ui::CanvasView& view)
{
    view.clearDocument();
    view.insertObject(makeSymbol(30, 30));
    CHECK(view.objects().size() == 1);

    ui::CadItem* item = view.itemById(1);
    CHECK(item != nullptr);
    if (!item)
        return;
    item->setSelected(true);
    view.copySelection();
    CHECK(view.hasClipboard());

    view.paste();
    CHECK(view.objects().size() == 2);
    CHECK(view.itemById(2) != nullptr);
    if (const ui::CadItem* pasted = view.itemById(2)) {
        CHECK(pasted->object().x() == 33);
        CHECK(pasted->object().y() == 33);
        CHECK(pasted->isSelected());
    }

    view.undoStack()->undo();
    CHECK(view.objects().size() == 1);
    view.undoStack()->redo();
    CHECK(view.objects().size() == 2);
    CHECK(view.itemById(2) != nullptr);

    view.itemById(1)->setSelected(true);
    view.itemById(2)->setSelected(true);
    view.duplicateSelection();
    CHECK(view.objects().size() == 4);
    view.undoStack()->undo();
    CHECK(view.objects().size() == 2);
}

void testWireTools(ui::CanvasView& view)
{
    view.clearDocument();
    view.resize(640, 480);
    view.show();
    view.zoomAll();
    QApplication::processEvents();

    view.setTool(ui::CanvasView::Tool::Wire);
    CHECK(view.tool() == ui::CanvasView::Tool::Wire);

    const QPoint a = view.mapFromScene(QPointF(30 * ui::kUnit, 30 * ui::kUnit));
    const QPoint b = view.mapFromScene(QPointF(61 * ui::kUnit, 62 * ui::kUnit));
    sendMouse(view.viewport(), QEvent::MouseButtonPress, a, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonPress, b, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, b, Qt::LeftButton,
              Qt::NoButton);

    CHECK(view.objects().size() == 1);
    if (view.objects().size() == 1) {
        const cad::Object wire = view.objects().first();
        CHECK(wire.type == 4000);
        CHECK(wire.isLine());
        CHECK(!wire.isWireGlyph());
        CHECK(wire.x() == 30);
        CHECK(wire.y() == 30);
        // Both endpoints snap to the 3-unit grid.
        CHECK(wire.x2() == 60);
        CHECK(wire.y2() == 63);
    }

    view.undoStack()->undo();
    CHECK(view.objects().isEmpty());
    view.undoStack()->redo();
    CHECK(view.objects().size() == 1);

    // Escape abandons a wire that has only its start point.
    view.setTool(ui::CanvasView::Tool::Wire);
    sendMouse(view.viewport(), QEvent::MouseButtonPress, a, Qt::LeftButton,
              Qt::LeftButton);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &escape);
    sendMouse(view.viewport(), QEvent::MouseButtonPress, b, Qt::LeftButton,
              Qt::LeftButton);
    CHECK(view.objects().size() == 1);

    // A junction is a 4001 dot at the snapped click point.
    view.setTool(ui::CanvasView::Tool::Junction);
    const QPoint c = view.mapFromScene(QPointF(45 * ui::kUnit, 48 * ui::kUnit));
    sendMouse(view.viewport(), QEvent::MouseButtonPress, c, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, c, Qt::LeftButton,
              Qt::NoButton);

    CHECK(view.objects().size() == 2);
    if (view.objects().size() == 2) {
        const cad::Object junction = view.objects().at(1);
        CHECK(junction.type == 4001);
        CHECK(junction.isJunction());
        CHECK(junction.x() == 45);
        CHECK(junction.y() == 48);
    }

    view.setTool(ui::CanvasView::Tool::Select);
    CHECK(view.tool() == ui::CanvasView::Tool::Select);
    view.hide();
}

void testTextTool(ui::CanvasView& view)
{
    view.clearDocument();
    view.resize(640, 480);
    view.show();
    view.zoomAll();
    QApplication::processEvents();

    QPoint captured;
    QObject::connect(&view, &ui::CanvasView::textInsertionRequested, &view,
                     [&captured](const QPoint& position) { captured = position; });

    view.setTool(ui::CanvasView::Tool::Text);
    const QPoint at = view.mapFromScene(QPointF(33 * ui::kUnit, 36 * ui::kUnit));
    sendMouse(view.viewport(), QEvent::MouseButtonPress, at, Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, at, Qt::LeftButton,
              Qt::NoButton);

    CHECK(captured == QPoint(33, 36));

    view.insertText(QStringLiteral("hello"), captured);
    CHECK(view.objects().size() == 1);
    if (view.objects().size() == 1) {
        const cad::Object text = view.objects().first();
        CHECK(text.type == 8);
        CHECK(text.isText());
        CHECK(text.text() == QStringLiteral("hello"));
        CHECK(text.x() == 33);
        CHECK(text.y() == 36);
    }

    view.setTool(ui::CanvasView::Tool::Select);
    view.hide();
}

void testMiddleClickPan(ui::CanvasView& view)
{
    view.clearDocument();
    view.insertObject(makeSymbol(30, 30));
    view.resize(640, 480);
    view.show();
    view.zoomAll();
    for (int i = 0; i < 12; ++i)
        view.zoomIn();
    QApplication::processEvents();

    const int beforeX = view.horizontalScrollBar()->value();
    const int beforeY = view.verticalScrollBar()->value();
    const QPoint start = view.viewport()->rect().center();
    const QPoint end = start - QPoint(60, 40);

    sendMouse(view.viewport(), QEvent::MouseButtonPress, start, Qt::MiddleButton,
              Qt::MiddleButton);
    sendMouse(view.viewport(), QEvent::MouseMove, end, Qt::NoButton,
              Qt::MiddleButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, end, Qt::MiddleButton,
              Qt::NoButton);

    CHECK(view.horizontalScrollBar()->value() == beforeX + 60);
    CHECK(view.verticalScrollBar()->value() == beforeY + 40);
    view.hide();
}

void testEditObject(ui::CanvasView& view)
{
    view.clearDocument();
    view.insertObject(makeSymbol(30, 30));
    view.undoStack()->setClean();

    cad::Object object = view.objects().first();
    object.label = QStringLiteral("-K1");
    object.typeParts[1] = object.label;
    view.editObject(object);

    CHECK(!view.undoStack()->isClean());
    CHECK(view.undoStack()->canUndo());
    CHECK(view.objects().first().label == QStringLiteral("-K1"));
    CHECK(view.objects().first().typeParts.value(1) == QStringLiteral("-K1"));

    view.undoStack()->undo();
    CHECK(view.objects().first().label == QStringLiteral("-F1"));
    CHECK(view.objects().first().typeParts.value(1) == QStringLiteral("-F1"));

    view.undoStack()->redo();
    CHECK(view.objects().first().label == QStringLiteral("-K1"));

    // Editing nothing must not add another undo step.
    const int count = view.undoStack()->count();
    view.editObject(view.objects().first());
    CHECK(view.undoStack()->count() == count);
}

void testRotation(ui::CanvasView& view)
{
    view.clearDocument();
    view.insertObject(makeSymbol(30, 30));
    ui::CadItem* item = view.itemById(1);
    CHECK(item != nullptr);
    if (!item)
        return;
    item->setSelected(true);

    const QRectF upright = item->boundingRect();
    view.rotateSelection(90);

    const cad::Object rotated = view.objects().first();
    CHECK(rotated.rotation == 90);
    CHECK(view.itemById(1)->object().rotation == 90);
    CHECK(view.itemById(1)->boundingRect() != upright);

    view.undoStack()->undo();
    CHECK(view.objects().first().rotation == 0);
    view.undoStack()->redo();
    CHECK(view.objects().first().rotation == 90);

    // Four quarter turns come back to the original orientation.
    view.rotateSelection(90);
    view.rotateSelection(90);
    view.rotateSelection(90);
    CHECK(view.objects().first().rotation == 0);

    // Lines and text are not rotated.
    const int count = view.undoStack()->count();
    view.insertText(QStringLiteral("note"), QPoint(12, 12));
    for (QGraphicsItem* graphicsItem : view.scene()->items()) {
        if (auto* cadItem = qgraphicsitem_cast<ui::CadItem*>(graphicsItem))
            cadItem->setSelected(true);
    }
    view.rotateSelection(90);
    for (const cad::Object& object : view.objects()) {
        if (object.isText())
            CHECK(object.rotation == 0);
    }
    CHECK(view.undoStack()->count() > count);
}

void testSimulation(ui::CanvasView& view)
{
    view.clearDocument();

    view.appendObject(makePart(1, 3000, QString(), {QStringLiteral("L1")}, 30, 30), 1);
    view.appendObject(makePart(2, 8000, QStringLiteral("-S1"),
                               {QStringLiteral("13"), QStringLiteral("14")}, 75, 30), 2);
    view.appendObject(makePart(3, 9000, QStringLiteral("-K1"),
                               {QStringLiteral("A1"), QStringLiteral("A2")}, 75, 42), 3);
    view.appendObject(makePart(4, 3001, QString(), {QStringLiteral("N")}, 165, 54), 4);
    view.appendObject(makeSegment(10, 30, 30, 75, 30), 10);
    view.appendObject(makeSegment(11, 75, 54, 165, 54), 11);

    view.setSimulationRunning(true);
    CHECK(view.simulationRunning());
    CHECK(!view.simulationState().activeLoads.value(3, false));
    CHECK(!view.itemById(3)->isSimulated());

    view.toggleSimulationObject(2);
    CHECK(view.simulationState().activeLoads.value(3, false));
    CHECK(view.itemById(3)->isSimulated());
    CHECK(view.itemById(10)->isSimulated());

    view.resetSimulation();
    CHECK(!view.simulationState().activeLoads.value(3, false));

    view.setSimulationRunning(false);
    CHECK(!view.simulationRunning());
    CHECK(!view.itemById(3)->isSimulated());
    CHECK(!view.itemById(10)->isSimulated());
}

void testCleanState(ui::CanvasView& view)
{
    view.clearDocument();
    CHECK(view.objects().isEmpty());
    CHECK(!view.undoStack()->canUndo());

    view.insertObject(makeSymbol(12, 12));
    CHECK(!view.undoStack()->isClean());

    view.undoStack()->setClean();
    CHECK(view.undoStack()->isClean());

    view.insertObject(makeSymbol(24, 24));
    CHECK(!view.undoStack()->isClean());
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    ui::CanvasView view;
    testAddUndoRedo(view);
    testDeleteUndo(view);
    testDragUndo(view);
    testClipboard(view);
    testWireTools(view);
    testTextTool(view);
    testMiddleClickPan(view);
    testEditObject(view);
    testRotation(view);
    testSimulation(view);
    testCleanState(view);

    if (g_failures > 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("all canvas edit checks passed\n");
    return 0;
}
