#include "WorkspaceSnapshot.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace famp::workspace
{
namespace
{
constexpr int SnapshotSchemaVersion = 2;
constexpr int MinimumSnapshotSchemaVersion = 1;
constexpr int MaxEntities = 10000;
constexpr int MaxAssetPathLength = 4096;
constexpr int MaxSourcesPerOperation = 256;
constexpr int MaxTransformsPerOperation = 64;

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

QString kindKey(EntityKind kind)
{
    switch (kind)
    {
    case EntityKind::ProjectRoot: return QStringLiteral("project-root");
    case EntityKind::Group: return QStringLiteral("group");
    case EntityKind::PointCloud: return QStringLiteral("point-cloud");
    case EntityKind::Dem: return QStringLiteral("dem");
    case EntityKind::ContourSet: return QStringLiteral("contour-set");
    case EntityKind::Profile: return QStringLiteral("profile");
    case EntityKind::CutFill: return QStringLiteral("cut-fill");
    case EntityKind::Measurement2D: return QStringLiteral("measurement-2d");
    case EntityKind::Measurement3D: return QStringLiteral("measurement-3d");
    case EntityKind::GraphicsItem: return QStringLiteral("graphics-item");
    }
    return {};
}

bool kindFromKey(const QString& key, EntityKind& kind)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("group")) kind = EntityKind::Group;
    else if (normalized == QStringLiteral("point-cloud")) kind = EntityKind::PointCloud;
    else if (normalized == QStringLiteral("dem")) kind = EntityKind::Dem;
    else if (normalized == QStringLiteral("contour-set")) kind = EntityKind::ContourSet;
    else if (normalized == QStringLiteral("profile")) kind = EntityKind::Profile;
    else if (normalized == QStringLiteral("cut-fill")) kind = EntityKind::CutFill;
    else if (normalized == QStringLiteral("measurement-2d")) kind = EntityKind::Measurement2D;
    else if (normalized == QStringLiteral("measurement-3d")) kind = EntityKind::Measurement3D;
    else if (normalized == QStringLiteral("graphics-item")) kind = EntityKind::GraphicsItem;
    else return false;
    return true;
}

QString uuidText(const EntityId& id)
{
    return id.toString(QUuid::WithoutBraces).toLower();
}

bool parseUuid(const QJsonValue& value, EntityId& id)
{
    if (!value.isString())
        return false;
    const EntityId parsed(value.toString().trimmed());
    if (parsed.isNull())
        return false;
    id = parsed;
    return true;
}

QJsonArray matrixArray(const Matrix4d& matrix)
{
    QJsonArray values;
    for (double value : matrix)
        values.append(value);
    return values;
}

bool readMatrix(const QJsonValue& value, Matrix4d& matrix)
{
    if (!value.isArray() || value.toArray().size() != 16)
        return false;
    Matrix4d candidate{};
    const QJsonArray values = value.toArray();
    for (int index = 0; index < values.size(); ++index)
    {
        if (!values.at(index).isDouble()
            || !std::isfinite(values.at(index).toDouble()))
        {
            return false;
        }
        candidate[static_cast<std::size_t>(index)] = values.at(index).toDouble();
    }
    matrix = candidate;
    return true;
}

QJsonObject serializeProvenance(const Provenance& provenance)
{
    QJsonArray sourceIds;
    for (const EntityId& id : provenance.sourceIds)
        sourceIds.append(uuidText(id));
    QJsonArray transforms;
    for (const Matrix4d& transform : provenance.transforms)
        transforms.append(matrixArray(transform));
    return QJsonObject{
        {QStringLiteral("operationId"), uuidText(provenance.operationId)},
        {QStringLiteral("operation"), provenance.operation},
        {QStringLiteral("sourceIds"), sourceIds},
        {QStringLiteral("sourceSnapshot"), provenance.sourceSnapshot},
        {QStringLiteral("parameters"), provenance.parameters},
        {QStringLiteral("createdAtUtc"),
         provenance.createdAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("transforms"), transforms},
        {QStringLiteral("metrics"), provenance.metrics}};
}

bool deserializeProvenance(const QJsonValue& value,
                           Provenance& provenance)
{
    if (!value.isObject())
        return false;
    const QJsonObject object = value.toObject();
    const QJsonValue operation = object.value(QStringLiteral("operation"));
    const QJsonValue sourceIds = object.value(QStringLiteral("sourceIds"));
    const QJsonValue sourceSnapshot = object.value(QStringLiteral("sourceSnapshot"));
    const QJsonValue parameters = object.value(QStringLiteral("parameters"));
    const QJsonValue createdAt = object.value(QStringLiteral("createdAtUtc"));
    const QJsonValue transforms = object.value(QStringLiteral("transforms"));
    const QJsonValue metrics = object.value(QStringLiteral("metrics"));
    Provenance candidate;
    if (!parseUuid(object.value(QStringLiteral("operationId")),
                   candidate.operationId)
        || !operation.isString() || !sourceIds.isArray()
        || sourceIds.toArray().size() > MaxSourcesPerOperation
        || !sourceSnapshot.isObject() || !parameters.isObject()
        || !createdAt.isString() || !transforms.isArray()
        || transforms.toArray().size() > MaxTransformsPerOperation
        || !metrics.isObject())
    {
        return false;
    }
    candidate.operation = operation.toString().trimmed();
    for (const QJsonValue& sourceIdValue : sourceIds.toArray())
    {
        EntityId sourceId;
        if (!parseUuid(sourceIdValue, sourceId))
            return false;
        candidate.sourceIds.append(sourceId);
    }
    candidate.sourceSnapshot = sourceSnapshot.toObject();
    candidate.parameters = parameters.toObject();
    candidate.createdAt = QDateTime::fromString(
        createdAt.toString(), Qt::ISODateWithMs);
    if (!candidate.createdAt.isValid())
        candidate.createdAt = QDateTime::fromString(createdAt.toString(), Qt::ISODate);
    for (const QJsonValue& matrixValue : transforms.toArray())
    {
        Matrix4d matrix{};
        if (!readMatrix(matrixValue, matrix))
            return false;
        candidate.transforms.append(matrix);
    }
    candidate.metrics = metrics.toObject();
    if (!candidate.isValid())
        return false;
    provenance = candidate;
    return true;
}

bool validateSnapshot(const WorkspaceSnapshot& snapshot,
                      QString* errorMessage)
{
    if (snapshot.rootId.isNull() || snapshot.rootName.trimmed().isEmpty()
        || snapshot.rootName.size() > 255)
    {
        return fail(errorMessage, QStringLiteral("工作区根节点无效"));
    }
    if (snapshot.entities.size() > MaxEntities)
        return fail(errorMessage, QStringLiteral("工作区实体数量超过上限"));

    QHash<EntityId, SnapshotRecord> records;
    QHash<EntityId, QSet<QString>> siblingNames;
    QHash<EntityId, QSet<int>> siblingRows;
    for (const SnapshotRecord& record : snapshot.entities)
    {
        if (record.id.isNull() || record.id == snapshot.rootId
            || records.contains(record.id)
            || record.parentId.isNull()
            || record.kind == EntityKind::ProjectRoot
            || record.name.trimmed().isEmpty() || record.name.size() > 255
            || record.row < 0
            || (record.assetPath.has_value()
                && (record.assetPath->trimmed().isEmpty()
                    || record.assetPath->size() > MaxAssetPathLength))
            || (record.provenance.has_value()
                && !record.provenance->isValid()))
        {
            return fail(errorMessage, QStringLiteral("工作区包含无效或重复实体"));
        }
        const QString nameKey = record.name.trimmed().toCaseFolded();
        if (siblingNames[record.parentId].contains(nameKey)
            || siblingRows[record.parentId].contains(record.row))
        {
            return fail(errorMessage, QStringLiteral("工作区同级名称或顺序重复"));
        }
        siblingNames[record.parentId].insert(nameKey);
        siblingRows[record.parentId].insert(record.row);
        records.insert(record.id, record);
    }

    for (const SnapshotRecord& record : snapshot.entities)
    {
        if (record.parentId != snapshot.rootId)
        {
            const auto parent = records.constFind(record.parentId);
            if (parent == records.cend() || parent->kind != EntityKind::Group)
                return fail(errorMessage, QStringLiteral("工作区实体父节点无效"));
        }
        QSet<EntityId> visited{record.id};
        EntityId parentId = record.parentId;
        while (parentId != snapshot.rootId)
        {
            if (visited.contains(parentId) || !records.contains(parentId))
                return fail(errorMessage, QStringLiteral("工作区层级存在循环"));
            visited.insert(parentId);
            parentId = records.value(parentId).parentId;
        }
    }
    succeed(errorMessage);
    return true;
}

} // namespace

QJsonObject emptySnapshot()
{
    const EntityId rootId = QUuid::createUuid();
    return QJsonObject{
        {QStringLiteral("schemaVersion"), SnapshotSchemaVersion},
        {QStringLiteral("rootId"), uuidText(rootId)},
        {QStringLiteral("rootName"), QStringLiteral("未命名项目")},
        {QStringLiteral("entities"), QJsonArray()}};
}

QJsonObject serializeSnapshot(const WorkspaceStore& store,
                              QString* errorMessage,
                              const QHash<EntityId, QString>& assetOverrides)
{
    const WorkspaceEntity* root = store.entity(store.rootId());
    if (!root)
    {
        fail(errorMessage, QStringLiteral("工作区缺少根节点"));
        return {};
    }
    QJsonArray entities;
    for (const EntityId& id : store.descendants(store.rootId()))
    {
        const WorkspaceEntity* entity = store.entity(id);
        if (!entity)
            continue;
        QJsonObject serialized{
            {QStringLiteral("id"), uuidText(entity->id)},
            {QStringLiteral("parentId"), uuidText(entity->parentId)},
            {QStringLiteral("kind"), kindKey(entity->kind)},
            {QStringLiteral("name"), entity->name},
            {QStringLiteral("row"), store.rowOf(entity->id)},
            {QStringLiteral("visible"), entity->visible},
            {QStringLiteral("locked"), entity->locked},
            {QStringLiteral("display"), entity->display}};
        const QString overridePath = assetOverrides.value(entity->id).trimmed();
        const QString assetPath = !overridePath.isEmpty()
            ? overridePath
            : (entity->assetPath.has_value()
                   ? entity->assetPath->trimmed() : QString());
        if (!assetPath.isEmpty())
            serialized.insert(QStringLiteral("assetPath"), assetPath);
        if (entity->provenance.has_value())
        {
            serialized.insert(
                QStringLiteral("provenance"),
                serializeProvenance(*entity->provenance));
        }
        entities.append(serialized);
    }
    const QJsonObject result{
        {QStringLiteral("schemaVersion"), SnapshotSchemaVersion},
        {QStringLiteral("rootId"), uuidText(root->id)},
        {QStringLiteral("rootName"), root->name},
        {QStringLiteral("entities"), entities}};
    WorkspaceSnapshot validated;
    if (!deserializeSnapshot(result, validated, errorMessage))
        return {};
    succeed(errorMessage);
    return result;
}

bool deserializeSnapshot(const QJsonObject& object,
                         WorkspaceSnapshot& snapshot,
                         QString* errorMessage)
{
    const QJsonValue schema = object.value(QStringLiteral("schemaVersion"));
    const QJsonValue rootName = object.value(QStringLiteral("rootName"));
    const QJsonValue entities = object.value(QStringLiteral("entities"));
    WorkspaceSnapshot candidate;
    const int schemaVersion = schema.toInt(-1);
    if (!schema.isDouble()
        || schemaVersion < MinimumSnapshotSchemaVersion
        || schemaVersion > SnapshotSchemaVersion
        || !parseUuid(object.value(QStringLiteral("rootId")), candidate.rootId)
        || !rootName.isString() || !entities.isArray()
        || entities.toArray().size() > MaxEntities)
    {
        return fail(errorMessage, QStringLiteral("工作区快照结构无效"));
    }
    candidate.rootName = rootName.toString().trimmed();
    for (const QJsonValue& value : entities.toArray())
    {
        if (!value.isObject())
            return fail(errorMessage, QStringLiteral("工作区实体记录无效"));
        const QJsonObject recordObject = value.toObject();
        const QJsonValue kind = recordObject.value(QStringLiteral("kind"));
        const QJsonValue name = recordObject.value(QStringLiteral("name"));
        const QJsonValue row = recordObject.value(QStringLiteral("row"));
        const QJsonValue visible = recordObject.value(QStringLiteral("visible"));
        const QJsonValue locked = recordObject.value(QStringLiteral("locked"));
        const QJsonValue display = recordObject.value(QStringLiteral("display"));
        SnapshotRecord record;
        if (!parseUuid(recordObject.value(QStringLiteral("id")), record.id)
            || !parseUuid(recordObject.value(QStringLiteral("parentId")),
                          record.parentId)
            || !kind.isString() || !kindFromKey(kind.toString(), record.kind)
            || !name.isString() || !row.isDouble()
            || row.toDouble() < 0.0 || std::floor(row.toDouble()) != row.toDouble()
            || !visible.isBool() || !locked.isBool() || !display.isObject())
        {
            return fail(errorMessage, QStringLiteral("工作区实体字段无效"));
        }
        record.name = name.toString().trimmed();
        record.row = row.toInt();
        record.visible = visible.toBool();
        record.locked = locked.toBool();
        record.display = display.toObject();
        const QJsonValue assetPath = recordObject.value(
            QStringLiteral("assetPath"));
        if (!assetPath.isUndefined())
        {
            if (!assetPath.isString()
                || assetPath.toString().trimmed().isEmpty()
                || assetPath.toString().size() > MaxAssetPathLength)
            {
                return fail(errorMessage,
                            QStringLiteral("工作区实体资产路径无效"));
            }
            record.assetPath = assetPath.toString().trimmed();
        }
        const QJsonValue provenance = recordObject.value(
            QStringLiteral("provenance"));
        if (!provenance.isUndefined())
        {
            Provenance parsed;
            if (!deserializeProvenance(provenance, parsed))
                return fail(errorMessage, QStringLiteral("工作区来源追溯记录无效"));
            record.provenance = parsed;
        }
        candidate.entities.append(record);
    }
    if (!validateSnapshot(candidate, errorMessage))
        return false;
    snapshot = candidate;
    succeed(errorMessage);
    return true;
}

bool applySnapshot(const WorkspaceSnapshot& snapshot,
                   WorkspaceStore& store,
                   QString* errorMessage)
{
    if (!validateSnapshot(snapshot, errorMessage))
        return false;

    QHash<EntityId, SnapshotRecord> records;
    for (const SnapshotRecord& record : snapshot.entities)
        records.insert(record.id, record);
    for (const SnapshotRecord& record : snapshot.entities)
    {
        const WorkspaceEntity* existing = store.entity(record.id);
        if (existing && existing->kind != record.kind)
            return fail(errorMessage, QStringLiteral("工作区实体类型与已加载数据冲突"));
    }

    store.setName(store.rootId(), snapshot.rootName);
    for (const SnapshotRecord& record : snapshot.entities)
    {
        if (store.contains(record.id))
            store.setLocked(record.id, false);
    }

    QSet<EntityId> pending;
    for (const SnapshotRecord& record : snapshot.entities)
    {
        if (!store.contains(record.id) && record.kind != EntityKind::PointCloud)
            pending.insert(record.id);
    }
    while (!pending.isEmpty())
    {
        bool progressed = false;
        const QSet<EntityId> iteration = pending;
        for (const EntityId& id : iteration)
        {
            const SnapshotRecord record = records.value(id);
            const EntityId parentId = record.parentId == snapshot.rootId
                ? store.rootId() : record.parentId;
            if (!store.contains(parentId))
                continue;
            WorkspaceEntity entity = makeEntity(record.kind, record.name);
            entity.id = record.id;
            entity.visible = record.visible;
            entity.locked = false;
            entity.dirty = false;
            entity.assetPath = record.assetPath;
            entity.provenance = record.provenance;
            entity.display = record.display;
            QString addError;
            if (store.addEntity(entity, parentId, record.row, &addError).isNull())
                return fail(errorMessage, addError);
            pending.remove(id);
            progressed = true;
        }
        if (!progressed)
            return fail(errorMessage, QStringLiteral("无法恢复工作区层级"));
    }

    QVector<SnapshotRecord> ordered = snapshot.entities;
    std::sort(ordered.begin(), ordered.end(), [](const SnapshotRecord& left,
                                                 const SnapshotRecord& right) {
        if (left.parentId == right.parentId)
            return left.row < right.row;
        return uuidText(left.parentId) < uuidText(right.parentId);
    });
    QHash<EntityId, int> nextRow;
    for (const SnapshotRecord& record : ordered)
    {
        if (!store.contains(record.id))
            continue;
        const EntityId parentId = record.parentId == snapshot.rootId
            ? store.rootId() : record.parentId;
        QString moveError;
        if (!store.moveEntity(
                record.id, parentId, nextRow[parentId]++, &moveError))
        {
            return fail(errorMessage, moveError);
        }
    }

    for (const SnapshotRecord& record : snapshot.entities)
    {
        const WorkspaceEntity* current = store.entity(record.id);
        if (!current)
            continue;
        WorkspaceEntity replacement = *current;
        replacement.name = record.name;
        replacement.visible = record.visible;
        replacement.locked = record.locked;
        replacement.dirty = false;
        if (record.kind != EntityKind::PointCloud)
            replacement.assetPath = record.assetPath;
        replacement.provenance = record.provenance;
        replacement.display = record.display;
        QString replaceError;
        if (!store.replaceEntity(replacement, &replaceError))
            return fail(errorMessage, replaceError);
    }
    store.setDirty(store.rootId(), false);
    succeed(errorMessage);
    return true;
}

} // namespace famp::workspace
