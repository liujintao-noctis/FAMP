#include "TerrainAnalysis.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
QVector<famp::cloud::Point3d> regularSurface(
    int columns,
    int rows,
    double spacing,
    const std::function<double(double, double)>& elevation)
{
    QVector<famp::cloud::Point3d> points;
    points.reserve(columns * rows);
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            const double x = column * spacing;
            const double y = row * spacing;
            points.append({x, y, elevation(x, y)});
        }
    }
    return points;
}
}

TEST(TerrainAnalysisTest, SuggestsTwiceMedianHorizontalSpacing)
{
    const auto points = regularSurface(
        10, 10, 0.25, [](double, double) { return 1.0; });
    double resolution = 0.0;
    QString error;
    ASSERT_TRUE(famp::terrain::suggestResolution(
        points, 1.0, resolution, &error)) << error.toStdString();
    EXPECT_NEAR(resolution, 0.5, 1.0e-6);

    const QVector<famp::cloud::Point3d> millimetrePoints{
        {0.0, 0.0, 0.0}, {0.001, 0.0, 0.0},
        {0.0, 0.001, 0.0}, {0.001, 0.001, 0.0}};
    ASSERT_TRUE(famp::terrain::suggestResolution(
        millimetrePoints, 1.0, resolution, &error));
    EXPECT_DOUBLE_EQ(resolution, 0.01);

    QVector<famp::cloud::Point3d> stackedReturns;
    for (int duplicate = 0; duplicate < 20; ++duplicate)
    {
        stackedReturns.append({0.0, 0.0, static_cast<double>(duplicate)});
        stackedReturns.append({1.0, 0.0, static_cast<double>(duplicate)});
        stackedReturns.append({0.0, 1.0, static_cast<double>(duplicate)});
    }
    ASSERT_TRUE(famp::terrain::suggestResolution(
        stackedReturns, 1.0, resolution, &error)) << error.toStdString();
    EXPECT_NEAR(resolution, 2.0, 1.0e-6);
}

TEST(TerrainAnalysisTest, AggregatesMinimumMaximumMeanAndMedian)
{
    const QVector<famp::cloud::Point3d> points{
        {0.1, 0.1, 1.0}, {0.2, 0.2, 2.0},
        {0.3, 0.3, 100.0}, {1.1, 0.1, 7.0},
        {0.1, 1.1, 8.0}, {1.1, 1.1, 9.0}};
    famp::terrain::GridOptions options;
    options.automaticResolution = false;
    options.resolution = 1.0;
    QString error;
    famp::terrain::Grid grid;

    options.statistic = famp::terrain::CellStatistic::Minimum;
    ASSERT_TRUE(famp::terrain::buildGrid(
        points, options, grid, nullptr, &error)) << error.toStdString();
    EXPECT_DOUBLE_EQ(grid.value(0, 0), 1.0);

    options.statistic = famp::terrain::CellStatistic::Maximum;
    ASSERT_TRUE(famp::terrain::buildGrid(
        points, options, grid, nullptr, &error));
    EXPECT_DOUBLE_EQ(grid.value(0, 0), 100.0);

    options.statistic = famp::terrain::CellStatistic::Mean;
    ASSERT_TRUE(famp::terrain::buildGrid(
        points, options, grid, nullptr, &error));
    EXPECT_NEAR(grid.value(0, 0), 103.0 / 3.0, 1.0e-12);

    options.statistic = famp::terrain::CellStatistic::Median;
    ASSERT_TRUE(famp::terrain::buildGrid(
        points, options, grid, nullptr, &error));
    EXPECT_DOUBLE_EQ(grid.value(0, 0), 2.0);
    EXPECT_EQ(grid.columns, 2);
    EXPECT_EQ(grid.rows, 2);
    EXPECT_EQ(grid.populatedCellCount, 4);
    EXPECT_TRUE(grid.isValid());
}

TEST(TerrainAnalysisTest, FillsOnlySmallBoundedNoDataComponents)
{
    QVector<famp::cloud::Point3d> points;
    for (int row = 0; row < 5; ++row)
    {
        for (int column = 0; column < 5; ++column)
        {
            if (row == 2 && column == 2)
                continue;
            points.append({column + 0.1, row + 0.1,
                           static_cast<double>(row + column)});
        }
    }
    famp::terrain::GridOptions options;
    options.automaticResolution = false;
    options.resolution = 1.0;
    options.fillSmallHoles = true;
    options.maximumHoleCells = 3;
    famp::terrain::Grid grid;
    QString error;
    ASSERT_TRUE(famp::terrain::buildGrid(
        points, options, grid, nullptr, &error)) << error.toStdString();
    EXPECT_EQ(grid.filledCellCount, 1);
    EXPECT_TRUE(std::isfinite(grid.value(2, 2)));
    EXPECT_NEAR(grid.value(2, 2), 4.0, 1.0e-12);

    points.erase(std::remove_if(
        points.begin(), points.end(), [](const auto& point) {
            return point[0] > 0.0 && point[0] < 1.0
                && point[1] > 0.0 && point[1] < 1.0;
        }), points.end());
    ASSERT_TRUE(famp::terrain::buildGrid(
        points, options, grid, nullptr, &error));
    EXPECT_TRUE(std::isnan(grid.value(0, 0)));
}

TEST(TerrainAnalysisTest, GeneratesDeterministicContoursForSlopedGrid)
{
    const auto points = regularSurface(
        6, 6, 1.0, [](double x, double y) { return x + y; });
    famp::terrain::GridOptions gridOptions;
    gridOptions.automaticResolution = false;
    gridOptions.resolution = 1.0;
    famp::terrain::Grid grid;
    QString error;
    ASSERT_TRUE(famp::terrain::buildGrid(
        points, gridOptions, grid, nullptr, &error)) << error.toStdString();

    famp::terrain::ContourOptions contourOptions;
    contourOptions.automaticInterval = false;
    contourOptions.interval = 2.0;
    contourOptions.automaticBase = false;
    contourOptions.baseElevation = 0.0;
    contourOptions.smoothingIterations = 0;
    QVector<famp::terrain::ContourLine> contours;
    double interval = 0.0;
    double base = -1.0;
    ASSERT_TRUE(famp::terrain::generateContours(
        grid, contourOptions, contours, &interval, &base, &error))
        << error.toStdString();
    EXPECT_DOUBLE_EQ(interval, 2.0);
    EXPECT_DOUBLE_EQ(base, 0.0);
    ASSERT_FALSE(contours.isEmpty());
    EXPECT_TRUE(std::all_of(
        contours.cbegin(), contours.cend(), [](const auto& line) {
            if (line.points.size() < 2)
                return false;
            for (const auto& point : line.points)
            {
                if (std::abs((point[0] - 0.5) + (point[1] - 0.5)
                             - line.elevation) > 1.0e-9)
                {
                    return false;
                }
            }
            return true;
        }));
}

TEST(TerrainAnalysisTest, RejectsDegenerateOversizedAndInvalidInputsAtomically)
{
    famp::terrain::Grid output;
    output.columns = 7;
    QVector<famp::cloud::Point3d> points{
        {0.0, 0.0, 0.0}, {1.0, 0.0, 1.0}, {2.0, 0.0, 2.0}};
    famp::terrain::GridOptions options;
    options.automaticResolution = false;
    options.resolution = 1.0;
    QString error;
    EXPECT_FALSE(famp::terrain::buildGrid(
        points, options, output, nullptr, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("退化")));
    EXPECT_EQ(output.columns, 7);

    points = {{0.0, 0.0, 0.0}, {100.0, 0.0, 1.0},
              {0.0, 100.0, 2.0}, {100.0, 100.0, 3.0}};
    options.resolution = 0.01;
    options.maximumCellCount = 1000;
    EXPECT_FALSE(famp::terrain::buildGrid(
        points, options, output, nullptr, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("安全上限")));

    points[0][2] = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(famp::terrain::buildGrid(
        points, options, output, nullptr, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("无效坐标")));

    points = {{1.0e15 - 100.0, 1.0e15 - 100.0, 0.0},
              {1.0e15, 1.0e15 - 100.0, 1.0},
              {1.0e15 - 100.0, 1.0e15, 2.0},
              {1.0e15, 1.0e15, 3.0}};
    options.resolution = 0.01;
    options.maximumCellCount = 25'000'000;
    EXPECT_FALSE(famp::terrain::buildGrid(
        points, options, output, nullptr, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("双精度")));
}

TEST(TerrainAnalysisTest, CancelsWithoutPublishingPartialOutput)
{
    const auto points = regularSurface(
        100, 100, 0.1, [](double x, double y) { return x - y; });
    famp::terrain::GridOptions options;
    famp::terrain::Grid output;
    output.rows = 9;
    QString error;
    EXPECT_FALSE(famp::terrain::buildGrid(
        points, options, output, nullptr, &error,
        []() { return true; }));
    EXPECT_TRUE(error.contains(QStringLiteral("取消")));
    EXPECT_EQ(output.rows, 9);
}

TEST(TerrainAnalysisTest, AppliesDoublePrecisionSpatialReference)
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            pcl::PointXYZRGB point;
            point.x = static_cast<float>(column);
            point.y = static_cast<float>(row);
            point.z = static_cast<float>(column + row);
            cloud->push_back(point);
        }
    }
    famp::cloud::SpatialReference spatial;
    spatial.origin = {500000.0, 3400000.0, 20.0};
    famp::terrain::GridOptions gridOptions;
    gridOptions.automaticResolution = false;
    gridOptions.resolution = 1.0;
    famp::terrain::ContourOptions contourOptions;
    contourOptions.automaticInterval = false;
    contourOptions.interval = 1.0;
    contourOptions.smoothingIterations = 0;
    const auto result = famp::terrain::analyze(
        cloud, spatial, gridOptions, contourOptions);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_DOUBLE_EQ(result.grid.originX, 500000.0);
    EXPECT_DOUBLE_EQ(result.grid.originY, 3400000.0);
    EXPECT_DOUBLE_EQ(result.grid.value(0, 0), 20.0);
    EXPECT_EQ(result.grid.sourcePointCount, 16);
}
