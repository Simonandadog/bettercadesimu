#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "cad/cadfile.h"

namespace cad {

// Native, JSON-based schematic project. Unlike Document (a read-only replay of
// the CADe SIMU log), a Project is the mutable model the editor works on.
inline constexpr int kProjectVersion = 1;
inline constexpr const char* kProjectFormat = "bettercadesimu-project";

struct Project {
    QList<Object> objects;
    QString path;
    bool modified = false;
    int nextId = 1;

    // Import a parsed .cad document, keeping object IDs unchanged.
    static Project fromDocument(const Document& document);
    static Project fromJson(const QByteArray& json, QString* error = nullptr);
    static Project load(const QString& path, QString* error = nullptr);

    QByteArray toJson() const;
    bool save(const QString& path, QString* error = nullptr);

    Object* find(int id);
    const Object* find(int id) const;

    // Adds an object, assigning the next free ID when it has none.
    int addObject(Object object);
    bool removeObject(int id);

    void rebuildNextId();
};

} // namespace cad
