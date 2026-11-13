#include "cad/cadfile.h"

#include <QDate>
#include <QFile>
#include <QHash>
#include <QLocale>

namespace cad {
namespace {

Field parseField(const QString& raw)
{
    Field field;
    const int hash = raw.indexOf(QLatin1Char('#'));
    const QString number = (hash < 0 ? raw : raw.left(hash)).trimmed();

    bool ok = false;
    field.value = number.toInt(&ok);
    if (!ok)
        field.value = 0;

    if (hash >= 0) {
        field.text = raw.mid(hash + 1);
        field.hasSuffix = true;
    }

    return field;
}

QString fieldToken(const Field& field, bool forceSuffix = false)
{
    if (!forceSuffix && !field.hasSuffix && field.text.isEmpty())
        return QString::number(field.value);
    return QString::number(field.value) + QLatin1Char('#') + field.text;
}

QString typeToken(const Object& object, int pinFlags)
{
    QStringList parts;
    if (!object.typeParts.isEmpty()) {
        parts = object.typeParts;
    } else {
        parts << QString::number(object.type);
        parts << object.label;
        parts << object.sublabel;
        parts += object.pins;
    }

    // CADe SIMU always spells out eight pin slots in the type field, even when
    // the symbol has fewer pins. Keeping that shape avoids the parser shifting
    // the pin flags into the geometry fields.
    while (parts.size() < 3 + pinFlags)
        parts.append(QString());

    return parts.join(QLatin1Char('#'));
}

// CADe SIMU stores the orientation as a quarter-turn count in the tenth tail
// field. When the editor changes it, the recorded box has to follow so that
// selection stays on the symbol.
void rotateBox(QList<Field>& fields, int tailStart, int degrees)
{
    if (degrees == 0)
        return;
    if (tailStart + 7 >= fields.size())
        return;

    const int left = fields.at(tailStart + 4).value;
    const int top = fields.at(tailStart + 5).value;
    const int width = fields.at(tailStart + 6).value;
    const int height = fields.at(tailStart + 7).value;

    int newLeft = left;
    int newTop = top;
    int newWidth = width;
    int newHeight = height;

    // Match QTransform::rotate() in Qt's screen coordinate system, where y
    // points down: 90 maps (x, y) to (-y, x), 180 to (-x, -y) and 270 to
    // (y, -x).
    switch (((degrees % 360) + 360) % 360) {
    case 90:
        newLeft = -(top + height);
        newTop = left;
        newWidth = height;
        newHeight = width;
        break;
    case 180:
        newLeft = -(left + width);
        newTop = -(top + height);
        break;
    case 270:
        newLeft = top;
        newTop = -(left + width);
        newWidth = height;
        newHeight = width;
        break;
    default:
        return;
    }

    fields[tailStart + 4].value = newLeft;
    fields[tailStart + 5].value = newTop;
    fields[tailStart + 6].value = newWidth;
    fields[tailStart + 7].value = newHeight;
}

QString synthesisedMetadata(const QString& title)
{
    const QLocale english(QLocale::English, QLocale::UnitedStates);
    const QString date = english.toString(QDate::currentDate(), QStringLiteral("dd-MMM-yyyy"));
    const QString cleanTitle = (title.isEmpty() ? QStringLiteral("bettercadesimu") : title)
                                   .left(21)
                                   .leftJustified(21, QLatin1Char(' '));

    // Mirrors the metadata block written by CADe SIMU: paper size (279x210 mm)
    // followed by the title block and fourteen "1" flags.
    return QStringLiteral("$$$*1*1*1*2*4*0*1*0*0*279*210&&&*")
           + QString(11, QLatin1Char(' ')) + QLatin1Char('*')
           + QString(11, QLatin1Char(' ')) + QLatin1Char('*')
           + QString(11, QLatin1Char(' ')) + QLatin1Char('*')
           + QString(11, QLatin1Char(' ')) + QLatin1Char('*')
           + QString(25, QLatin1Char(' ')) + QLatin1Char('*')
           + QString(25, QLatin1Char(' ')) + QLatin1Char('*')
           + date.leftJustified(11, QLatin1Char(' ')) + QLatin1Char('*')
           + QStringLiteral("1          *1          *")
           + cleanTitle + QLatin1Char('*')
           + QStringLiteral("1*1*1*1*1*1*1*1*1*1*1*1*1*1***$$$&&&");
}

} // namespace

bool Object::isLine() const
{
    return type == 1 || (type >= 4000 && type <= 4019 && type != 4001);
}

bool Object::isJunction() const
{
    // Type 4001 is the junction dot; the other 40xx types are wires.
    return type == 4001;
}

bool Object::isWireGlyph() const
{
    // Palette records for drawing/wire marks are stored as lines whose second
    // endpoint is the unset (0, 0). They are glyphs at the first endpoint, not
    // wires running to the page origin.
    return isLine() && x2() == 0 && y2() == 0 && (x() != 0 || y() != 0);
}

QString typeName(int type)
{
    switch (type) {
    case 1: return QStringLiteral("Drawing line");
    case 8: return QStringLiteral("Text label");
    case 1000: return QStringLiteral("Three-phase motor");
    case 2004: return QStringLiteral("Contactor / switch");
    case 2008: return QStringLiteral("Contactor / switch");
    case 3000: return QStringLiteral("Terminal / connection");
    case 3001: return QStringLiteral("Terminal / connection");
    case 3004: return QStringLiteral("Terminal / connection");
    case 4000: return QStringLiteral("Wire");
    case 4001: return QStringLiteral("Wire");
    case 4018: return QStringLiteral("Wire");
    case 4019: return QStringLiteral("Wire");
    case 5003: return QStringLiteral("Fuse");
    case 7000: return QStringLiteral("NO contact");
    case 7001: return QStringLiteral("NC contact");
    case 8000: return QStringLiteral("NO push button");
    case 8001: return QStringLiteral("NC push button");
    case 9000: return QStringLiteral("Coil");
    case 9008: return QStringLiteral("Lamp");
    default:
        if (type >= 3000 && type < 3100)
            return QStringLiteral("Terminal / connection");
        return QStringLiteral("Unknown type %1").arg(type);
    }
}

Document Document::load(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not open %1: %2").arg(path, file.errorString());
        return {};
    }

    Document document = parse(file.readAll(), error);
    document.sourcePath = path;
    return document;
}

Document Document::parse(const QByteArray& data, QString* error)
{
    Document document;

    QString text = QString::fromLatin1(data);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    const int metadataStart = text.indexOf(QStringLiteral("$$$"));
    QString body = metadataStart >= 0 ? text.left(metadataStart) : text;
    if (metadataStart >= 0)
        document.metadata = text.mid(metadataStart);

    if (!body.startsWith(QStringLiteral("CADe_SIMU"))) {
        if (error)
            *error = QStringLiteral("Not a CADe SIMU document (missing CADe_SIMU header)");
        return document;
    }

    const QStringList tokens = body.split(QLatin1Char('*'), Qt::KeepEmptyParts);
    document.cadHeader = tokens.value(0);

    QHash<int, Object> byId;
    QList<int> order;

    int i = 1; // tokens[0] is the header
    while (i + 1 < tokens.size()) {
        bool ok = false;
        const int id = tokens.at(i).trimmed().toInt(&ok);
        if (!ok)
            break;

        QStringList typeParts = tokens.at(i + 1).split(QLatin1Char('#'), Qt::KeepEmptyParts);
        const int type = typeParts.value(0).trimmed().toInt(&ok);
        if (!ok)
            break;

        int pinCount = typeParts.size() - 3;
        if (pinCount < 0)
            pinCount = 0;
        const int pinFlags = qMax(kMinPinFlags, pinCount);
        const int fieldCount = pinFlags + kTailFields;
        const int recordTokens = 2 + fieldCount;

        if (i + recordTokens > tokens.size()) {
            document.warnings.append(
                QStringLiteral("record %1 is truncated (%2 token(s) missing)")
                    .arg(id)
                    .arg(i + recordTokens - tokens.size()));
            i = tokens.size();
            break;
        }

        Object object;
        object.id = id;
        object.definitionIndex = document.recordCount;
        object.type = type;
        object.typeParts = typeParts;
        object.label = object.typeParts.value(1);
        object.sublabel = object.typeParts.value(2);
        for (int part = 3; part < object.typeParts.size(); ++part)
            object.pins.append(object.typeParts.at(part));

        object.pinFlagCount = pinFlags;
        object.fields.reserve(fieldCount);
        for (int field = 0; field < fieldCount; ++field)
            object.fields.append(parseField(tokens.at(i + 2 + field)));

        // The tenth tail field is the clockwise orientation as a quarter-turn
        // count. Values outside 0..3 are preserved untouched (some records use
        // the field for other purposes).
        if (object.isComponent()) {
            const int orientation = object.fields.at(pinFlags + 9).value;
            if (orientation >= 0 && orientation <= 3)
                object.rotation = orientation * 90;
        }

        if (!byId.contains(id))
            order.append(id);
        byId.insert(id, object);

        ++document.recordCount;
        i += recordTokens;
    }

    if (i < tokens.size()) {
        bool whitespaceOnly = true;
        for (int t = i; t < tokens.size(); ++t) {
            if (!tokens.at(t).trimmed().isEmpty()) {
                whitespaceOnly = false;
                break;
            }
        }
        if (!whitespaceOnly) {
            document.warnings.append(
                QStringLiteral("%1 trailing token(s) after the last complete record")
                    .arg(tokens.size() - i));
        }
    }

    for (int id : order)
        document.objects.append(byId.value(id));

    return document;
}

QByteArray Document::toCad(const QString& title) const
{
    QString text = cadHeader.isEmpty() ? QStringLiteral("CADe_SIMU") : cadHeader;

    for (const Object& source : objects) {
        Object object = source;
        int pinFlags = qMax(kMinPinFlags, object.pinFlagCount);
        pinFlags = qMax(pinFlags, object.pins.size());
        const int tailStart = pinFlags;

        while (object.fields.size() < tailStart + kTailFields)
            object.fields.append(Field{});

        if (object.isComponent()) {
            Field& orientation = object.fields[tailStart + 9];
            const int raw = orientation.value;
            const bool encoded = raw >= 0 && raw <= 3;
            const int rawRotation = encoded ? raw * 90 : 0;

            if (encoded || object.rotation != 0) {
                if (object.rotation != rawRotation)
                    rotateBox(object.fields, tailStart, object.rotation - rawRotation);
                orientation.value = object.rotation / 90;
                orientation.hasSuffix = false;
                orientation.text.clear();
            }
        }

        text += QLatin1Char('*') + QString::number(object.id);
        text += QLatin1Char('*') + typeToken(object, pinFlags);

        for (int field = 0; field < pinFlags; ++field)
            text += QLatin1Char('*') + fieldToken(object.fields.at(field));

        for (int field = 0; field < kTailFields; ++field) {
            const Field& value = object.fields.at(tailStart + field);
            text += QLatin1Char('*')
                    + fieldToken(value, field == kTailFields - 1);
        }
    }

    text += metadata.isEmpty() ? synthesisedMetadata(title) : metadata;
    return text.toLatin1();
}

const Object* Document::find(int id) const
{
    for (const Object& object : objects) {
        if (object.id == id)
            return &object;
    }
    return nullptr;
}

} // namespace cad
