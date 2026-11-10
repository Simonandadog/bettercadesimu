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
    return type == 1 || (type >= 4000 && type <= 4019);
}

bool Object::isJunction() const
{
    return type == 3000 || type == 3001 || type == 3004;
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
    while (i + 1 + kFieldCount <= tokens.size()) {
        bool ok = false;
        const int id = tokens.at(i).trimmed().toInt(&ok);
        if (!ok)
            break;

        Object object;
        object.id = id;
        object.definitionIndex = document.recordCount;
        object.typeParts = tokens.at(i + 1).split(QLatin1Char('#'), Qt::KeepEmptyParts);
        object.type = object.typeParts.value(0).trimmed().toInt();
        object.label = object.typeParts.value(1);
        object.sublabel = object.typeParts.value(2);
        for (int part = 3; part < object.typeParts.size(); ++part)
            object.pins.append(object.typeParts.at(part));

        for (int field = 0; field < kFieldCount; ++field)
            object.fields[field] = parseField(tokens.at(i + 2 + field));

        if (!byId.contains(id))
            order.append(id);
        byId.insert(id, object);

        ++document.recordCount;
        i += 1 + 1 + kFieldCount; // id + type field + 23 fields
    }

    if (i != tokens.size()) {
        document.warnings.append(
            QStringLiteral("%1 trailing token(s) after the last complete record")
                .arg(tokens.size() - i));
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
