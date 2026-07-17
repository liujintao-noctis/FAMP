#include "CutFillDialog.h"
#include "CutFillIO.h"
#include "TerrainIO.h"

#include <gtest/gtest.h>

#include <QTemporaryDir>

namespace
{
pcl::PointCloud<pcl::PointXYZRGB>::Ptr currentSurface()
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            pcl::PointXYZRGB point;
            point.x = static_cast<float>(column) + 0.1f;
            point.y = static_cast<float>(row) + 0.1f;
            point.z = row < 2 ? 12.0f : 8.0f;
            cloud->push_back(point);
        }
    }
    return cloud;
}

famp::terrain::Grid referenceSurface()
{
    famp::terrain::Grid grid;
    grid.columns = 4;
    grid.rows = 4;
    grid.originX = 500000.0;
    grid.originY = 3400000.0;
    grid.resolution = 1.0;
    grid.horizontalUnitToMetre = 1.0;
    grid.statistic = famp::terrain::CellStatistic::Median;
    grid.sourcePointCount = 16;
    grid.populatedCellCount = 16;
    grid.sourceLayerId = QStringLiteral("reference-layer");
    grid.sourceLayerName = QStringLiteral("历史地表");
    grid.sourceCrs = QStringLiteral("EPSG:4547");
    grid.horizontalUnitName = QStringLiteral("米");
    grid.elevations.fill(10.0, 16);
    return grid;
}
}

TEST(CutFillWorkflowTest, RunsReferenceDemPipelineAndPersistsResult)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString referencePath = directory.filePath(
        QStringLiteral("reference.famp-dem"));
    QString error;
    ASSERT_TRUE(famp::terrainio::saveGridAtomically(
        referencePath, referenceSurface(), &error))
        << error.toStdString();

    famp::terrain::Grid loadedReference;
    ASSERT_TRUE(famp::terrainio::loadGrid(
        referencePath, loadedReference, &error))
        << error.toStdString();

    famp::cutfillui::Options options;
    options.analysis.referenceMode =
        famp::cutfill::ReferenceMode::DemGrid;
    options.referenceDemPath = referencePath;
    options.sidecarPath = directory.filePath(
        QStringLiteral("volume.famp-volume"));
    ASSERT_TRUE(famp::cutfillui::applyReferenceGrid(
        loadedReference, QStringLiteral("EPSG:4547"), 1.0,
        options, &error)) << error.toStdString();
    EXPECT_DOUBLE_EQ(options.grid.resolution, 1.0);

    famp::cloud::SpatialReference spatial;
    spatial.origin = {500000.0, 3400000.0, 0.0};
    famp::terrain::Grid currentGrid;
    ASSERT_TRUE(famp::terrain::buildGridFromCloud(
        currentSurface(), spatial, options.grid, currentGrid,
        nullptr, &error)) << error.toStdString();
    currentGrid.sourceLayerId = QStringLiteral("current-layer");
    currentGrid.sourceLayerName = QStringLiteral("当前地表");
    currentGrid.sourceCrs = QStringLiteral("EPSG:4547");
    currentGrid.horizontalUnitName = QStringLiteral("米");

    auto result = famp::cutfill::compareToGrid(
        std::move(currentGrid), loadedReference, options.analysis);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    result.referencePath = referencePath;
    EXPECT_EQ(result.cutCellCount, 8U);
    EXPECT_EQ(result.fillCellCount, 8U);
    EXPECT_DOUBLE_EQ(result.cutVolumeCubicMetres, 16.0);
    EXPECT_DOUBLE_EQ(result.fillVolumeCubicMetres, 16.0);
    EXPECT_DOUBLE_EQ(result.signedVolumeCubicMetres, 0.0);

    ASSERT_TRUE(famp::cutfillio::saveResultAtomically(
        options.sidecarPath, result, &error)) << error.toStdString();
    famp::cutfill::Result reloaded;
    ASSERT_TRUE(famp::cutfillio::loadResult(
        options.sidecarPath, reloaded, &error)) << error.toStdString();
    EXPECT_TRUE(reloaded.succeeded()) << reloaded.error.toStdString();
    EXPECT_EQ(reloaded.referencePath, referencePath);
    EXPECT_DOUBLE_EQ(reloaded.cutVolumeCubicMetres, 16.0);
    EXPECT_DOUBLE_EQ(reloaded.fillVolumeCubicMetres, 16.0);
}
