#include <QApplication>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPointF>
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
    testCleanState(view);

    if (g_failures > 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("all canvas edit checks passed\n");
    return 0;
}
