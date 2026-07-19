#include "CloudProjection.h"

#include <gtest/gtest.h>

#include <limits>

namespace
{
pcl::PointCloud<pcl::PointXYZRGB>::Ptr sampleCloud()
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (int index = 0; index < 12; ++index)
    {
        pcl::PointXYZRGB point;
        point.x = -3.0F + static_cast<float>(index) * 0.5F;
        point.y = 8.0F - static_cast<float>(index) * 0.25F;
        point.z = -1.5F + static_cast<float>(index % 5) * 0.75F;
        point.r = static_cast<std::uint8_t>(index * 3);
        point.g = static_cast<std::uint8_t>(index * 5);
        point.b = static_cast<std::uint8_t>(index * 7);
        cloud->push_back(point);
    }
    return cloud;
}
}

TEST(CloudProjectionTest, FlattensRequestedAxisAndPreservesPointIdentity)
{
    const auto source = sampleCloud();
    struct Expectation
    {
        famp::projection::Plane plane;
        int flatAxis;
        double flatValue;
    };
    const Expectation expectations[] = {
        {famp::projection::Plane::YOZ, 0, -3.0},
        {famp::projection::Plane::XOZ, 1, 5.25},
        {famp::projection::Plane::XOY, 2, -1.5},
        {famp::projection::Plane::Overlook, 2, -1.5}};

    for (const Expectation& expectation : expectations)
    {
        const auto result = famp::projection::projectToMinimumPlane(
            source, expectation.plane);
        ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
        ASSERT_EQ(result.points->size(), source->size());
        EXPECT_NE(result.points.get(), source.get());
        for (std::size_t index = 0; index < source->size(); ++index)
        {
            const auto& before = source->at(index);
            const auto& after = result.points->at(index);
            const float coordinates[] = {after.x, after.y, after.z};
            EXPECT_FLOAT_EQ(
                coordinates[expectation.flatAxis],
                static_cast<float>(expectation.flatValue));
            if (expectation.flatAxis != 0)
                EXPECT_FLOAT_EQ(after.x, before.x);
            if (expectation.flatAxis != 1)
                EXPECT_FLOAT_EQ(after.y, before.y);
            if (expectation.flatAxis != 2)
                EXPECT_FLOAT_EQ(after.z, before.z);
            EXPECT_EQ(after.r, before.r);
            EXPECT_EQ(after.g, before.g);
            EXPECT_EQ(after.b, before.b);
        }
    }
}

TEST(CloudProjectionTest, RejectsEmptyAndNonFiniteSources)
{
    pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr empty;
    EXPECT_FALSE(famp::projection::projectToMinimumPlane(
        empty, famp::projection::Plane::XOY).succeeded());

    auto invalid = sampleCloud();
    invalid->at(3).z = std::numeric_limits<float>::infinity();
    const auto result = famp::projection::projectToMinimumPlane(
        invalid, famp::projection::Plane::XOY);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.error.contains(QStringLiteral("非有限")));
}

TEST(ProjectionWorkflowTest, KeepsPreviewOnlyForTheSameSource)
{
    famp::projection::Workflow workflow;
    const auto source = sampleCloud();
    const auto projected = famp::projection::projectToMinimumPlane(
        source, famp::projection::Plane::XOY);
    ASSERT_TRUE(projected.succeeded());

    const QUuid firstId = QUuid::createUuid();
    QString error;
    ASSERT_TRUE(workflow.selectSource(
        firstId, QStringLiteral("大墓坑"), source, &error));
    ASSERT_TRUE(workflow.setPreview(
        projected.points, famp::projection::Plane::XOY, &error));
    EXPECT_TRUE(workflow.hasPreview());

    ASSERT_TRUE(workflow.selectSource(
        firstId, QStringLiteral("大墓坑-重命名"), source, &error));
    ASSERT_TRUE(workflow.hasPreview());
    EXPECT_EQ(workflow.preview()->source.name,
              QStringLiteral("大墓坑-重命名"));

    auto replacementData = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>(*source));
    ASSERT_TRUE(workflow.selectSource(
        firstId, QStringLiteral("大墓坑-更新"), replacementData,
        &error));
    EXPECT_FALSE(workflow.hasPreview());
    ASSERT_TRUE(workflow.setPreview(
        projected.points, famp::projection::Plane::XOY, &error));

    ASSERT_TRUE(workflow.selectSource(
        QUuid::createUuid(), QStringLiteral("切割成果"), source, &error));
    EXPECT_FALSE(workflow.hasPreview());
}

TEST(ProjectionWorkflowTest, RestoresExplicitPlaneMetadata)
{
    EXPECT_EQ(famp::projection::planeFromMetadata(
                  QStringLiteral("xoz"), false),
              famp::projection::Plane::XOZ);
    EXPECT_EQ(famp::projection::planeFromMetadata(
                  QStringLiteral("XOY"), true),
              famp::projection::Plane::Overlook);
    EXPECT_FALSE(famp::projection::planeFromMetadata(
        QStringLiteral("YOZ"), true).has_value());
}
