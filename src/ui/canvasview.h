#pragma once

#include <QGraphicsView>
#include <QPointF>
#include <QSize>

#include "cad/cadfile.h"

class QGraphicsScene;

namespace ui {

class CanvasView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit CanvasView(QWidget* parent = nullptr);

    void setDocument(const cad::Document& document);
    void clearDocument();

    void zoomIn();
    void zoomOut();
    void zoomAll();

    bool gridVisible() const { return m_grid; }
    void setGridVisible(bool visible);

signals:
    void cursorMoved(const QPointF& filePosition);
    void selectionChanged(const cad::Object* object);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void emitSelection();
    void applyZoom(double factor);

    QGraphicsScene* m_scene = nullptr;
    bool m_grid = true;
    bool m_panning = false;
    QPoint m_panStart;
};

// Render a document to a PNG without opening a window. Used by --render and
// by smoke tests.
bool renderToPng(const cad::Document& document,
                 const QString& outputPath,
                 QSize maximumSize = {},
                 QString* error = nullptr);

} // namespace ui
