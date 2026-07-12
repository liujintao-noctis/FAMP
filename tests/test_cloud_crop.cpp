#include <gtest/gtest.h>

#include <QFileInfo>
#include <QTemporaryDir>

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
    EXPECT_DOUBLE_EQ(bounds.minimumX, -2.0);
    EXPECT_DOUBLE_EQ(bounds.maximumX, 2.0);
    EXPECT_LT(bounds.minimumZ, 5.0);
    EXPECT_GT(bounds.maximumZ, 5.0);
}

TEST(CloudCropTest, KeepsInsideOrOutsideInclusiveBounds)
{
    const auto cloud = sampleCloud();
    famp::crop::Options options;
    auto inside = famp::crop::process(cloud, options);
    ASSERT_TRUE(inside.succeeded()) << inside.error.toStdString();
    EXPECT_EQ(inside.outputPointCount, 3U);

    options.keepInside = false;
    auto outside = famp::crop::process(cloud, options);
    ASSERT_TRUE(outside.succeeded()) << outside.error.toStdString();
    EXPECT_EQ(outside.outputPointCount, 2U);
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
