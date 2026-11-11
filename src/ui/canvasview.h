#pragma once

#include <QGraphicsView>
#include <QList>
#include <QPair>
#include <QPoint>
#include <QPointF>
#include <QSize>

#include "cad/cadfile.h"

class QGraphicsScene;
class QKeyEvent;
class QPainter;
class QUndoStack;

namespace ui {

class CadItem;

class CanvasView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit CanvasView(QWidget* parent = nullptr);
    ~CanvasView() override;

    void setDocument(const cad::Document& document);
    void setObjects(const QList<cad::Object>& objects);
    QList<cad::Object> objects() const;

    void clearDocument();

    void zoomIn();
    void zoomOut();
    void zoomAll();

    bool gridVisible() const { return m_grid; }
    void setGridVisible(bool visible);

    enum class Tool { Select, Wire, Junction, Text };
    Tool tool() const { return m_tool; }
    void setTool(Tool tool);

    // Interactive editing.
    void insertObject(cad::Object object);
    void beginInsert(const cad::Object& prototype);
    void cancelInsert();
    bool insertPending() const { return m_hasPending; }
    void insertText(const QString& text, const QPoint& position);
    void deleteSelection();
    void selectAll();
    QList<cad::Object> selectedObjects() const;
    void editObject(const cad::Object& object);
    void replaceObject(const cad::Object& object);
    void copySelection();
    void paste();
    void duplicateSelection();
    bool hasClipboard() const { return !m_clipboard.isEmpty(); }

    QUndoStack* undoStack() const { return m_undoStack; }

    // Low-level scene operations. Undo commands call these so they bypass the
    // undo stack; normal editing code should use the methods above.
    int appendObject(const cad::Object& object, int id = -1);
    void removeObjectById(int id);
    CadItem* itemById(int id) const;

    QPointF viewCenterInUnits() const;

signals:
    void cursorMoved(const QPointF& filePosition);
    void selectionChanged(const cad::Object* object);
    void documentEdited();
    void toolChanged(Tool tool);
    void objectActivated();
    void textInsertionRequested(const QPoint& position);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void emitSelection();
    void applyZoom(double factor);
    void addCadItem(const cad::Object& object);
    void updateSceneRect();
    void updateCursor();
    QPoint snapUnits(const QPointF& scenePosition) const;

    QGraphicsScene* m_scene = nullptr;
    QUndoStack* m_undoStack = nullptr;
    bool m_grid = true;
    bool m_panning = false;
    QPoint m_panStart;
    Tool m_tool = Tool::Select;

    bool m_wireActive = false;
    QPoint m_wireStart;
    QPoint m_wireEnd;

    bool m_hasPending = false;
    cad::Object m_pending;
    int m_nextId = 1;

    // Object positions when a drag begins, so the move can be undone.
    QList<QPair<int, QPointF>> m_dragStart;

    QList<cad::Object> m_clipboard;
};

// Render a document to a PNG without opening a window. Used by --render and
// by smoke tests.
bool renderToPng(const cad::Document& document,
                 const QString& outputPath,
                 QSize maximumSize = {},
                 QString* error = nullptr);

} // namespace ui
