#include "CloudLayer.h"

#include "ArchaeologyMetadata.h"

#include <QFileInfo>
#include <QUuid>

#include <cmath>

namespace
{
constexpr int MaxLayerNameLength = 512;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool validSpatial(const famp::cloud::SpatialReference& spatial)
{
    for (double value : spatial.origin)
    {
        if (!std::isfinite(value))
            return false;
    }
    for (double value : spatial.transform)
    {
        if (!std::isfinite(value))
            return false;
    }
    return true;
}
}

namespace famp::cloud
{
std::size_t CloudLayer::pointCount() const
{
    return points ? points->size() : 0;
}

bool CloudLayer::isEmpty() const
{
    return !points || points->empty();
}

QString createLayerId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

QString stableLayerId(const QString& seed)
{
    static const QUuid namespaceId(
        QStringLiteral("{7f489adc-d0c3-53fd-bd92-d7212f244b5f}"));
    return QUuid::createUuidV5(namespaceId, seed.trimmed().toUtf8())
        .toString(QUuid::WithoutBraces)
        .toLower();
}

bool isValidLayerId(const QString& id)
{
    const QString trimmed = id.trimmed();
    if (trimmed.isEmpty())
        return false;
    const QUuid uuid(trimmed);
    return !uuid.isNull()
        && uuid.toString(QUuid::WithoutBraces)
               .compare(trimmed, Qt::CaseInsensitive) == 0;
}

CloudLayer makeLayer(
    const QString& sourcePath,
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& points,
    const SpatialReference& spatial,
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& sourcePoints)
{
    CloudLayer layer;
    layer.id = createLayerId();
    layer.sourcePath = sourcePath.trimmed().isEmpty()
        ? QString() : QFileInfo(sourcePath).absoluteFilePath();
    layer.name = QFileInfo(sourcePath).fileName();
    if (layer.name.isEmpty())
        layer.name = QStringLiteral("未命名点云");
    layer.points = points;
    layer.sourcePoints = sourcePoints;
    layer.spatial = spatial;
    return layer;
}

bool validateLayer(const CloudLayer& layer,
                   bool requirePoints,
                   QString* errorMessage)
{
    if (!isValidLayerId(layer.id))
    {
        setError(errorMessage, QStringLiteral("点云图层 ID 无效。"));
        return false;
    }
    const QString name = layer.name.trimmed();
    if (name.isEmpty() || name.size() > MaxLayerNameLength)
    {
        setError(errorMessage, QStringLiteral("点云图层名称不能为空且不能超过 512 个字符。"));
        return false;
    }
    if (requirePoints && layer.isEmpty())
    {
        setError(errorMessage, QStringLiteral("点云图层不包含可用点。"));
        return false;
    }
    if (!validSpatial(layer.spatial))
    {
        setError(errorMessage, QStringLiteral("点云图层空间变换包含无效数值。"));
        return false;
    }
    if (!layer.attributes.validate(
            static_cast<qint64>(layer.pointCount()), errorMessage))
    {
        return false;
    }
    if (!famp::display::validateSettings(layer.display, errorMessage))
        return false;
    if (!famp::archaeology::validateFields(
            layer.archaeologyFields, errorMessage))
        return false;
    if (!famp::control::validatePoints(layer.controlPoints, errorMessage))
        return false;
    if (errorMessage)
        errorMessage->clear();
    return true;
}
}
