#include "ui/sheet.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPixmap>

#include "ui/symbols.h"

namespace ui {
namespace {

const QColor kPaper(251, 251, 251);
const QColor kInk(0, 0, 0);
const QColor kMuted(110, 110, 110);

QString fileNameFor(const cad::PaletteSection& section)
{
    return QStringLiteral("palette-section-%1.png").arg(section.number, 2, 10, QLatin1Char('0'));
}

void drawScaledSymbol(QPainter* painter, const cad::PaletteItem& item, const QRectF& cell)
{
    const cad::Object object = item.makeObject();
    const QRectF bounds = localBounds(object);
    if (bounds.isEmpty())
        return;

    const double scale = qMin(cell.width() / bounds.width(), cell.height() / bounds.height());
    painter->save();
    painter->translate(cell.center());
    painter->scale(scale, scale);
    painter->translate(-bounds.center());
    paintObject(painter, object, false);
    painter->restore();
}

} // namespace

QPixmap symbolPreview(const cad::PaletteItem& item, const QSize& size)
{
    const qreal ratio = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pixmap(size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawScaledSymbol(&painter, item, QRectF(QPointF(0, 0), QSizeF(size)).adjusted(3, 3, -3, -3));
    painter.end();
    return pixmap;
}

bool renderPaletteSheet(const cad::Palette& palette,
                        const QString& outputDirectory,
                        QString* error)
{
    QDir dir(outputDirectory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(outputDirectory);
        return false;
    }

    const int columns = 6;
    const int cellWidth = 190;
    const int cellHeight = 140;
    const int headerHeight = 46;
    const int margin = 16;

    QFont titleFont;
    titleFont.setPixelSize(20);
    titleFont.setBold(true);

    QFont nameFont;
    nameFont.setPixelSize(12);

    QFont metaFont;
    metaFont.setPixelSize(10);

    for (const cad::PaletteSection& section : palette.sections) {
        const int rows = (section.items.size() + columns - 1) / columns;
        QImage image(columns * cellWidth + margin * 2,
                     headerHeight + rows * cellHeight + margin,
                     QImage::Format_RGB32);
        image.fill(kPaper);

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);

        painter.setPen(kInk);
        painter.setFont(titleFont);
        painter.drawText(QRectF(margin, margin / 2.0, image.width() - margin * 2.0, headerHeight),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("%1. %2").arg(section.number).arg(section.title));

        for (int i = 0; i < section.items.size(); ++i) {
            const cad::PaletteItem& item = section.items.at(i);
            const int column = i % columns;
            const int row = i / columns;
            const QRectF cell(margin + column * cellWidth,
                              headerHeight + row * cellHeight,
                              cellWidth,
                              cellHeight);

            painter.setPen(QPen(QColor(220, 220, 220), 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(cell.adjusted(1, 1, -1, -1));

            drawScaledSymbol(&painter, item, cell.adjusted(6, 6, -6, -40));

            painter.setPen(kInk);
            painter.setFont(nameFont);
            const QFontMetricsF metrics(nameFont);
            painter.drawText(cell.adjusted(6, 0, -6, 0).translated(0, 0)
                                 .adjusted(0, cellHeight - 34, 0, 0),
                             Qt::AlignHCenter | Qt::AlignTop,
                             metrics.elidedText(item.displayName(), Qt::ElideRight,
                                                int(cellWidth - 12)));

            painter.setPen(kMuted);
            painter.setFont(metaFont);
            painter.drawText(cell.adjusted(6, 0, -6, -6)
                                 .adjusted(0, cellHeight - 18, 0, 0),
                             Qt::AlignHCenter | Qt::AlignTop,
                             QStringLiteral("%1 · %2×%3")
                                 .arg(item.type)
                                 .arg(item.box.width())
                                 .arg(item.box.height()));
        }

        const QString path = dir.filePath(fileNameFor(section));
        if (!image.save(path)) {
            if (error)
                *error = QStringLiteral("cannot write %1").arg(path);
            return false;
        }
    }

    return true;
}

} // namespace ui
