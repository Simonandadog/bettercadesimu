#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <array>

namespace cad {

// A record is: id, type field, max(8, pinCount) pin-flag fields, then a
// fixed 15-field tail (geometry, bounding box, inline text, ...).
// kFieldCount is therefore only the minimum payload; symbols with more than
// eight pins carry extra pin-flag fields (see Object::pinFlagCount).
inline constexpr int kMinPinFlags = 8;
inline constexpr int kTailFields = 15;
inline constexpr int kFieldCount = kMinPinFlags + kTailFields; // 23

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
    QList<Field> fields;   // raw fields in file order
    int pinFlagCount = kMinPinFlags;
    int definitionIndex = -1; // which record last defined this object

    // Tail accessors; index 0 is the first field after the pin flags.
    const Field& tail(int index) const { return fields.at(pinFlagCount + index); }

    int x() const { return tail(0).value; }
    int y() const { return tail(1).value; }
    int x2() const { return tail(2).value; }
    int y2() const { return tail(3).value; }
    int boxLeft() const { return tail(4).value; }
    int boxTop() const { return tail(5).value; }
    int boxWidth() const { return tail(6).value; }
    int boxHeight() const { return tail(7).value; }

    QString text() const { return tail(14).text; }

    bool isLine() const;
    bool isWireGlyph() const;
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
