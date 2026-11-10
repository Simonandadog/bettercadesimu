#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <array>

namespace cad {

// A CADe SIMU record carries a fixed 23 numeric fields after its type field.
inline constexpr int kFieldCount = 23;

struct Field {
    int value = 0;
    QString text; // whatever follows the first '#' in the field, if anything
};

struct Object {
    int id = -1;
    int type = 0;
    QString label;    // e.g. "-K1" for components
    QString sublabel; // second '#' field; meaning not fully known yet
    QStringList typeParts; // raw type field split on '#'
    QStringList pins;      // pin names, when present
    std::array<Field, kFieldCount> fields{};
    int definitionIndex = -1; // which record last defined this object

    int x() const { return fields[8].value; }
    int y() const { return fields[9].value; }
    int x2() const { return fields[10].value; }
    int y2() const { return fields[11].value; }

    QString text() const { return fields[22].text; }

    bool isLine() const;
    bool isText() const { return type == 8; }
    bool isJunction() const;
    bool isComponent() const { return !isLine() && !isText() && !isJunction(); }
};

struct Document {
    QList<Object> objects;
    QString metadata;   // everything from the first "$$$" onwards
    QString sourcePath;
    int recordCount = 0;
    QStringList warnings;

    static Document load(const QString& path, QString* error = nullptr);
    static Document parse(const QByteArray& data, QString* error = nullptr);

    const Object* find(int id) const;
};

// Human-readable name for a type code. The long tail is filled in as we map
// the palette; unknown codes still render as a labelled placeholder.
QString typeName(int type);

} // namespace cad
