#include "RendererRegistry.h"

namespace famp::workspace
{

bool RendererRegistry::registerRenderer(EntityKind kind,
                                        RendererCallbacks callbacks)
{
    if (!callbacks.show || !callbacks.hide || !callbacks.remove)
        return false;
    m_renderers.insert(static_cast<int>(kind), std::move(callbacks));
    return true;
}

void RendererRegistry::unregisterRenderer(EntityKind kind)
{
    m_renderers.remove(static_cast<int>(kind));
}

bool RendererRegistry::hasRenderer(EntityKind kind) const
{
    return m_renderers.contains(static_cast<int>(kind));
}

bool RendererRegistry::applyVisibility(const WorkspaceEntity& entity,
                                       QString* errorMessage) const
{
    const auto found = m_renderers.constFind(static_cast<int>(entity.kind));
    if (found == m_renderers.cend())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("该实体类型没有渲染器");
        return false;
    }
    if (entity.visible)
        return found->show(entity, errorMessage);
    found->hide(entity);
    if (errorMessage)
        errorMessage->clear();
    return true;
}

void RendererRegistry::remove(const WorkspaceEntity& entity) const
{
    const auto found = m_renderers.constFind(static_cast<int>(entity.kind));
    if (found != m_renderers.cend())
        found->remove(entity);
}

void RendererRegistry::select(const WorkspaceEntity& entity) const
{
    const auto found = m_renderers.constFind(static_cast<int>(entity.kind));
    if (found != m_renderers.cend() && found->select)
        found->select(entity);
}

void RendererRegistry::zoom(const WorkspaceEntity& entity) const
{
    const auto found = m_renderers.constFind(static_cast<int>(entity.kind));
    if (found != m_renderers.cend() && found->zoom)
        found->zoom(entity);
}

} // namespace famp::workspace
