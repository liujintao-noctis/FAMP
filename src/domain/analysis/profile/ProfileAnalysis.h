#pragma once

#include "CloudCoordinates.h"
#include "TaskCancellation.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <QString>
#include <QVector>

#include <functional>
#include <limits>

namespace famp::profile
{
enum class Statistic
{
    Minimum,
    Maximum,
    Mean,
    Median
};

struct Options
{
    // All distances are expressed in the source projected coordinate unit.
    double corridorWidth = 1.0;
    double binSize = 0.25;
    double horizontalUnitToMetre = 1.0;
    Statistic statistic = Statistic::Median;
    int minimumPointsPerBin = 1;
    quint64 maximumBinCount = 250'000;
    quint64 maximumSampleCount = 2'000'000;
};

struct Baseline
{
    famp::cloud::Point3d start{0.0, 0.0, 0.0};
    famp::cloud::Point3d end{0.0, 0.0, 0.0};
};

struct Sample
{
    quint64 sourceIndex = 0;
    int binIndex = -1;
    double station = 0.0;
    double signedOffset = 0.0;
    famp::cloud::Point3d coordinate{0.0, 0.0, 0.0};
};

struct Bin
{
    int index = 0;
    double startStation = 0.0;
    double endStation = 0.0;
    double centerStation = 0.0;
    int pointCount = 0;
    double minimum = std::numeric_limits<double>::quiet_NaN();
    double maximum = std::numeric_limits<double>::quiet_NaN();
    double mean = std::numeric_limits<double>::quiet_NaN();
    double median = std::numeric_limits<double>::quiet_NaN();
    double selected = std::numeric_limits<double>::quiet_NaN();

    bool hasSelectedValue() const;
};

struct Result
{
    Baseline baseline;
    double length = 0.0;
    double corridorWidth = 0.0;
    double binSize = 0.0;
    double horizontalUnitToMetre = 1.0;
    Statistic statistic = Statistic::Median;
    int minimumPointsPerBin = 1;
    quint64 sourcePointCount = 0;
    quint64 selectedPointCount = 0;
    int populatedBinCount = 0;
    double minimumElevation = std::numeric_limits<double>::quiet_NaN();
    double maximumElevation = std::numeric_limits<double>::quiet_NaN();
    QString sourceLayerId;
    QString sourceLayerName;
    QString sourcePath;
    QString sourceCrs;
    QString horizontalUnitName;
    QVector<Sample> samples;
    QVector<Bin> bins;
    QString error;
    bool cancelled = false;

    bool isValid(
        const famp::tasks::CancellationCheck& shouldCancel = {}) const;
    bool succeeded() const;
};

using Progress = std::function<void(double)>;

QString statisticName(Statistic statistic);
bool validateOptions(const Options& options,
                     QString* errorMessage = nullptr);

Result extract(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& cloud,
    const famp::cloud::SpatialReference& spatial,
    const famp::cloud::Point3d& localStart,
    const famp::cloud::Point3d& localEnd,
    const Options& options,
    const famp::tasks::CancellationCheck& shouldCancel = {},
    const Progress& reportProgress = {});
}
