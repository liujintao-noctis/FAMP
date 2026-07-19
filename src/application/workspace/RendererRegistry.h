#pragma once

#include "WorkspaceEntity.h"

#include <QHash>

#include <functional>

namespace famp::workspace
{

struct RendererCallbacks
{
    std::function<bool(const WorkspaceEntity&, QString*)> show;
    std::function<void(const WorkspaceEntity&)> hide;
    std::function<void(const WorkspaceEntity&)> remove;
    std::function<void(const WorkspaceEntity&)> select;
    std::function<void(const WorkspaceEntity&)> zoom;
};

class RendererRegistry
{
public:
    bool registerRenderer(EntityKind kind, RendererCallbacks callbacks);
    void unregisterRenderer(EntityKind kind);
    bool hasRenderer(EntityKind kind) const;

    bool applyVisibility(const WorkspaceEntity& entity,
                         QString* errorMessage = nullptr) const;
    void remove(const WorkspaceEntity& entity) const;
    void select(const WorkspaceEntity& entity) const;
    void zoom(const WorkspaceEntity& entity) const;

private:
    QHash<int, RendererCallbacks> m_renderers;
};

} // namespace famp::workspace
