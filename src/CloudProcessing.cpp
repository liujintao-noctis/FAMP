#include "CloudProcessing.h"

#include "FileIO.h"

#include <pcl/common/point_tests.h>
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/filters/voxel_grid.h>

#include <cmath>
#include <exception>

namespace famp::processing
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
    result.error = QStringLiteral("点云预处理已取消。");
    return true;
}
}

bool validateOptions(const Options& options,
                     std::size_t inputPointCount,
                     QString* errorMessage)
{
    if (inputPointCount == 0)
    {
        setError(errorMessage, QStringLiteral("点云为空，无法预处理。"));
        return false;
    }

    if (options.method == Method::VoxelDownsample)
    {
        if (!std::isfinite(options.voxelLeafSizeMeters)
            || options.voxelLeafSizeMeters < 1.0e-6
            || options.voxelLeafSizeMeters > 1000.0)
        {
            setError(errorMessage,
                     QStringLiteral("体素边长必须在 0.000001 到 1000 米之间。"));
            return false;
        }
    }
    else
    {
        if (inputPointCount < 3)
        {
            setError(errorMessage,
                     QStringLiteral("统计去噪至少需要 3 个有效点。"));
            return false;
        }
        if (options.meanNeighbors < 2
            || static_cast<std::size_t>(options.meanNeighbors) >= inputPointCount)
        {
            setError(errorMessage,
                     QStringLiteral("邻域点数必须至少为 2，且小于点云点数。"));
            return false;
        }
        if (!std::isfinite(options.standardDeviationMultiplier)
            || options.standardDeviationMultiplier <= 0.0
            || options.standardDeviationMultiplier > 100.0)
        {
            setError(errorMessage,
                     QStringLiteral("标准差倍数必须大于 0 且不超过 100。"));
            return false;
        }
    }

    if (errorMessage)
        errorMessage->clear();
    return true;
}

Result process(const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& input,
               const Options& options,
               const famp::tasks::CancellationCheck& shouldCancel)
{
    Result result;
    result.inputPointCount = input ? input->size() : 0;
    if (!input)
    {
        result.error = QStringLiteral("点云指针为空，无法预处理。");
        return result;
    }
    if (cancel(result, shouldCancel))
        return result;

    auto finiteCloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    finiteCloud->reserve(input->size());
    std::size_t pointIndex = 0;
    for (const pcl::PointXYZRGB& point : input->points)
    {
        if ((pointIndex++ & 0x0fffU) == 0U
            && cancel(result, shouldCancel))
        {
            return result;
        }
        if (pcl::isFinite(point))
            finiteCloud->push_back(point);
    }
    finiteCloud->width = static_cast<std::uint32_t>(finiteCloud->size());
    finiteCloud->height = 1;
    finiteCloud->is_dense = true;
    result.finitePointCount = finiteCloud->size();

    if (!validateOptions(options, finiteCloud->size(), &result.error))
        return result;
    if (cancel(result, shouldCancel))
        return result;

    try
    {
        result.cloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
        if (options.method == Method::VoxelDownsample)
        {
            pcl::VoxelGrid<pcl::PointXYZRGB> filter;
            const float leafSize = static_cast<float>(
                options.voxelLeafSizeMeters);
            filter.setInputCloud(finiteCloud);
            filter.setLeafSize(leafSize, leafSize, leafSize);
            filter.filter(*result.cloud);
        }
        else
        {
            pcl::StatisticalOutlierRemoval<pcl::PointXYZRGB> filter;
            filter.setInputCloud(finiteCloud);
            filter.setMeanK(options.meanNeighbors);
            filter.setStddevMulThresh(options.standardDeviationMultiplier);
            filter.filter(*result.cloud);
        }

        if (cancel(result, shouldCancel))
            return result;

        if (!result.cloud || result.cloud->empty())
        {
            result.cloud.reset();
            result.error = QStringLiteral("预处理结果为空，请调整参数后重试。");
            return result;
        }
        result.cloud->width = static_cast<std::uint32_t>(result.cloud->size());
        result.cloud->height = 1;
        result.cloud->is_dense = true;
        result.outputPointCount = result.cloud->size();
    }
    catch (const std::exception& exception)
    {
        result.cloud.reset();
        result.error = QStringLiteral("点云预处理失败：%1")
                           .arg(QString::fromLocal8Bit(exception.what()));
    }
    catch (...)
    {
        result.cloud.reset();
        result.error = QStringLiteral("点云预处理发生未知错误。");
    }
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
    if (!result.succeeded())
        return result;
    if (cancel(result, shouldCancel))
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
