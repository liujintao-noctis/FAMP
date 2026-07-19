#pragma once

#include "WorkspaceStore.h"

#include <QJsonObject>

namespace famp::workspace
{

struct SnapshotRecord
{
    EntityId id;
    EntityId parentId;
    EntityKind kind = EntityKind::Group;
    QString name;
    int row = 0;
    bool visible = true;
    bool locked = false;
    std::optional<QString> assetPath;
    std::optional<Provenance> provenance;
    QJsonObject display;
};

struct WorkspaceSnapshot
{
    EntityId rootId;
    QString rootName;
    QVector<SnapshotRecord> entities;
};

QJsonObject serializeSnapshot(const WorkspaceStore& store,
                              QString* errorMessage = nullptr,
                              const QHash<EntityId, QString>& assetOverrides = {});
bool deserializeSnapshot(const QJsonObject& object,
                         WorkspaceSnapshot& snapshot,
                         QString* errorMessage = nullptr);
bool applySnapshot(const WorkspaceSnapshot& snapshot,
                   WorkspaceStore& store,
                   QString* errorMessage = nullptr);
QJsonObject emptySnapshot();

} // namespace famp::workspace
