#pragma once

#include "WorkspaceEntity.h"

#include <QHash>
#include <QObject>
#include <QSharedPointer>

namespace famp::workspace
{

class WorkspaceStore : public QObject
{
    Q_OBJECT

public:
    explicit WorkspaceStore(QObject* parent = nullptr);

    EntityId rootId() const;
    const WorkspaceEntity* entity(const EntityId& id) const;
    bool contains(const EntityId& id) const;
    QVector<EntityId> children(const EntityId& parentId) const;
    int rowOf(const EntityId& id) const;
    int size() const;

    EntityId addEntity(WorkspaceEntity entity,
                       const EntityId& parentId = {},
                       int row = -1,
                       QString* errorMessage = nullptr);
    // 派生点云与来源点云同级并紧邻；其他成果保留各自的分类规则。
    EntityId addDerivedEntity(WorkspaceEntity entity,
                              const EntityId& sourceId,
                              QString* errorMessage = nullptr);
    bool removeEntity(const EntityId& id, QString* errorMessage = nullptr);
    bool removeEntities(const QVector<EntityId>& ids,
                        QString* errorMessage = nullptr);
    bool moveEntity(const EntityId& id,
                    const EntityId& newParentId,
                    int row = -1,
                    QString* errorMessage = nullptr);
    bool moveEntities(const QVector<EntityId>& ids,
                      const EntityId& newParentId,
                      int row = -1,
                      QString* errorMessage = nullptr);

    bool setName(const EntityId& id,
                 const QString& name,
                 QString* errorMessage = nullptr);
    bool setVisible(const EntityId& id,
                    bool visible,
                    bool recursive = true,
                    QString* errorMessage = nullptr);
    bool setLocked(const EntityId& id,
                   bool locked,
                   QString* errorMessage = nullptr);
    bool setDirty(const EntityId& id, bool dirty);
    bool setAssetPath(const EntityId& id, const std::optional<QString>& path);
    bool replaceEntity(const WorkspaceEntity& entity,
                       QString* errorMessage = nullptr);

    QString uniqueSiblingName(const EntityId& parentId,
                              const QString& requested,
                              const EntityId& ignoredId = {}) const;
    QVector<EntityId> descendants(const EntityId& id) const;
    QVector<EntityId> allEntityIds() const;
    void sortChildren(const EntityId& parentId,
                      Qt::SortOrder order = Qt::AscendingOrder);
    void clear(const QString& projectName = QStringLiteral("未命名项目"));

signals:
    void entityAboutToBeInserted(famp::workspace::EntityId parentId, int row);
    void entityInserted(famp::workspace::EntityId id);
    void entityAboutToBeRemoved(famp::workspace::EntityId parentId, int row);
    void entityRemoved(famp::workspace::EntityId id,
                       QVector<famp::workspace::EntityId> removedIds);
    void entityChanged(famp::workspace::EntityId id, QVector<int> roles);
    void storeAboutToReset();
    void storeReset();

private:
    using EntityPointer = QSharedPointer<WorkspaceEntity>;

    EntityId m_rootId;
    QHash<EntityId, EntityPointer> m_entities;
    QHash<EntityId, QVector<EntityId>> m_children;

    bool validateNewEntity(const WorkspaceEntity& entity,
                           const EntityId& parentId,
                           QString* errorMessage) const;
    bool isAncestor(const EntityId& possibleAncestor,
                    const EntityId& id) const;
    bool subtreeContainsLockedEntity(const EntityId& id) const;
    void collectSubtree(const EntityId& id, QVector<EntityId>& ids) const;
    void eraseSubtree(const EntityId& id);
    void notifyAncestors(const EntityId& id, const QVector<int>& roles);
};

} // namespace famp::workspace
