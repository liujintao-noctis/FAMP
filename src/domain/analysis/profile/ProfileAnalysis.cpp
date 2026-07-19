#include "ProfileAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <vector>

namespace
{
constexpr quint64 AbsoluteMaximumBinCount = 2'000'000;
constexpr quint64 AbsoluteMaximumSampleCount = 10'000'000;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool finitePoint(const famp::cloud::Point3d& point)
{
    return std::isfinite(point[0])
        && std::isfinite(point[1])
        && std::isfinite(point[2]);
}

bool cancelled(const famp::tasks::CancellationCheck& shouldCancel)
{
    return famp::tasks::isCancellationRequested(shouldCancel);
}

struct Accumulator
{
    int count = 0;
    long double sum = 0.0L;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
};

double selectedValue(const famp::profile::Bin& bin,
                     famp::profile::Statistic statistic)
{
    switch (statistic)
    {
    case famp::profile::Statistic::Minimum:
        return bin.minimum;
    case famp::profile::Statistic::Maximum:
        return bin.maximum;
    case famp::profile::Statistic::Mean:
        return bin.mean;
    case famp::profile::Statistic::Median:
        return bin.median;
    }
    return std::numeric_limits<double>::quiet_NaN();
}
}

namespace famp::profile
{
bool Bin::hasSelectedValue() const
{
    return pointCount > 0 && std::isfinite(selected);
}

bool Result::isValid(
    const famp::tasks::CancellationCheck& shouldCancel) const
{
    if (!finitePoint(baseline.start) || !finitePoint(baseline.end)
        || !std::isfinite(length) || length <= 0.0
        || !std::isfinite(corridorWidth) || corridorWidth <= 0.0
        || !std::isfinite(binSize) || binSize <= 0.0
        || !std::isfinite(horizontalUnitToMetre)
        || horizontalUnitToMetre <= 0.0
        || minimumPointsPerBin < 1
        || bins.isEmpty() || samples.size() < 2
        || selectedPointCount != static_cast<quint64>(samples.size())
        || populatedBinCount < 2
        || !std::isfinite(minimumElevation)
        || !std::isfinite(maximumElevation)
        || minimumElevation > maximumElevation)
    {
        return false;
    }

    int selectedBins = 0;
    quint64 countedSamples = 0;
    for (int index = 0; index < bins.size(); ++index)
    {
        if ((static_cast<unsigned int>(index) & 4095U) == 0U
            && famp::tasks::isCancellationRequested(shouldCancel))
        {
            return false;
        }
        const Bin& bin = bins.at(index);
        if (bin.index != index || bin.pointCount < 0
            || !std::isfinite(bin.startStation)
            || !std::isfinite(bin.endStation)
            || !std::isfinite(bin.centerStation)
            || bin.startStation < 0.0
            || bin.endStation < bin.startStation
            || bin.endStation > length + std::max(1.0, length) * 1.0e-12)
        {
            return false;
        }
        countedSamples += static_cast<quint64>(bin.pointCount);
        if (bin.pointCount > 0)
        {
            const double tolerance = std::max(
                {1.0, std::abs(bin.minimum), std::abs(bin.maximum)})
                * 1.0e-12;
            if (!std::isfinite(bin.minimum) || !std::isfinite(bin.maximum)
                || !std::isfinite(bin.mean) || !std::isfinite(bin.median)
                || bin.minimum > bin.maximum
                || bin.mean < bin.minimum - tolerance
                || bin.mean > bin.maximum + tolerance
                || bin.median < bin.minimum - tolerance
                || bin.median > bin.maximum + tolerance)
            {
                return false;
            }
        }
        if (bin.hasSelectedValue())
            ++selectedBins;
    }
    if (selectedBins != populatedBinCount
        || countedSamples != selectedPointCount)
        return false;

    for (qsizetype index = 0; index < samples.size(); ++index)
    {
        if ((index % 4096) == 0
            && famp::tasks::isCancellationRequested(shouldCancel))
            return false;
        const Sample& sample = samples.at(index);
        if (sample.binIndex < 0 || sample.binIndex >= bins.size()
            || !std::isfinite(sample.station)
            || !std::isfinite(sample.signedOffset)
            || !finitePoint(sample.coordinate)
            || sample.station < -1.0e-9
            || sample.station > length + 1.0e-9)
        {
            return false;
        }
    }
    return true;
}

bool Result::succeeded() const
{
    return isValid() && error.isEmpty() && !cancelled;
}

QString statisticName(Statistic statistic)
{
    switch (statistic)
    {
    case Statistic::Minimum:
        return QStringLiteral("最低值");
    case Statistic::Maximum:
        return QStringLiteral("最高值");
    case Statistic::Mean:
        return QStringLiteral("平均值");
    case Statistic::Median:
        return QStringLiteral("中位数");
    }
    return QStringLiteral("未知");
}

bool validateOptions(const Options& options, QString* errorMessage)
{
    if (!std::isfinite(options.corridorWidth)
        || options.corridorWidth <= 0.0)
    {
        setError(errorMessage, QStringLiteral("剖面走廊宽度必须是正数。"));
        return false;
    }
    if (!std::isfinite(options.binSize) || options.binSize <= 0.0)
    {
        setError(errorMessage, QStringLiteral("剖面采样间隔必须是正数。"));
        return false;
    }
    if (!std::isfinite(options.horizontalUnitToMetre)
        || options.horizontalUnitToMetre <= 0.0)
    {
        setError(errorMessage, QStringLiteral("水平单位到米的换算系数无效。"));
        return false;
    }
    if (options.minimumPointsPerBin < 1
        || options.minimumPointsPerBin > 1'000'000)
    {
        setError(errorMessage, QStringLiteral("每个采样段的最少点数无效。"));
        return false;
    }
    if (options.maximumBinCount < 2
        || options.maximumBinCount > AbsoluteMaximumBinCount)
    {
        setError(errorMessage, QStringLiteral("剖面采样段数量上限无效。"));
        return false;
    }
    if (options.maximumSampleCount < 2
        || options.maximumSampleCount > AbsoluteMaximumSampleCount)
    {
        setError(errorMessage, QStringLiteral("剖面点数量上限无效。"));
        return false;
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

Result extract(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& cloud,
    const famp::cloud::SpatialReference& spatial,
    const famp::cloud::Point3d& localStart,
    const famp::cloud::Point3d& localEnd,
    const Options& options,
    const famp::tasks::CancellationCheck& shouldCancel,
    const Progress& reportProgress)
{
    Result result;
    result.corridorWidth = options.corridorWidth;
    result.binSize = options.binSize;
    result.horizontalUnitToMetre = options.horizontalUnitToMetre;
    result.statistic = options.statistic;
    result.minimumPointsPerBin = options.minimumPointsPerBin;
    result.sourcePointCount = cloud
        ? static_cast<quint64>(cloud->size()) : 0;

    QString validationError;
    if (!validateOptions(options, &validationError))
    {
        result.error = validationError;
        return result;
    }
    if (!cloud || cloud->size() < 2)
    {
        result.error = QStringLiteral("点云至少需要两个有效点才能生成剖面。");
        return result;
    }
    if (!finitePoint(localStart) || !finitePoint(localEnd))
    {
        result.error = QStringLiteral("剖面线端点包含非有限坐标。");
        return result;
    }
    if (cancelled(shouldCancel))
    {
        result.cancelled = true;
        return result;
    }
    if (!famp::cloud::localToReal(
            spatial, localStart, result.baseline.start, &result.error)
        || !famp::cloud::localToReal(
            spatial, localEnd, result.baseline.end, &result.error))
    {
        return result;
    }

    const double deltaX = result.baseline.end[0] - result.baseline.start[0];
    const double deltaY = result.baseline.end[1] - result.baseline.start[1];
    result.length = std::hypot(deltaX, deltaY);
    const double coordinateScale = std::max(
        {1.0,
         std::abs(result.baseline.start[0]),
         std::abs(result.baseline.start[1]),
         std::abs(result.baseline.end[0]),
         std::abs(result.baseline.end[1])});
    const double precisionFloor = coordinateScale
        * std::numeric_limits<double>::epsilon() * 64.0;
    if (!std::isfinite(result.length) || result.length <= precisionFloor)
    {
        result.error = QStringLiteral(
            "剖面线的水平长度为零或低于当前坐标精度，请重新拾取两个端点。");
        return result;
    }
    if (options.binSize <= precisionFloor)
    {
        result.error = QStringLiteral(
            "剖面采样间隔低于当前真实坐标可表达精度，请增大采样间隔。");
        return result;
    }

    const long double exactBinCount = std::ceil(
        static_cast<long double>(result.length)
        / static_cast<long double>(options.binSize));
    if (!std::isfinite(static_cast<double>(exactBinCount))
        || exactBinCount < 1.0L
        || exactBinCount
            > static_cast<long double>(options.maximumBinCount)
        || exactBinCount
            > static_cast<long double>(std::numeric_limits<int>::max()))
    {
        result.error = QStringLiteral(
            "剖面采样段过多，请增大采样间隔或缩短剖面线。");
        return result;
    }
    const int binCount = static_cast<int>(exactBinCount);
    result.bins.resize(binCount);
    std::vector<Accumulator> accumulators(
        static_cast<std::size_t>(binCount));
    for (int index = 0; index < binCount; ++index)
    {
        Bin& bin = result.bins[index];
        bin.index = index;
        bin.startStation = static_cast<double>(index) * options.binSize;
        bin.endStation = std::min(
            result.length,
            static_cast<double>(index + 1) * options.binSize);
        bin.centerStation = (bin.startStation + bin.endStation) * 0.5;
    }

    const double unitX = deltaX / result.length;
    const double unitY = deltaY / result.length;
    const double halfWidth = options.corridorWidth * 0.5;
    const double tolerance = std::max(
        precisionFloor, std::max(result.length, options.corridorWidth)
            * 1.0e-12);
    result.samples.reserve(static_cast<qsizetype>(std::min<quint64>(
        result.sourcePointCount, options.maximumSampleCount)));
    double minimumElevation = std::numeric_limits<double>::infinity();
    double maximumElevation = -std::numeric_limits<double>::infinity();

    for (std::size_t sourceIndex = 0;
         sourceIndex < cloud->size(); ++sourceIndex)
    {
        if ((sourceIndex & 4095U) == 0U)
        {
            if (cancelled(shouldCancel))
            {
                result.cancelled = true;
                result.samples.clear();
                result.bins.clear();
                return result;
            }
            if (reportProgress)
            {
                reportProgress(0.75
                    * static_cast<double>(sourceIndex)
                    / static_cast<double>(cloud->size()));
            }
        }

        const pcl::PointXYZRGB& point = cloud->at(sourceIndex);
        const famp::cloud::Point3d local{
            static_cast<double>(point.x),
            static_cast<double>(point.y),
            static_cast<double>(point.z)};
        famp::cloud::Point3d real;
        if (!finitePoint(local)
            || !famp::cloud::localToReal(spatial, local, real))
        {
            continue;
        }
        const double relativeX = real[0] - result.baseline.start[0];
        const double relativeY = real[1] - result.baseline.start[1];
        double station = relativeX * unitX + relativeY * unitY;
        const double signedOffset = unitX * relativeY - unitY * relativeX;
        if (station < -tolerance || station > result.length + tolerance
            || std::abs(signedOffset) > halfWidth + tolerance)
        {
            continue;
        }
        station = std::clamp(station, 0.0, result.length);
        int binIndex = static_cast<int>(std::floor(station / options.binSize));
        binIndex = std::clamp(binIndex, 0, binCount - 1);

        if (static_cast<quint64>(result.samples.size())
            >= options.maximumSampleCount)
        {
            result.error = QStringLiteral(
                "剖面走廊内点数超过安全上限，请减小走廊宽度或先对点云降采样。");
            result.samples.clear();
            result.bins.clear();
            return result;
        }
        Sample sample;
        sample.sourceIndex = static_cast<quint64>(sourceIndex);
        sample.binIndex = binIndex;
        sample.station = station;
        sample.signedOffset = signedOffset;
        sample.coordinate = real;
        result.samples.append(sample);

        Accumulator& accumulator = accumulators[static_cast<std::size_t>(
            binIndex)];
        ++accumulator.count;
        accumulator.sum += static_cast<long double>(real[2]);
        accumulator.minimum = std::min(accumulator.minimum, real[2]);
        accumulator.maximum = std::max(accumulator.maximum, real[2]);
        minimumElevation = std::min(minimumElevation, real[2]);
        maximumElevation = std::max(maximumElevation, real[2]);
    }

    if (result.samples.size() < 2)
    {
        result.error = QStringLiteral(
            "剖面走廊内有效点不足，请增大走廊宽度或重新拾取剖面线。");
        result.samples.clear();
        result.bins.clear();
        return result;
    }
    if (cancelled(shouldCancel))
    {
        result.cancelled = true;
        result.samples.clear();
        result.bins.clear();
        return result;
    }

    std::vector<std::size_t> offsets(
        static_cast<std::size_t>(binCount) + 1U, 0U);
    for (int index = 0; index < binCount; ++index)
    {
        offsets[static_cast<std::size_t>(index + 1)] =
            offsets[static_cast<std::size_t>(index)]
            + static_cast<std::size_t>(
                accumulators[static_cast<std::size_t>(index)].count);
    }
    std::vector<std::size_t> cursors = offsets;
    std::vector<double> elevations(
        static_cast<std::size_t>(result.samples.size()));
    for (const Sample& sample : result.samples)
    {
        const std::size_t bin = static_cast<std::size_t>(sample.binIndex);
        elevations[cursors[bin]++] = sample.coordinate[2];
    }

    int populatedBinCount = 0;
    for (int index = 0; index < binCount; ++index)
    {
        if ((static_cast<unsigned int>(index) & 1023U) == 0U)
        {
            if (cancelled(shouldCancel))
            {
                result.cancelled = true;
                result.samples.clear();
                result.bins.clear();
                return result;
            }
            if (reportProgress)
            {
                reportProgress(0.75 + 0.25
                    * static_cast<double>(index)
                    / static_cast<double>(binCount));
            }
        }
        const Accumulator& accumulator =
            accumulators[static_cast<std::size_t>(index)];
        Bin& bin = result.bins[index];
        bin.pointCount = accumulator.count;
        if (accumulator.count == 0)
            continue;

        bin.minimum = accumulator.minimum;
        bin.maximum = accumulator.maximum;
        bin.mean = static_cast<double>(
            accumulator.sum / static_cast<long double>(accumulator.count));
        if (!std::isfinite(bin.mean))
        {
            result.error = QStringLiteral(
                "剖面高程统计溢出，请检查点云真实坐标和空间变换。");
            result.samples.clear();
            result.bins.clear();
            return result;
        }
        const std::size_t begin = offsets[static_cast<std::size_t>(index)];
        const std::size_t end = offsets[static_cast<std::size_t>(index + 1)];
        std::sort(elevations.begin() + static_cast<std::ptrdiff_t>(begin),
                  elevations.begin() + static_cast<std::ptrdiff_t>(end));
        const std::size_t count = end - begin;
        const std::size_t middle = begin + count / 2U;
        bin.median = (count & 1U) != 0U
            ? elevations[middle]
            : (elevations[middle - 1U] + elevations[middle]) * 0.5;
        if (bin.pointCount >= options.minimumPointsPerBin)
        {
            bin.selected = selectedValue(bin, options.statistic);
            ++populatedBinCount;
        }
    }

    if (populatedBinCount < 2)
    {
        result.error = QStringLiteral(
            "满足最少点数要求的采样段不足两个，请减小采样间隔要求或增大走廊宽度。");
        result.samples.clear();
        result.bins.clear();
        return result;
    }

    result.selectedPointCount = static_cast<quint64>(result.samples.size());
    result.populatedBinCount = populatedBinCount;
    result.minimumElevation = minimumElevation;
    result.maximumElevation = maximumElevation;
    if (reportProgress)
        reportProgress(1.0);
    return result;
}
}
