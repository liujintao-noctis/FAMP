#include "EntityWriterRegistry.h"

namespace famp::workspace
{

bool EntityWriterRegistry::registerWriter(EntityKind kind, EntityWriter writer)
{
    if (writer.description.trimmed().isEmpty()
        || writer.extensions.isEmpty() || !writer.write)
    {
        return false;
    }
    for (QString& extension : writer.extensions)
    {
        extension = extension.trimmed().toLower();
        while (extension.startsWith(QLatin1Char('.')))
            extension.remove(0, 1);
        if (extension.isEmpty())
            return false;
    }
    writer.extensions.removeDuplicates();
    m_writers.insert(static_cast<int>(kind), std::move(writer));
    return true;
}

void EntityWriterRegistry::unregisterWriter(EntityKind kind)
{
    m_writers.remove(static_cast<int>(kind));
}

bool EntityWriterRegistry::hasWriter(EntityKind kind) const
{
    return m_writers.contains(static_cast<int>(kind));
}

const EntityWriter* EntityWriterRegistry::writer(EntityKind kind) const
{
    const auto found = m_writers.constFind(static_cast<int>(kind));
    return found == m_writers.cend() ? nullptr : &found.value();
}

bool EntityWriterRegistry::write(const WorkspaceEntity& entity,
                                 const QString& path,
                                 QString* errorMessage) const
{
    const EntityWriter* selected = writer(entity.kind);
    if (!selected)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("该实体类型没有保存器");
        return false;
    }
    if (path.trimmed().isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("保存路径不能为空");
        return false;
    }
    return selected->write(entity, path, errorMessage);
}

} // namespace famp::workspace
