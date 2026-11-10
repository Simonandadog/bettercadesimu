#pragma once

#include <QGraphicsView>
#include <QList>
#include <QPointF>
#include <QSize>

#include "cad/cadfile.h"

class QGraphicsScene;
class QKeyEvent;

namespace ui {

class CanvasView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit CanvasView(QWidget* parent = nullptr);

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

    QGraphicsScene* m_scene = nullptr;
    bool m_grid = true;
    bool m_panning = false;
    QPoint m_panStart;

    bool m_hasPending = false;
    cad::Object m_pending;
    int m_nextId = 1;
};

// Render a document to a PNG without opening a window. Used by --render and
// by smoke tests.
bool renderToPng(const cad::Document& document,
                 const QString& outputPath,
                 QSize maximumSize = {},
                 QString* error = nullptr);

} // namespace ui
