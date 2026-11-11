#pragma once

#include <QGraphicsView>
#include <QList>
#include <QPair>
#include <QPointF>
#include <QSize>

#include "cad/cadfile.h"

class QGraphicsScene;
class QKeyEvent;
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

    // Interactive editing.
    void insertObject(cad::Object object);
    void beginInsert(const cad::Object& prototype);
    void cancelInsert();
    bool insertPending() const { return m_hasPending; }
    void deleteSelection();
    void selectAll();

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

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void emitSelection();
    void applyZoom(double factor);
    void addCadItem(const cad::Object& object);
    void updateSceneRect();

    QGraphicsScene* m_scene = nullptr;
    QUndoStack* m_undoStack = nullptr;
    bool m_grid = true;
    bool m_panning = false;
    QPoint m_panStart;

    bool m_hasPending = false;
    cad::Object m_pending;
    int m_nextId = 1;

    // Object positions when a drag begins, so the move can be undone.
    QList<QPair<int, QPointF>> m_dragStart;
};

// Render a document to a PNG without opening a window. Used by --render and
// by smoke tests.
bool renderToPng(const cad::Document& document,
                 const QString& outputPath,
                 QSize maximumSize = {},
                 QString* error = nullptr);

} // namespace ui
