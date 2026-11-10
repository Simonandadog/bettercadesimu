#pragma once

#include <QSize>
#include <QString>

#include "cad/palette.h"

class QPixmap;

namespace ui {

// Paint a palette entry into a pixmap of the requested size, scaled to fit.
QPixmap symbolPreview(const cad::PaletteItem& item, const QSize& size);

// Write one proof sheet per palette section into a directory, so the artwork
// can be reviewed (and compared to the reference screenshots) without
// launching the GUI.
bool renderPaletteSheet(const cad::Palette& palette,
                        const QString& outputDirectory,
                        QString* error = nullptr);

} // namespace ui
