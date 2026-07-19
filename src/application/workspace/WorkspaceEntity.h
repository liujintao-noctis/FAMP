#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QVector>

#include <array>
#include <memory>
#include <optional>
#include <typeindex>
#include <typeinfo>

namespace famp::workspace
{

using EntityId = QUuid;
using Matrix4d = std::array<double, 16>;

enum class EntityKind
{
    ProjectRoot,
    Group,
    PointCloud,
    Dem,
    ContourSet,
    Profile,
    CutFill,
    Measurement2D,
    Measurement3D,
    GraphicsItem
};

QString entityKindName(EntityKind kind);
bool entityKindCanHaveChildren(EntityKind kind);
bool entityKindIsRenderable(EntityKind kind);

struct Provenance
{
    EntityId operationId = QUuid::createUuid();
    QString operation;
    QVector<EntityId> sourceIds;
    QJsonObject sourceSnapshot;
    QJsonObject parameters;
    QDateTime createdAt = QDateTime::currentDateTimeUtc();
    QVector<Matrix4d> transforms;
    QJsonObject metrics;

    bool isValid(QString* errorMessage = nullptr) const;
};

struct WorkspaceEntity
{
    EntityId id;
    EntityId parentId;
    QString name;
    EntityKind kind = EntityKind::Group;
    bool visible = true;
    bool locked = false;
    bool dirty = false;
    std::optional<QString> assetPath;
    std::optional<Provenance> provenance;
    QJsonObject display;

    // Derived results are self-contained in memory. The type tag prevents an
    // accidental cast when a renderer or writer requests the payload.
    std::shared_ptr<void> payload;
    std::type_index payloadType{typeid(void)};

    template<typename T>
    void setPayload(std::shared_ptr<T> value)
    {
        payload = std::move(value);
        payloadType = std::type_index(typeid(T));
    }

    template<typename T>
    std::shared_ptr<T> payloadAs() const
    {
        if (!payload || payloadType != std::type_index(typeid(T)))
            return {};
        return std::static_pointer_cast<T>(payload);
    }

    bool hasPayload() const
    {
        return static_cast<bool>(payload);
    }
};

WorkspaceEntity makeEntity(EntityKind kind, const QString& name);

} // namespace famp::workspace

Q_DECLARE_METATYPE(famp::workspace::EntityKind)
