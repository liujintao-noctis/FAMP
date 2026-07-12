#include "CloudRegistration.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <limits>

namespace
{
pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudWithTranslation(float dx,
                                                             float dy,
                                                             float dz)
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    const float points[][3] = {
        {0.0F, 0.0F, 0.0F}, {0.7F, 0.1F, 0.2F}, {0.2F, 1.1F, 0.4F},
        {1.3F, 0.4F, 0.8F}, {0.4F, 0.6F, 1.5F}, {1.7F, 1.2F, 0.3F},
        {0.9F, 1.8F, 1.1F}, {2.2F, 0.7F, 1.7F}, {1.4F, 2.1F, 2.0F}};
    for (const auto& xyz : points)
    {
        pcl::PointXYZRGB point;
        point.x = xyz[0] + dx;
        point.y = xyz[1] + dy;
        point.z = xyz[2] + dz;
        point.r = 20;
        point.g = 80;
        point.b = 140;
        cloud->push_back(point);
    }
    cloud->width = static_cast<std::uint32_t>(cloud->size());
    cloud->height = 1;
    cloud->is_dense = true;
    return cloud;
}
}

TEST(CloudRegistrationTest, AlignsTranslatedAsymmetricCloud)
{
    const auto source = cloudWithTranslation(0.0F, 0.0F, 0.0F);
    const auto target = cloudWithTranslation(0.18F, -0.12F, 0.09F);
    famp::registration::Options options;
    options.maximumCorrespondenceDistance = 0.8;
    const auto result = famp::registration::align(source, target, options);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_LT(result.fitnessScore, 1.0e-8);
    EXPECT_NEAR(result.transform(0, 3), 0.18F, 1.0e-3F);
    EXPECT_NEAR(result.transform(1, 3), -0.12F, 1.0e-3F);
    EXPECT_NEAR(result.transform(2, 3), 0.09F, 1.0e-3F);
    ASSERT_EQ(result.cloud->size(), target->size());
    for (std::size_t index = 0; index < target->size(); ++index)
    {
        EXPECT_NEAR((*result.cloud)[index].x, (*target)[index].x, 1.0e-3F);
        EXPECT_NEAR((*result.cloud)[index].y, (*target)[index].y, 1.0e-3F);
        EXPECT_NEAR((*result.cloud)[index].z, (*target)[index].z, 1.0e-3F);
    }
}

TEST(CloudRegistrationTest, SupportsVoxelSamplingAndRejectsNoOverlap)
{
    const auto source = cloudWithTranslation(0.0F, 0.0F, 0.0F);
    const auto target = cloudWithTranslation(0.18F, -0.12F, 0.09F);
    famp::registration::Options options;
    options.maximumCorrespondenceDistance = 0.8;
    options.samplingVoxelSizeMeters = 0.2;
    const auto sampled = famp::registration::align(source, target, options);
    ASSERT_TRUE(sampled.succeeded()) << sampled.error.toStdString();
    EXPECT_GT(sampled.registrationSourcePointCount, 2U);
    EXPECT_LE(sampled.registrationSourcePointCount, source->size());

    options.maximumCorrespondenceDistance = 0.05;
    options.samplingVoxelSizeMeters = 0.0;
    const auto separated = famp::registration::align(
        source, cloudWithTranslation(100.0F, 100.0F, 100.0F), options);
    EXPECT_FALSE(separated.succeeded());
    EXPECT_FALSE(separated.error.isEmpty());
}

TEST(CloudRegistrationTest, RejectsInvalidInputsAndOptions)
{
    const auto source = cloudWithTranslation(0.0F, 0.0F, 0.0F);
    famp::registration::Options options;
    options.maximumIterations = 0;
    EXPECT_FALSE(famp::registration::align(source, source, options).succeeded());
    auto tiny = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    tiny->resize(2);
    options.maximumIterations = 10;
    EXPECT_FALSE(famp::registration::align(tiny, source, options).succeeded());
}

TEST(CloudRegistrationTest, CancelsWithoutWritingOutput)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString output = directory.filePath(QStringLiteral("cancelled.pcd"));
    const auto result = famp::registration::alignAndSave(
        cloudWithTranslation(0.0F, 0.0F, 0.0F),
        cloudWithTranslation(0.1F, 0.0F, 0.0F),
        {}, output, []() { return true; });
    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(QFile::exists(output));
}

TEST(CloudRegistrationTest, SavesUnicodePcdAtomically)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString output = directory.filePath(QStringLiteral("配准结果"));
    const auto result = famp::registration::alignAndSave(
        cloudWithTranslation(0.0F, 0.0F, 0.0F),
        cloudWithTranslation(0.1F, -0.05F, 0.02F), {}, output);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_TRUE(QFile::exists(output + QStringLiteral(".pcd")));
    EXPECT_EQ(QDir(directory.path()).entryList(QStringList() << QStringLiteral("*.tmp"),
                                              QDir::Files).size(), 0);
}
