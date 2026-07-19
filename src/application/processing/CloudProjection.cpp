#include "CloudProjection.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

namespace famp::projection
{

QString axisName(Plane plane)
{
    switch (plane)
    {
    case Plane::YOZ:
        return QStringLiteral("YOZ");
    case Plane::XOZ:
        return QStringLiteral("XOZ");
    case Plane::XOY:
    case Plane::Overlook:
        return QStringLiteral("XOY");
    }
    return QStringLiteral("XOY");
}

QString displayName(Plane plane)
{
    return plane == Plane::Overlook
        ? QStringLiteral("俯视 XOY") : axisName(plane);
}

bool isOverlook(Plane plane) noexcept
{
    return plane == Plane::Overlook;
}

std::optional<Plane> planeFromMetadata(const QString& axis,
                                       bool overlook) noexcept
{
    const QString normalized = axis.trimmed().toUpper();
    if (overlook)
        return normalized == QStringLiteral("XOY")
            ? std::optional<Plane>(Plane::Overlook) : std::nullopt;
    if (normalized == QStringLiteral("XOY"))
        return Plane::XOY;
    if (normalized == QStringLiteral("XOZ"))
        return Plane::XOZ;
    if (normalized == QStringLiteral("YOZ"))
        return Plane::YOZ;
    return std::nullopt;
}

bool Result::succeeded() const noexcept
{
    return points && !points->empty() && error.isEmpty();
}

Result projectToMinimumPlane(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& source,
    Plane plane)
{
    Result result;
    result.plane = plane;
    if (!source || source->empty())
    {
        result.error = QStringLiteral("投影来源点云为空。");
        return result;
    }

    std::array<double, 3> minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    for (const pcl::PointXYZRGB& point : source->points)
    {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)
            || !std::isfinite(point.z))
        {
            result.error = QStringLiteral("投影来源点云包含非有限坐标。");
            return result;
        }
        minimum[0] = std::min(minimum[0], static_cast<double>(point.x));
        minimum[1] = std::min(minimum[1], static_cast<double>(point.y));
        minimum[2] = std::min(minimum[2], static_cast<double>(point.z));
    }

    result.origin = minimum;
    result.points.reset(new pcl::PointCloud<pcl::PointXYZRGB>(*source));
    switch (plane)
    {
    case Plane::YOZ:
        result.normal = {1.0, 0.0, 0.0};
        for (pcl::PointXYZRGB& point : result.points->points)
            point.x = static_cast<float>(minimum[0]);
        break;
    case Plane::XOZ:
        result.normal = {0.0, 1.0, 0.0};
        for (pcl::PointXYZRGB& point : result.points->points)
            point.y = static_cast<float>(minimum[1]);
        break;
    case Plane::XOY:
    case Plane::Overlook:
        result.normal = {0.0, 0.0, 1.0};
        for (pcl::PointXYZRGB& point : result.points->points)
            point.z = static_cast<float>(minimum[2]);
        break;
    }
    return result;
}

bool Workflow::selectSource(
    const QUuid& entityId,
    const QString& name,
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& points,
    QString* errorMessage)
{
    if (entityId.isNull())
        return fail(errorMessage, QStringLiteral("投影来源实体 ID 为空。"));
    if (!points || points->empty())
        return fail(errorMessage, QStringLiteral("投影来源点云为空。"));

    const bool changed = !source_.has_value()
        || source_->entityId != entityId
        || source_->points.get() != points.get()
        || source_->points->size() != points->size();
    source_ = Source{entityId, name.trimmed(), points};
    if (changed)
        preview_.reset();
    else if (preview_.has_value())
        preview_->source = *source_;
    succeed(errorMessage);
    return true;
}

void Workflow::clearSource()
{
    preview_.reset();
    source_.reset();
}

bool Workflow::hasSource() const noexcept
{
    return source_.has_value();
}

const Source* Workflow::source() const noexcept
{
    return source_.has_value() ? &*source_ : nullptr;
}

bool Workflow::setPreview(
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& points,
    Plane plane,
    QString* errorMessage)
{
    if (!source_.has_value())
        return fail(errorMessage, QStringLiteral("尚未选择投影来源点云。"));
    if (!points || points->empty())
        return fail(errorMessage, QStringLiteral("投影预览点云为空。"));
    if (points->size() != source_->points->size())
    {
        return fail(
            errorMessage,
            QStringLiteral("投影预览点数与来源点云不一致。"));
    }
    preview_ = Preview{*source_, points, plane};
    succeed(errorMessage);
    return true;
}

void Workflow::clearPreview()
{
    preview_.reset();
}

bool Workflow::hasPreview() const noexcept
{
    return preview_.has_value();
}

const Preview* Workflow::preview() const noexcept
{
    return preview_.has_value() ? &*preview_ : nullptr;
}

} // namespace famp::projection
