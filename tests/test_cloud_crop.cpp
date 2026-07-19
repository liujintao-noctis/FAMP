#include <gtest/gtest.h>

#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <limits>

#include "CloudCrop.h"

namespace
{
pcl::PointCloud<pcl::PointXYZRGB>::Ptr sampleCloud()
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (int value = -2; value <= 2; ++value)
    {
        pcl::PointXYZRGB point;
        point.x = static_cast<float>(value);
        point.y = static_cast<float>(value);
        point.z = static_cast<float>(value);
        cloud->push_back(point);
    }
    pcl::PointXYZRGB invalid;
    invalid.x = std::numeric_limits<float>::quiet_NaN();
    cloud->push_back(invalid);
    return cloud;
}
}

TEST(CloudCropTest, ComputesFiniteBoundsAndExpandsFlatAxes)
{
    auto cloud = sampleCloud();
    for (pcl::PointXYZRGB& point : cloud->points)
        point.z = 5.0f;
    famp::crop::Options bounds;
    QString error;
    ASSERT_TRUE(famp::crop::dataBounds(cloud, bounds, &error));
    EXPECT_LT(bounds.minimumX, -2.0);
    EXPECT_GT(bounds.maximumX, 2.0);
    EXPECT_LT(bounds.minimumZ, 5.0);
    EXPECT_GT(bounds.maximumZ, 5.0);
}

TEST(CloudCropTest, EditableRoundedBoundsKeepEveryFiniteExtremum)
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (const float value : {-2.4337337F, -0.1234568F, 4.5118008F})
    {
        pcl::PointXYZRGB point;
        point.x = value;
        point.y = value * 0.37F;
        point.z = value * -0.21F;
        cloud->push_back(point);
    }

    famp::crop::Options bounds;
    QString error;
    ASSERT_TRUE(famp::crop::dataBounds(cloud, bounds, &error));
    const auto roundForEditor = [](double value) {
        constexpr double scale = 1.0e9;
        return std::round(value * scale) / scale;
    };
    bounds.minimumX = roundForEditor(bounds.minimumX);
    bounds.maximumX = roundForEditor(bounds.maximumX);
    bounds.minimumY = roundForEditor(bounds.minimumY);
    bounds.maximumY = roundForEditor(bounds.maximumY);
    bounds.minimumZ = roundForEditor(bounds.minimumZ);
    bounds.maximumZ = roundForEditor(bounds.maximumZ);

    const auto result = famp::crop::process(cloud, bounds);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_EQ(result.outputPointCount, cloud->size());
    EXPECT_EQ(result.sourceIndices, QVector<qint64>({0, 1, 2}));
}

TEST(CloudCropTest, KeepsInsideOrOutsideInclusiveBounds)
{
    const auto cloud = sampleCloud();
    famp::crop::Options options;
    auto inside = famp::crop::process(cloud, options);
    ASSERT_TRUE(inside.succeeded()) << inside.error.toStdString();
    EXPECT_EQ(inside.outputPointCount, 3U);
    EXPECT_EQ(inside.sourceIndices, QVector<qint64>({1, 2, 3}));

    options.keepInside = false;
    auto outside = famp::crop::process(cloud, options);
    ASSERT_TRUE(outside.succeeded()) << outside.error.toStdString();
    EXPECT_EQ(outside.outputPointCount, 2U);
    EXPECT_EQ(outside.sourceIndices, QVector<qint64>({0, 4}));
}

TEST(CloudCropTest, RejectsInvalidRangeAndEmptyResult)
{
    famp::crop::Options options;
    options.minimumX = options.maximumX;
    QString error;
    EXPECT_FALSE(famp::crop::validateOptions(options, &error));
    EXPECT_FALSE(error.isEmpty());

    options.minimumX = 10.0;
    options.maximumX = 20.0;
    const auto result = famp::crop::process(sampleCloud(), options);
    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(result.error.isEmpty());
}

TEST(CloudCropTest, CancelsWithoutWritingOutput)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString output = directory.filePath(QStringLiteral("取消裁剪.pcd"));
    famp::crop::Options options;
    const auto result = famp::crop::processAndSave(
        sampleCloud(), options, output, []() { return true; });
    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(QFileInfo::exists(output));
}
