#pragma once

#include "CloudCoordinates.h"
#include "TaskCancellation.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <QString>
#include <QVector>

#include <array>
#include <functional>

namespace famp::terrain
{
enum class CellStatistic
{
    Minimum,
    Maximum,
    Mean,
    Median
};

struct GridOptions
{
    bool automaticResolution = true;
    double resolution = 0.1;
    double horizontalUnitToMetre = 1.0;
    CellStatistic statistic = CellStatistic::Median;
    bool fillSmallHoles = false;
    int maximumHoleCells = 3;
    quint64 maximumCellCount = 25'000'000;
};

struct ContourOptions
{
    bool automaticInterval = true;
    double interval = 1.0;
    bool automaticBase = true;
    double baseElevation = 0.0;
    int smoothingIterations = 1;
    quint64 maximumSegmentCount = 5'000'000;
};

struct Grid
{
    int columns = 0;
    int rows = 0;
    double originX = 0.0;
    double originY = 0.0;
    double resolution = 0.0;
    double horizontalUnitToMetre = 1.0;
    CellStatistic statistic = CellStatistic::Median;
    int sourcePointCount = 0;
    int populatedCellCount = 0;
    int filledCellCount = 0;
    QString sourceLayerId;
    QString sourceLayerName;
    QString sourceCrs;
    QString horizontalUnitName;
    QVector<double> elevations;

    bool isValid() const;
    qsizetype index(int row, int column) const;
    double value(int row, int column) const;
    std::array<double, 2> cellCenter(int row, int column) const;
};

struct ContourLine
{
    double elevation = 0.0;
    QVector<std::array<double, 2>> points;
};

struct Result
{
    Grid grid;
    QVector<ContourLine> contours;
    double suggestedResolution = 0.0;
    double contourInterval = 0.0;
    double contourBase = 0.0;
    QString error;
    bool cancelled = false;

    bool succeeded() const
    {
        return grid.isValid() && error.isEmpty() && !cancelled;
    }
};

using Progress = std::function<void(double)>;

QString statisticName(CellStatistic statistic);
bool validateGridOptions(const GridOptions& options,
                         QString* errorMessage = nullptr);
bool validateContourOptions(const ContourOptions& options,
                            QString* errorMessage = nullptr);

bool suggestResolution(
    const QVector<famp::cloud::Point3d>& coordinates,
    double horizontalUnitToMetre,
    double& resolution,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {});

bool buildGrid(
    const QVector<famp::cloud::Point3d>& coordinates,
    const GridOptions& options,
    Grid& grid,
    double* suggestedResolution = nullptr,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {},
    const Progress& reportProgress = {});

bool buildGridFromCloud(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& cloud,
    const famp::cloud::SpatialReference& spatial,
    const GridOptions& options,
    Grid& grid,
    double* suggestedResolution = nullptr,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {},
    const Progress& reportProgress = {});

bool generateContours(
    const Grid& grid,
    const ContourOptions& options,
    QVector<ContourLine>& contours,
    double* usedInterval = nullptr,
    double* usedBase = nullptr,
    QString* errorMessage = nullptr,
    const famp::tasks::CancellationCheck& shouldCancel = {},
    const Progress& reportProgress = {});

Result analyze(
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr& cloud,
    const famp::cloud::SpatialReference& spatial,
    const GridOptions& gridOptions,
    const ContourOptions& contourOptions,
    const famp::tasks::CancellationCheck& shouldCancel = {},
    const Progress& reportProgress = {});
}
