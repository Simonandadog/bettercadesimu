#include "cad/palette.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace cad {
namespace {

QSize sizeFrom(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 2)
        return {};
    return QSize(array.at(0).toInt(), array.at(1).toInt());
}

QPoint pointFrom(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 2)
        return {};
    return QPoint(array.at(0).toInt(), array.at(1).toInt());
}

QStringList stringsFrom(const QJsonValue& value)
{
    QStringList result;
    for (const QJsonValue& entry : value.toArray())
        result.append(entry.toString());
    return result;
}

} // namespace

QString PaletteItem::displayName() const
{
    if (!name.isEmpty())
        return name;
    return QStringLiteral("Type %1").arg(type);
}

Object PaletteItem::makeObject() const
{
    Object object;
    object.id = -1;
    object.type = type;
    object.label = label;
    object.pins = pins;
    object.pinFlagCount = qMax(kMinPinFlags, pins.size());

    object.typeParts << QString::number(type) << label << QString();
    object.typeParts += pins;

    // Tail layout: x, y, x2, y2, boxLeft, boxTop, boxWidth, boxHeight, ...
    const int fieldCount = object.pinFlagCount + kTailFields;
    object.fields.reserve(fieldCount);
    for (int i = 0; i < fieldCount; ++i)
        object.fields.append(Field{});
    object.fields[object.pinFlagCount + 4].value = -box.width() / 2;
    object.fields[object.pinFlagCount + 5].value = -box.height() / 2;
    object.fields[object.pinFlagCount + 6].value = box.width();
    object.fields[object.pinFlagCount + 7].value = box.height();
    object.fields[object.pinFlagCount + 11].value = 1;
    return object;
}

Palette Palette::parse(const QByteArray& json, QString* error)
{
    Palette palette;

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("palette.json: %1 at offset %2")
                         .arg(parseError.errorString())
                         .arg(parseError.offset);
        return palette;
    }

    const QJsonObject root = document.object();
    for (const QJsonValue& sectionValue : root.value(QStringLiteral("sections")).toArray()) {
        const QJsonObject sectionObject = sectionValue.toObject();
        PaletteSection section;
        section.number = sectionObject.value(QStringLiteral("number")).toInt();
        section.title = sectionObject.value(QStringLiteral("title")).toString();
        section.source = sectionObject.value(QStringLiteral("source")).toString();

        for (const QJsonValue& itemValue : sectionObject.value(QStringLiteral("items")).toArray()) {
            const QJsonObject itemObject = itemValue.toObject();
            PaletteItem item;
            item.type = itemObject.value(QStringLiteral("type")).toInt();
            item.name = itemObject.value(QStringLiteral("name")).toString();
            item.label = itemObject.value(QStringLiteral("label")).toString();
            item.pins = stringsFrom(itemObject.value(QStringLiteral("pins")));
            item.origin = pointFrom(itemObject.value(QStringLiteral("origin")));
            item.box = sizeFrom(itemObject.value(QStringLiteral("box")));
            section.items.append(item);
        }

        palette.sections.append(section);
    }

    if (palette.sections.isEmpty() && error)
        *error = QStringLiteral("palette.json contains no sections");

    return palette;
}

Palette Palette::load(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return {};
    }
    return parse(file.readAll(), error);
}

Palette Palette::loadDefault()
{
    QString error;
    Palette palette = load(QStringLiteral(":/palette.json"), &error);
    if (!error.isEmpty()) {
        Palette empty;
        return empty;
    }
    return palette;
}

int Palette::itemCount() const
{
    int count = 0;
    for (const PaletteSection& section : sections)
        count += section.items.size();
    return count;
}

int Palette::namedCount() const
{
    int count = 0;
    for (const PaletteSection& section : sections) {
        for (const PaletteItem& item : section.items) {
            if (!item.name.isEmpty())
                ++count;
        }
    }
    return count;
}

const PaletteItem* Palette::find(int type) const
{
    for (const PaletteSection& section : sections) {
        for (const PaletteItem& item : section.items) {
            if (item.type == type)
                return &item;
        }
    }
    return nullptr;
}

} // namespace cad
