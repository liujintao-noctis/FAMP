#include "CloudCrop.h"

#include "FileIO.h"

#include <pcl/common/point_tests.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace famp::crop
{
namespace
{
void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool cancel(Result& result,
            const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!famp::tasks::isCancellationRequested(shouldCancel))
        return false;
    result.cloud.reset();
    result.outputPointCount = 0;
    result.cancelled = true;
    result.error = QStringLiteral("点云范围裁剪已取消。");
    return true;
}

void expandFlatRange(double& minimum, double& maximum)
{
    if (minimum < maximum)
        return;
    const double padding = std::max(0.5, std::abs(minimum) * 1.0e-6);
    minimum -= padding;
    maximum += padding;
}
}

bool dataBounds(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
    Options& bounds,
    QString* errorMessage)
{
    if (!input || input->empty())
    {
        setError(errorMessage, QStringLiteral("点云为空，无法计算裁剪范围。"));
        return false;
    }

    double minimumX = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double minimumY = minimumX;
    double maximumY = maximumX;
    double minimumZ = minimumX;
    double maximumZ = maximumX;
    std::size_t finiteCount = 0;
    for (const pcl::PointXYZRGB& point : input->points)
    {
        if (!pcl::isFinite(point))
            continue;
        minimumX = std::min(minimumX, static_cast<double>(point.x));
        maximumX = std::max(maximumX, static_cast<double>(point.x));
        minimumY = std::min(minimumY, static_cast<double>(point.y));
        maximumY = std::max(maximumY, static_cast<double>(point.y));
        minimumZ = std::min(minimumZ, static_cast<double>(point.z));
        maximumZ = std::max(maximumZ, static_cast<double>(point.z));
        ++finiteCount;
    }
    if (finiteCount == 0)
    {
        setError(errorMessage, QStringLiteral("点云不包含有限坐标。"));
        return false;
    }

    expandFlatRange(minimumX, maximumX);
    expandFlatRange(minimumY, maximumY);
    expandFlatRange(minimumZ, maximumZ);
    Options candidate = bounds;
    candidate.minimumX = minimumX;
    candidate.maximumX = maximumX;
    candidate.minimumY = minimumY;
    candidate.maximumY = maximumY;
    candidate.minimumZ = minimumZ;
    candidate.maximumZ = maximumZ;
    bounds = candidate;
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool validateOptions(const Options& options, QString* errorMessage)
{
    const double values[]{
        options.minimumX, options.maximumX,
        options.minimumY, options.maximumY,
        options.minimumZ, options.maximumZ};
    for (double value : values)
    {
        if (!std::isfinite(value))
        {
            setError(errorMessage, QStringLiteral("裁剪范围必须是有限数值。"));
            return false;
        }
    }
    if (options.minimumX >= options.maximumX
        || options.minimumY >= options.maximumY
        || options.minimumZ >= options.maximumZ)
    {
        setError(errorMessage,
                 QStringLiteral("每个坐标轴的最小值都必须小于最大值。"));
        return false;
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

Result process(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
    const Options& options,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    Result result;
    result.inputPointCount = input ? input->size() : 0;
    if (!input || input->empty())
    {
        result.error = QStringLiteral("点云为空，无法范围裁剪。");
        return result;
    }
    if (!validateOptions(options, &result.error))
        return result;
    if (cancel(result, shouldCancel))
        return result;

    result.cloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
    result.cloud->reserve(input->size());
    std::size_t pointIndex = 0;
    for (const pcl::PointXYZRGB& point : input->points)
    {
        if ((pointIndex++ & 0x0fffU) == 0U
            && cancel(result, shouldCancel))
        {
            return result;
        }
        if (!pcl::isFinite(point))
            continue;
        const bool inside = point.x >= options.minimumX
            && point.x <= options.maximumX
            && point.y >= options.minimumY
            && point.y <= options.maximumY
            && point.z >= options.minimumZ
            && point.z <= options.maximumZ;
        if (inside == options.keepInside)
            result.cloud->push_back(point);
    }
    if (cancel(result, shouldCancel))
        return result;
    if (result.cloud->empty())
    {
        result.cloud.reset();
        result.error = QStringLiteral("裁剪结果为空，请调整范围或保留模式。");
        return result;
    }
    result.cloud->width = static_cast<std::uint32_t>(result.cloud->size());
    result.cloud->height = 1;
    result.cloud->is_dense = true;
    result.outputPointCount = result.cloud->size();
    return result;
}

Result processAndSave(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
    const Options& options,
    const QString& requestedOutputPath,
    const famp::tasks::CancellationCheck& shouldCancel,
    const famp::cloud::SpatialReference* spatial)
{
    Result result = process(input, options, shouldCancel);
    if (!result.succeeded() || cancel(result, shouldCancel))
        return result;
    result.outputPath = famp::io::pathWithRequiredSuffix(
        requestedOutputPath, QStringLiteral("pcd"));
    if (!famp::io::savePcdAsciiAtomically(
            result.outputPath, *result.cloud, &result.error, spatial))
    {
        result.cloud.reset();
        result.outputPointCount = 0;
    }
    return result;
}
}
