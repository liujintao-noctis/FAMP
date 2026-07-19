#include "WorkspaceStore.h"

#include <QFileInfo>

#include <algorithm>

namespace famp::workspace
{
namespace
{

bool fail(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
    return false;
}

void succeed(QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
}

} // namespace

WorkspaceStore::WorkspaceStore(QObject* parent)
    : QObject(parent)
{
    clear();
}

EntityId WorkspaceStore::rootId() const
{
    return m_rootId;
}

const WorkspaceEntity* WorkspaceStore::entity(const EntityId& id) const
{
    const auto found = m_entities.constFind(id);
    return found == m_entities.cend() ? nullptr : found.value().data();
}

bool WorkspaceStore::contains(const EntityId& id) const
{
    return m_entities.contains(id);
}

QVector<EntityId> WorkspaceStore::children(const EntityId& parentId) const
{
    return m_children.value(parentId);
}

int WorkspaceStore::rowOf(const EntityId& id) const
{
    const WorkspaceEntity* current = entity(id);
    if (!current)
        return -1;
    if (id == m_rootId)
        return 0;
    return m_children.value(current->parentId).indexOf(id);
}

int WorkspaceStore::size() const
{
    return m_entities.size();
}

EntityId WorkspaceStore::addEntity(WorkspaceEntity candidate,
                                   const EntityId& requestedParentId,
                                   int row,
                                   QString* errorMessage)
{
    const EntityId parentId = requestedParentId.isNull()
        ? m_rootId : requestedParentId;
    if (candidate.id.isNull())
        candidate.id = QUuid::createUuid();
    candidate.parentId = parentId;
    candidate.name = uniqueSiblingName(parentId, candidate.name);
    if (!validateNewEntity(candidate, parentId, errorMessage))
        return {};

    QVector<EntityId>& siblings = m_children[parentId];
    if (row < 0 || row > siblings.size())
        row = siblings.size();

    emit entityAboutToBeInserted(parentId, row);
    m_entities.insert(candidate.id, EntityPointer(new WorkspaceEntity(candidate)));
    siblings.insert(row, candidate.id);
    m_children.insert(candidate.id, {});
    emit entityInserted(candidate.id);
    succeed(errorMessage);
    return candidate.id;
}

EntityId WorkspaceStore::addDerivedEntity(WorkspaceEntity candidate,
                                          const EntityId& sourceId,
                                          QString* errorMessage)
{
    const WorkspaceEntity* source = entity(sourceId);
    if (!source)
    {
        fail(errorMessage, QStringLiteral("来源实体不存在"));
        return {};
    }
    if (!candidate.provenance.has_value())
    {
        fail(errorMessage, QStringLiteral("派生成果必须记录来源"));
        return {};
    }
    if (!candidate.provenance->sourceIds.contains(sourceId))
        candidate.provenance->sourceIds.prepend(sourceId);

    const EntityId sourceParentId = source->parentId;
    int insertionRow = -1;
    if (candidate.kind == EntityKind::PointCloud)
    {
        const int sourceRow = rowOf(sourceId);
        if (sourceId != m_rootId && sourceRow < 0)
        {
            fail(errorMessage, QStringLiteral("来源实体不在内容列表中"));
            return {};
        }
        insertionRow = sourceRow + 1;
    }

    return addEntity(std::move(candidate),
                     sourceParentId,
                     insertionRow,
                     errorMessage);
}

bool WorkspaceStore::removeEntity(const EntityId& id, QString* errorMessage)
{
    if (id.isNull() || id == m_rootId)
        return fail(errorMessage, QStringLiteral("不能删除项目根节点"));
    const WorkspaceEntity* current = entity(id);
    if (!current)
        return fail(errorMessage, QStringLiteral("实体不存在"));
    if (subtreeContainsLockedEntity(id))
        return fail(errorMessage, QStringLiteral("实体或其子项已锁定"));

    const EntityId parentId = current->parentId;
    const int row = rowOf(id);
    QVector<EntityId> removedIds;
    collectSubtree(id, removedIds);
    emit entityAboutToBeRemoved(parentId, row);
    m_children[parentId].removeAt(row);
    eraseSubtree(id);
    emit entityRemoved(id, removedIds);
    notifyAncestors(parentId, {Qt::CheckStateRole});
    succeed(errorMessage);
    return true;
}

bool WorkspaceStore::removeEntities(const QVector<EntityId>& ids,
                                    QString* errorMessage)
{
    QVector<EntityId> normalized;
    for (const EntityId& id : ids)
    {
        if (!contains(id) || id == m_rootId)
            continue;
        bool covered = false;
        for (const EntityId& candidate : ids)
        {
            if (candidate != id && contains(candidate) && isAncestor(candidate, id))
            {
                covered = true;
                break;
            }
        }
        if (!covered && !normalized.contains(id))
            normalized.append(id);
    }
    if (normalized.isEmpty())
        return fail(errorMessage, QStringLiteral("没有可删除的实体"));
    for (const EntityId& id : normalized)
    {
        if (subtreeContainsLockedEntity(id))
            return fail(errorMessage, QStringLiteral("所选实体中包含锁定项"));
    }

    std::sort(normalized.begin(), normalized.end(), [this](const EntityId& left,
                                                            const EntityId& right) {
        const WorkspaceEntity* leftEntity = entity(left);
        const WorkspaceEntity* rightEntity = entity(right);
        if (leftEntity && rightEntity && leftEntity->parentId == rightEntity->parentId)
            return rowOf(left) > rowOf(right);
        return left.toString() < right.toString();
    });
    for (const EntityId& id : normalized)
    {
        if (!removeEntity(id, errorMessage))
            return false;
    }
    succeed(errorMessage);
    return true;
}

bool WorkspaceStore::moveEntity(const EntityId& id,
                                const EntityId& requestedParentId,
                                int row,
                                QString* errorMessage)
{
    return moveEntities({id}, requestedParentId, row, errorMessage);
}

bool WorkspaceStore::moveEntities(const QVector<EntityId>& ids,
                                  const EntityId& requestedParentId,
                                  int row,
                                  QString* errorMessage)
{
    const EntityId parentId = requestedParentId.isNull()
        ? m_rootId : requestedParentId;
    const WorkspaceEntity* parent = entity(parentId);
    if (!parent || !entityKindCanHaveChildren(parent->kind))
        return fail(errorMessage, QStringLiteral("目标不能包含子项"));
    if (parent->locked)
        return fail(errorMessage, QStringLiteral("目标组已锁定"));

    QVector<EntityId> normalized;
    for (const EntityId& id : ids)
    {
        if (id.isNull() || id == m_rootId || !contains(id))
            return fail(errorMessage, QStringLiteral("不能移动该实体"));
        if (normalized.contains(id))
            continue;
        bool coveredBySelectedAncestor = false;
        for (const EntityId& candidate : ids)
        {
            if (candidate != id && contains(candidate)
                && isAncestor(candidate, id))
            {
                coveredBySelectedAncestor = true;
                break;
            }
        }
        if (!coveredBySelectedAncestor)
            normalized.append(id);
    }
    if (normalized.isEmpty())
        return fail(errorMessage, QStringLiteral("没有可移动的实体"));
    for (const EntityId& id : normalized)
    {
        const WorkspaceEntity* current = entity(id);
        if (!current || current->locked)
            return fail(errorMessage, QStringLiteral("所选实体中包含锁定项"));
        if (id == parentId || isAncestor(id, parentId))
            return fail(errorMessage, QStringLiteral("不能把实体移动到自身后代"));
    }

    emit storeAboutToReset();
    for (const EntityId& id : normalized)
    {
        WorkspaceEntity* current = const_cast<WorkspaceEntity*>(entity(id));
        m_children[current->parentId].removeOne(id);
    }
    QVector<EntityId>& newSiblings = m_children[parentId];
    if (row < 0 || row > newSiblings.size())
        row = newSiblings.size();
    int insertionRow = row;
    for (const EntityId& id : normalized)
    {
        WorkspaceEntity* current = const_cast<WorkspaceEntity*>(entity(id));
        current->parentId = parentId;
        current->name = uniqueSiblingName(parentId, current->name);
        current->dirty = true;
        newSiblings.insert(insertionRow++, id);
    }
    emit storeReset();
    succeed(errorMessage);
    return true;
}

bool WorkspaceStore::setName(const EntityId& id,
                             const QString& requestedName,
                             QString* errorMessage)
{
    WorkspaceEntity* current = const_cast<WorkspaceEntity*>(entity(id));
    if (!current)
        return fail(errorMessage, QStringLiteral("实体不存在"));
    if (current->locked)
        return fail(errorMessage, QStringLiteral("实体已锁定"));
    const QString trimmed = requestedName.trimmed();
    if (trimmed.isEmpty())
        return fail(errorMessage, QStringLiteral("名称不能为空"));
    if (trimmed.size() > 255)
        return fail(errorMessage, QStringLiteral("名称不能超过 255 个字符"));
    const QString name = uniqueSiblingName(current->parentId, trimmed, id);
    if (current->name == name)
    {
        succeed(errorMessage);
        return true;
    }
    current->name = name;
    current->dirty = true;
    emit entityChanged(id, {Qt::DisplayRole, Qt::EditRole, Qt::FontRole});
    succeed(errorMessage);
    return true;
}

bool WorkspaceStore::setVisible(const EntityId& id,
                                bool visible,
                                bool recursive,
                                QString* errorMessage)
{
    WorkspaceEntity* current = const_cast<WorkspaceEntity*>(entity(id));
    if (!current)
        return fail(errorMessage, QStringLiteral("实体不存在"));
    QVector<EntityId> affected{id};
    if (recursive && entityKindCanHaveChildren(current->kind))
        affected += descendants(id);
    for (const EntityId& affectedId : affected)
    {
        WorkspaceEntity* affectedEntity = const_cast<WorkspaceEntity*>(entity(affectedId));
        if (!affectedEntity)
            continue;
        affectedEntity->visible = visible;
        affectedEntity->dirty = true;
        emit entityChanged(affectedId,
                           {Qt::CheckStateRole, Qt::ForegroundRole, Qt::FontRole});
    }
    notifyAncestors(current->parentId, {Qt::CheckStateRole});
    succeed(errorMessage);
    return true;
}

bool WorkspaceStore::setLocked(const EntityId& id,
                               bool locked,
                               QString* errorMessage)
{
    WorkspaceEntity* current = const_cast<WorkspaceEntity*>(entity(id));
    if (!current)
        return fail(errorMessage, QStringLiteral("实体不存在"));
    if (id == m_rootId && locked)
        return fail(errorMessage, QStringLiteral("项目根节点不使用实体锁"));
    current->locked = locked;
    current->dirty = true;
    emit entityChanged(id, {Qt::FontRole, Qt::ToolTipRole});
    succeed(errorMessage);
    return true;
}

bool WorkspaceStore::setDirty(const EntityId& id, bool dirty)
{
    WorkspaceEntity* current = const_cast<WorkspaceEntity*>(entity(id));
    if (!current)
        return false;
    current->dirty = dirty;
    emit entityChanged(id, {Qt::FontRole});
    return true;
}

bool WorkspaceStore::setAssetPath(const EntityId& id,
                                  const std::optional<QString>& path)
{
    WorkspaceEntity* current = const_cast<WorkspaceEntity*>(entity(id));
    if (!current)
        return false;
    if (path.has_value() && path->trimmed().isEmpty())
        current->assetPath.reset();
    else
        current->assetPath = path;
    current->dirty = true;
    emit entityChanged(id, {Qt::ToolTipRole, Qt::FontRole});
    return true;
}

bool WorkspaceStore::replaceEntity(const WorkspaceEntity& replacement,
                                   QString* errorMessage)
{
    WorkspaceEntity* current = const_cast<WorkspaceEntity*>(entity(replacement.id));
    if (!current)
        return fail(errorMessage, QStringLiteral("实体不存在"));
    if (replacement.id != current->id
        || replacement.parentId != current->parentId
        || replacement.kind != current->kind)
    {
        return fail(errorMessage, QStringLiteral("替换不能改变 ID、父项或类型"));
    }
    WorkspaceEntity candidate = replacement;
    candidate.name = candidate.name.trimmed();
    if (candidate.name.isEmpty())
        return fail(errorMessage, QStringLiteral("名称不能为空"));
    if (candidate.name.size() > 255)
        return fail(errorMessage, QStringLiteral("名称不能超过 255 个字符"));
    if (candidate.assetPath.has_value())
    {
        if (candidate.assetPath->contains(QChar::Null))
            return fail(errorMessage, QStringLiteral("资产路径无效"));
        if (candidate.assetPath->trimmed().isEmpty())
            candidate.assetPath.reset();
    }
    if (candidate.provenance.has_value()
        && !candidate.provenance->isValid(errorMessage))
    {
        return false;
    }
    candidate.name = uniqueSiblingName(candidate.parentId,
                                       candidate.name,
                                       candidate.id);
    *current = candidate;
    emit entityChanged(candidate.id, {});
    succeed(errorMessage);
    return true;
}

QString WorkspaceStore::uniqueSiblingName(const EntityId& parentId,
                                          const QString& requested,
                                          const EntityId& ignoredId) const
{
    const QString base = requested.trimmed().isEmpty()
        ? QStringLiteral("未命名实体") : requested.trimmed();
    auto exists = [&](const QString& candidate) {
        for (const EntityId& id : m_children.value(parentId))
        {
            if (id == ignoredId)
                continue;
            const WorkspaceEntity* sibling = entity(id);
            if (sibling && sibling->name.compare(candidate, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    };
    if (!exists(base))
        return base;
    for (int suffix = 1; suffix < 1000000; ++suffix)
    {
        const QString candidate = QStringLiteral("%1.%2").arg(base).arg(suffix);
        if (!exists(candidate))
            return candidate;
    }
    return base + QStringLiteral(".") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QVector<EntityId> WorkspaceStore::descendants(const EntityId& id) const
{
    QVector<EntityId> result;
    for (const EntityId& child : m_children.value(id))
    {
        result.append(child);
        result += descendants(child);
    }
    return result;
}

QVector<EntityId> WorkspaceStore::allEntityIds() const
{
    QVector<EntityId> result;
    result.reserve(m_entities.size());
    result.append(m_rootId);
    result += descendants(m_rootId);
    return result;
}

void WorkspaceStore::sortChildren(const EntityId& parentId, Qt::SortOrder order)
{
    if (!m_children.contains(parentId))
        return;
    emit storeAboutToReset();
    QVector<EntityId>& ids = m_children[parentId];
    std::sort(ids.begin(), ids.end(), [this, order](const EntityId& left,
                                                    const EntityId& right) {
        const WorkspaceEntity* leftEntity = entity(left);
        const WorkspaceEntity* rightEntity = entity(right);
        const int comparison = QString::localeAwareCompare(
            leftEntity ? leftEntity->name : QString(),
            rightEntity ? rightEntity->name : QString());
        return order == Qt::AscendingOrder ? comparison < 0 : comparison > 0;
    });
    emit storeReset();
}

void WorkspaceStore::clear(const QString& requestedProjectName)
{
    emit storeAboutToReset();
    m_entities.clear();
    m_children.clear();
    WorkspaceEntity root = makeEntity(EntityKind::ProjectRoot,
                                      requestedProjectName.trimmed().isEmpty()
                                          ? QStringLiteral("未命名项目")
                                          : requestedProjectName);
    root.parentId = {};
    root.visible = true;
    root.dirty = false;
    m_rootId = root.id;
    m_entities.insert(root.id, EntityPointer(new WorkspaceEntity(root)));
    m_children.insert(root.id, {});
    emit storeReset();
}

bool WorkspaceStore::validateNewEntity(const WorkspaceEntity& candidate,
                                       const EntityId& parentId,
                                       QString* errorMessage) const
{
    if (candidate.id.isNull())
        return fail(errorMessage, QStringLiteral("实体 ID 不能为空"));
    if (contains(candidate.id))
        return fail(errorMessage, QStringLiteral("实体 ID 已存在"));
    if (candidate.kind == EntityKind::ProjectRoot)
        return fail(errorMessage, QStringLiteral("只能存在一个项目根节点"));
    const WorkspaceEntity* parent = entity(parentId);
    if (!parent || !entityKindCanHaveChildren(parent->kind))
        return fail(errorMessage, QStringLiteral("父项不存在或不能包含子项"));
    if (parent->locked)
        return fail(errorMessage, QStringLiteral("父项已锁定"));
    if (candidate.name.trimmed().isEmpty())
        return fail(errorMessage, QStringLiteral("实体名称不能为空"));
    if (candidate.name.size() > 255)
        return fail(errorMessage, QStringLiteral("实体名称不能超过 255 个字符"));
    if (candidate.assetPath.has_value() && candidate.assetPath->contains(QChar::Null))
        return fail(errorMessage, QStringLiteral("资产路径无效"));
    if (candidate.provenance.has_value()
        && !candidate.provenance->isValid(errorMessage))
    {
        return false;
    }
    return true;
}

bool WorkspaceStore::isAncestor(const EntityId& possibleAncestor,
                                const EntityId& id) const
{
    const WorkspaceEntity* current = entity(id);
    while (current && !current->parentId.isNull())
    {
        if (current->parentId == possibleAncestor)
            return true;
        current = entity(current->parentId);
    }
    return false;
}

bool WorkspaceStore::subtreeContainsLockedEntity(const EntityId& id) const
{
    const WorkspaceEntity* current = entity(id);
    if (current && current->locked)
        return true;
    for (const EntityId& child : m_children.value(id))
    {
        if (subtreeContainsLockedEntity(child))
            return true;
    }
    return false;
}

void WorkspaceStore::collectSubtree(const EntityId& id,
                                    QVector<EntityId>& ids) const
{
    ids.append(id);
    for (const EntityId& child : m_children.value(id))
        collectSubtree(child, ids);
}

void WorkspaceStore::eraseSubtree(const EntityId& id)
{
    const QVector<EntityId> childIds = m_children.value(id);
    for (const EntityId& child : childIds)
        eraseSubtree(child);
    m_children.remove(id);
    m_entities.remove(id);
}

void WorkspaceStore::notifyAncestors(const EntityId& id,
                                     const QVector<int>& roles)
{
    const WorkspaceEntity* current = entity(id);
    while (current)
    {
        emit entityChanged(current->id, roles);
        if (current->parentId.isNull())
            break;
        current = entity(current->parentId);
    }
}

} // namespace famp::workspace
