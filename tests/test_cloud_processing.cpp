#include <gtest/gtest.h>

#include <QFileInfo>
#include <QTemporaryDir>

#include <limits>

#include "CloudProcessing.h"
#include "PcdLoader.h"

namespace
{
pcl::PointCloud<pcl::PointXYZRGB>::Ptr clusteredCloud()
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (int x = 0; x < 10; ++x)
    {
        for (int y = 0; y < 10; ++y)
        {
            pcl::PointXYZRGB point;
            point.x = static_cast<float>(x) * 0.01f;
            point.y = static_cast<float>(y) * 0.01f;
            point.z = 0.0f;
            point.r = 100;
            point.g = 150;
            point.b = 200;
            cloud->push_back(point);
        }
    }
    pcl::PointXYZRGB outlier;
    outlier.x = 50.0f;
    outlier.y = 50.0f;
    outlier.z = 50.0f;
    cloud->push_back(outlier);
    return cloud;
}
}

TEST(CloudProcessingTest, VoxelDownsampleReducesPointsAndRemovesNonFiniteInput)
{
    auto cloud = clusteredCloud();
    pcl::PointXYZRGB invalid;
    invalid.x = std::numeric_limits<float>::quiet_NaN();
    cloud->push_back(invalid);

    famp::processing::Options options;
    options.method = famp::processing::Method::VoxelDownsample;
    options.voxelLeafSizeMeters = 0.05;
    const famp::processing::Result result =
        famp::processing::process(cloud, options);

    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_EQ(result.inputPointCount, 102U);
    EXPECT_EQ(result.finitePointCount, 101U);
    EXPECT_LT(result.outputPointCount, result.finitePointCount);
    EXPECT_TRUE(result.cloud->is_dense);
}

TEST(CloudProcessingTest, StatisticalFilterRemovesDistantOutlier)
{
    const auto cloud = clusteredCloud();
    famp::processing::Options options;
    options.method = famp::processing::Method::StatisticalOutlierRemoval;
    options.meanNeighbors = 10;
    options.standardDeviationMultiplier = 1.0;
    const famp::processing::Result result =
        famp::processing::process(cloud, options);

    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_LT(result.outputPointCount, result.inputPointCount);
    for (const pcl::PointXYZRGB& point : result.cloud->points)
        EXPECT_LT(point.x, 1.0f);
}

TEST(CloudProcessingTest, RejectsInvalidOptions)
{
    famp::processing::Options options;
    options.voxelLeafSizeMeters = 0.0;
    QString error;
    EXPECT_FALSE(famp::processing::validateOptions(options, 10, &error));
    EXPECT_FALSE(error.isEmpty());

    options.method = famp::processing::Method::StatisticalOutlierRemoval;
    options.meanNeighbors = 10;
    EXPECT_FALSE(famp::processing::validateOptions(options, 10, &error));
}

TEST(CloudProcessingTest, ProcessesAndAtomicallySavesUnicodePcd)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString requestedPath = directory.filePath(QStringLiteral("探方降采样"));

    famp::processing::Options options;
    options.voxelLeafSizeMeters = 0.05;
    const famp::processing::Result result = famp::processing::processAndSave(
        clusteredCloud(), options, requestedPath);

    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_TRUE(result.outputPath.endsWith(QStringLiteral(".pcd")));
    EXPECT_TRUE(QFileInfo::exists(result.outputPath));

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr loaded;
    QString loadError;
    ASSERT_TRUE(loadPcdAsRgb(result.outputPath, loaded, &loadError))
        << loadError.toStdString();
    EXPECT_EQ(loaded->size(), result.outputPointCount);
}
