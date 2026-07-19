#pragma once

#include "WorkspaceEntity.h"

#include <QHash>
#include <QStringList>

#include <functional>

namespace famp::workspace
{

struct EntityWriter
{
    QString description;
    QStringList extensions;
    std::function<bool(const WorkspaceEntity&, const QString&, QString*)> write;
};

class EntityWriterRegistry
{
public:
    bool registerWriter(EntityKind kind, EntityWriter writer);
    void unregisterWriter(EntityKind kind);
    bool hasWriter(EntityKind kind) const;
    const EntityWriter* writer(EntityKind kind) const;
    bool write(const WorkspaceEntity& entity,
               const QString& path,
               QString* errorMessage = nullptr) const;

private:
    QHash<int, EntityWriter> m_writers;
};

} // namespace famp::workspace
