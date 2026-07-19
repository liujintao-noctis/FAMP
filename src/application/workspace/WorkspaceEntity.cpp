#include "WorkspaceEntity.h"

#include <cmath>

namespace famp::workspace
{
namespace
{

bool setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
    return false;
}

} // namespace

QString entityKindName(EntityKind kind)
{
    switch (kind)
    {
    case EntityKind::ProjectRoot:
        return QStringLiteral("项目");
    case EntityKind::Group:
        return QStringLiteral("组");
    case EntityKind::PointCloud:
        return QStringLiteral("点云");
    case EntityKind::Dem:
        return QStringLiteral("DEM");
    case EntityKind::ContourSet:
        return QStringLiteral("等高线");
    case EntityKind::Profile:
        return QStringLiteral("剖面");
    case EntityKind::CutFill:
        return QStringLiteral("挖填方");
    case EntityKind::Measurement2D:
        return QStringLiteral("二维测量");
    case EntityKind::Measurement3D:
        return QStringLiteral("三维测量");
    case EntityKind::GraphicsItem:
        return QStringLiteral("二维图元");
    }
    return QStringLiteral("未知实体");
}

bool entityKindCanHaveChildren(EntityKind kind)
{
    return kind == EntityKind::ProjectRoot || kind == EntityKind::Group;
}

bool entityKindIsRenderable(EntityKind kind)
{
    return kind != EntityKind::ProjectRoot && kind != EntityKind::Group;
}

bool Provenance::isValid(QString* errorMessage) const
{
    if (operationId.isNull())
        return setError(errorMessage, QStringLiteral("来源操作 ID 不能为空"));
    if (operation.trimmed().isEmpty())
        return setError(errorMessage, QStringLiteral("来源操作名称不能为空"));
    if (!createdAt.isValid())
        return setError(errorMessage, QStringLiteral("来源时间无效"));
    for (const EntityId& id : sourceIds)
    {
        if (id.isNull())
            return setError(errorMessage, QStringLiteral("来源实体 ID 不能为空"));
    }
    for (const Matrix4d& matrix : transforms)
    {
        for (double value : matrix)
        {
            if (!std::isfinite(value))
                return setError(errorMessage, QStringLiteral("来源变换包含非有限值"));
        }
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

WorkspaceEntity makeEntity(EntityKind kind, const QString& name)
{
    WorkspaceEntity entity;
    entity.id = QUuid::createUuid();
    entity.kind = kind;
    entity.name = name.trimmed();
    return entity;
}

} // namespace famp::workspace
