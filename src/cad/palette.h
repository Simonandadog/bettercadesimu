#pragma once

#include <QByteArray>
#include <QList>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>

#include "cad/cadfile.h"

namespace cad {

// One entry of the symbol palette, as surveyed from the reference sheets.
struct PaletteItem {
    int type = 0;
    QString name;    // tooltip name, empty when the original has none
    QString label;   // default reference designator, e.g. "-K1"
    QStringList pins;
    QPoint origin;
    QSize box;

    QString displayName() const;
    // Build a synthetic object suitable for painting a preview.
    Object makeObject() const;
};

struct PaletteSection {
    int number = 0;
    QString title;
    QString source;
    QList<PaletteItem> items;
};

struct Palette {
    QList<PaletteSection> sections;

    static Palette parse(const QByteArray& json, QString* error = nullptr);
    static Palette load(const QString& path, QString* error = nullptr);
    // Reads the catalogue compiled into the executable.
    static Palette loadDefault();

    int itemCount() const;
    int namedCount() const;
    const PaletteItem* find(int type) const;
};

} // namespace cad
