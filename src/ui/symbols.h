#pragma once

#include <QColor>
#include <QPainter>
#include <QRectF>
#include <QString>

#include "cad/cadfile.h"

namespace ui {

// CADe SIMU exports at 4 px per logical unit. The original application renders
// a 330 x 207 unit visible page at 4 px/unit, which matches the exported PNGs.
inline constexpr double kUnit = 4.0;

// Typical pin spacing between adjacent poles, in logical units.
inline constexpr double kPitch = 6.0;

// Bounds in the item's local pixel coordinates. Includes room for the label.
QRectF localBounds(const cad::Object& object);

QColor lineColor(const cad::Object& object);
QColor symbolColor(const cad::Object& object);

void paintObject(QPainter* painter, const cad::Object& object, bool selected);

} // namespace ui
