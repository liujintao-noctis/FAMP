#include "CloudReprojection.h"

#include "CrsService.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr std::size_t CancellationInterval = 4096;

bool cancellationRequested(
    famp::cloud::ReprojectionResult& result,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!famp::tasks::isCancellationRequested(shouldCancel))
        return false;
    result.points.reset();
    result.cancelled = true;
    result.error = QStringLiteral("点云重投影已取消。");
    return true;
}

bool spatialMatrix(const famp::cloud::SpatialReference& spatial,
                   Eigen::Matrix4d& matrix)
{
    for (double value : spatial.origin)
    {
        if (!std::isfinite(value))
            return false;
    }
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            const double value = spatial.transform[static_cast<std::size_t>(
                row * 4 + column)];
            if (!std::isfinite(value))
                return false;
            matrix(row, column) = value;
        }
    }
    return true;
}

bool sourceCoordinate(const Eigen::Matrix4d& matrix,
                      const famp::cloud::SpatialReference& spatial,
                      double localX,
                      double localY,
                      double localZ,
                      famp::crs::Coordinate& coordinate)
{
    const Eigen::Vector4d transformed = matrix * Eigen::Vector4d(
        localX + spatial.origin[0],
        localY + spatial.origin[1],
        localZ + spatial.origin[2],
        1.0);
    if (!transformed.allFinite() || std::abs(transformed.w()) < 1.0e-12)
        return false;
    coordinate = famp::crs::Coordinate{
        transformed.x() / transformed.w(),
        transformed.y() / transformed.w(),
        transformed.z() / transformed.w()};
    return std::isfinite(coordinate.x)
        && std::isfinite(coordinate.y)
        && std::isfinite(coordinate.z);
}

bool toLocalFloat(double value, float& output)
{
    if (!std::isfinite(value)
        || std::abs(value) > std::numeric_limits<float>::max())
    {
        return false;
    }
    output = static_cast<float>(value);
    return std::isfinite(output);
}
}

namespace famp::cloud
{
ReprojectionResult reproject(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
    const SpatialReference& inputSpatial,
    const QString& sourceCrs,
    const QString& targetCrs,
    const famp::tasks::CancellationCheck& shouldCancel,
    const ReprojectionProgress& reportProgress)
{
    ReprojectionResult result;
    result.sourceCrs = famp::crs::normalizedEpsg(sourceCrs);
    result.targetCrs = famp::crs::normalizedEpsg(targetCrs);
    if (!input || input->empty())
    {
        result.error = QStringLiteral("没有可重投影的点云数据。");
        return result;
    }
    if (result.sourceCrs.isEmpty() || result.targetCrs.isEmpty())
    {
        result.error = QStringLiteral("源坐标系和目标坐标系必须是有效的 EPSG 编码。");
        return result;
    }
    if (result.sourceCrs == result.targetCrs)
    {
        result.error = QStringLiteral("源坐标系和目标坐标系相同，无需重投影。");
        return result;
    }
    if (cancellationRequested(result, shouldCancel))
        return result;

    Eigen::Matrix4d matrix;
    if (!spatialMatrix(inputSpatial, matrix))
    {
        result.error = QStringLiteral("点云空间参考包含无效数值。");
        return result;
    }

    famp::crs::Transformer transformer;
    if (!transformer.initialize(
            result.sourceCrs, result.targetCrs, &result.error))
    {
        return result;
    }

    // Keep display coordinates close to zero so PCL's float storage retains
    // sub-centimetre detail even when the projected CRS has large eastings.
    famp::crs::Coordinate sourceOrigin;
    famp::crs::Coordinate targetOrigin;
    if (!sourceCoordinate(
            matrix, inputSpatial, 0.0, 0.0, 0.0, sourceOrigin)
        || !transformer.transform(sourceOrigin, targetOrigin, &result.error))
    {
        if (result.error.isEmpty())
            result.error = QStringLiteral("无法转换点云中心坐标。");
        return result;
    }

    auto output = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    output->resize(input->size());
    output->width = input->width;
    output->height = input->height;
    output->is_dense = input->is_dense;

    const double denominator = static_cast<double>(input->size());
    for (std::size_t index = 0; index < input->size(); ++index)
    {
        if ((index % CancellationInterval) == 0)
        {
            if (cancellationRequested(result, shouldCancel))
                return result;
            if (reportProgress)
                reportProgress(static_cast<double>(index) / denominator);
        }

        const pcl::PointXYZRGB& sourcePoint = input->points[index];
        famp::crs::Coordinate sourceCoordinateValue;
        famp::crs::Coordinate targetCoordinate;
        QString pointError;
        if (!sourceCoordinate(
                matrix, inputSpatial,
                static_cast<double>(sourcePoint.x),
                static_cast<double>(sourcePoint.y),
                static_cast<double>(sourcePoint.z),
                sourceCoordinateValue)
            || !transformer.transform(
                sourceCoordinateValue, targetCoordinate, &pointError))
        {
            result.error = QStringLiteral("第 %1 个点重投影失败：%2")
                               .arg(index + 1)
                               .arg(pointError.isEmpty()
                                    ? QStringLiteral("坐标无效") : pointError);
            return result;
        }

        pcl::PointXYZRGB targetPoint = sourcePoint;
        if (!toLocalFloat(targetCoordinate.x - targetOrigin.x, targetPoint.x)
            || !toLocalFloat(targetCoordinate.y - targetOrigin.y, targetPoint.y)
            || !toLocalFloat(targetCoordinate.z - targetOrigin.z, targetPoint.z))
        {
            result.error = QStringLiteral("第 %1 个点的目标局部坐标超出可表示范围。")
                               .arg(index + 1);
            return result;
        }
        output->points[index] = targetPoint;
    }

    if (cancellationRequested(result, shouldCancel))
        return result;
    if (reportProgress)
        reportProgress(1.0);

    result.points = output;
    result.spatial = SpatialReference{};
    result.spatial.origin = {
        targetOrigin.x, targetOrigin.y, targetOrigin.z};
    result.error.clear();
    return result;
}
}
