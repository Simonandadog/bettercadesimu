#include "cad/project.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace cad {
namespace {

QString typeFieldFor(const Object& object)
{
    if (!object.typeParts.isEmpty())
        return object.typeParts.join(QLatin1Char('#'));

    QStringList parts;
    parts << QString::number(object.type);
    parts << object.label;
    parts << object.sublabel;
    parts += object.pins;
    return parts.join(QLatin1Char('#'));
}

QString fieldFor(const Field& field)
{
    if (field.text.isEmpty())
        return QString::number(field.value);
    return QStringLiteral("%1#%2").arg(field.value).arg(field.text);
}

Field fieldFrom(const QString& text)
{
    Field field;
    const int hash = text.indexOf(QLatin1Char('#'));
    if (hash < 0) {
        field.value = text.trimmed().toInt();
    } else {
        field.value = text.left(hash).trimmed().toInt();
        field.text = text.mid(hash + 1);
    }
    return field;
}

} // namespace

Project Project::fromDocument(const Document& document)
{
    Project project;
    project.objects = document.objects;
    project.rebuildNextId();
    return project;
}

void Project::rebuildNextId()
{
    int highest = 0;
    for (const Object& object : objects)
        highest = qMax(highest, object.id);
    nextId = highest + 1;
}

Object* Project::find(int id)
{
    for (Object& object : objects) {
        if (object.id == id)
            return &object;
    }
    return nullptr;
}

const Object* Project::find(int id) const
{
    for (const Object& object : objects) {
        if (object.id == id)
            return &object;
    }
    return nullptr;
}

int Project::addObject(Object object)
{
    if (object.id < 0)
        object.id = nextId++;
    else
        nextId = qMax(nextId, object.id + 1);

    if (object.pinFlagCount < kMinPinFlags)
        object.pinFlagCount = kMinPinFlags;
    if (object.fields.size() < object.pinFlagCount + kTailFields) {
        while (object.fields.size() < object.pinFlagCount + kTailFields)
            object.fields.append(Field{});
    }

    objects.append(object);
    return object.id;
}

bool Project::removeObject(int id)
{
    for (int i = 0; i < objects.size(); ++i) {
        if (objects.at(i).id == id) {
            objects.removeAt(i);
            return true;
        }
    }
    return false;
}

QByteArray Project::toJson() const
{
    QJsonArray objectArray;
    for (const Object& object : objects) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), object.id);
        entry.insert(QStringLiteral("type"), object.type);
        entry.insert(QStringLiteral("typeField"), typeFieldFor(object));
        entry.insert(QStringLiteral("pinFlagCount"), object.pinFlagCount);

        QJsonArray fields;
        for (const Field& field : object.fields)
            fields.append(fieldFor(field));
        entry.insert(QStringLiteral("fields"), fields);

        objectArray.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QString::fromLatin1(kProjectFormat));
    root.insert(QStringLiteral("version"), kProjectVersion);
    root.insert(QStringLiteral("objects"), objectArray);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

Project Project::fromJson(const QByteArray& json, QString* error)
{
    Project project;

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("project: %1 at offset %2")
                         .arg(parseError.errorString())
                         .arg(parseError.offset);
        return project;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString()
        != QString::fromLatin1(kProjectFormat)) {
        if (error)
            *error = QStringLiteral("not a bettercadesimu project file");
        return project;
    }

    const int version = root.value(QStringLiteral("version")).toInt();
    if (version > kProjectVersion) {
        if (error)
            *error = QStringLiteral("project version %1 is newer than supported (%2)")
                         .arg(version)
                         .arg(kProjectVersion);
        return project;
    }

    for (const QJsonValue& value : root.value(QStringLiteral("objects")).toArray()) {
        const QJsonObject entry = value.toObject();

        Object object;
        object.id = entry.value(QStringLiteral("id")).toInt(-1);
        object.type = entry.value(QStringLiteral("type")).toInt();
        object.typeParts = entry.value(QStringLiteral("typeField")).toString()
                               .split(QLatin1Char('#'), Qt::KeepEmptyParts);
        if (object.typeParts.isEmpty() || object.typeParts.first().isEmpty())
            object.typeParts = QStringList{QString::number(object.type)};

        object.label = object.typeParts.value(1);
        object.sublabel = object.typeParts.value(2);
        for (int part = 3; part < object.typeParts.size(); ++part)
            object.pins.append(object.typeParts.at(part));

        const int declaredFlags = entry.value(QStringLiteral("pinFlagCount"))
                                      .toInt(kMinPinFlags);
        object.pinFlagCount = qMax(kMinPinFlags, qMax(object.pins.size(), declaredFlags));

        const QJsonArray fields = entry.value(QStringLiteral("fields")).toArray();
        object.fields.reserve(qMax(fields.size(), object.pinFlagCount + kTailFields));
        for (const QJsonValue& field : fields)
            object.fields.append(fieldFrom(field.toString()));
        while (object.fields.size() < object.pinFlagCount + kTailFields)
            object.fields.append(Field{});

        project.objects.append(object);
    }

    project.rebuildNextId();
    return project;
}

Project Project::load(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return {};
    }

    QString parseError;
    Project project = fromJson(file.readAll(), &parseError);
    if (!parseError.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, parseError);
        return {};
    }

    project.path = path;
    return project;
}

bool Project::save(const QString& targetPath, QString* error)
{
    QSaveFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("cannot write %1: %2").arg(targetPath, file.errorString());
        return false;
    }

    file.write(toJson());
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("cannot write %1: %2").arg(targetPath, file.errorString());
        return false;
    }

    return true;
}

} // namespace cad
