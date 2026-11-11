#include "cad/cadfile.h"

#include <QFile>
#include <QHash>

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

    if (hash >= 0)
        field.text = raw.mid(hash + 1);

    return field;
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

const Object* Document::find(int id) const
{
    for (const Object& object : objects) {
        if (object.id == id)
            return &object;
    }
    return nullptr;
}

} // namespace cad
