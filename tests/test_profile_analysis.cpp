#include "ProfileAnalysis.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>

namespace
{
pcl::PointCloud<pcl::PointXYZRGB>::Ptr corridorCloud()
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (int bin = 0; bin < 5; ++bin)
    {
        for (int point = 0; point < 2; ++point)
        {
            pcl::PointXYZRGB value;
            value.x = static_cast<float>(bin * 2 + 0.5 + point);
            value.y = point == 0 ? -0.2f : 0.2f;
            value.z = static_cast<float>(bin + point * 2);
            cloud->push_back(value);
        }
    }
    pcl::PointXYZRGB outside;
    outside.x = 5.0f;
    outside.y = 2.0f;
    outside.z = -100.0f;
    cloud->push_back(outside);
    return cloud;
}

famp::cloud::SpatialReference shiftedSpatial()
{
    famp::cloud::SpatialReference spatial;
    spatial.origin = {500000.0, 3400000.0, 100.0};
    return spatial;
}

famp::profile::Options baseOptions()
{
    famp::profile::Options options;
    options.corridorWidth = 1.0;
    options.binSize = 2.0;
    options.statistic = famp::profile::Statistic::Median;
    return options;
}
}

TEST(ProfileAnalysisTest, ExtractsCorridorAndMedianBinsInRealCoordinates)
{
    const auto result = famp::profile::extract(
        corridorCloud(), shiftedSpatial(), {0.0, 0.0, 0.0},
        {10.0, 0.0, 0.0}, baseOptions());
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_DOUBLE_EQ(result.length, 10.0);
    EXPECT_EQ(result.sourcePointCount, 11U);
    EXPECT_EQ(result.selectedPointCount, 10U);
    ASSERT_EQ(result.bins.size(), 5);
    EXPECT_EQ(result.populatedBinCount, 5);
    EXPECT_DOUBLE_EQ(result.baseline.start[0], 500000.0);
    EXPECT_DOUBLE_EQ(result.baseline.start[1], 3400000.0);
    EXPECT_DOUBLE_EQ(result.bins.at(0).median, 101.0);
    EXPECT_DOUBLE_EQ(result.bins.at(4).median, 105.0);
    EXPECT_NEAR(result.samples.at(0).signedOffset, -0.2, 1.0e-6);
    EXPECT_NEAR(result.samples.at(1).signedOffset, 0.2, 1.0e-6);
}

TEST(ProfileAnalysisTest, SupportsAllRepresentativeStatistics)
{
    struct Expected
    {
        famp::profile::Statistic statistic;
        double value;
    };
    const Expected expectations[]{
        {famp::profile::Statistic::Minimum, 100.0},
        {famp::profile::Statistic::Maximum, 102.0},
        {famp::profile::Statistic::Mean, 101.0},
        {famp::profile::Statistic::Median, 101.0}};
    for (const Expected& expected : expectations)
    {
        auto options = baseOptions();
        options.statistic = expected.statistic;
        const auto result = famp::profile::extract(
            corridorCloud(), shiftedSpatial(), {0.0, 0.0, 0.0},
            {10.0, 0.0, 0.0}, options);
        ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
        EXPECT_DOUBLE_EQ(result.bins.front().selected, expected.value);
    }
}

TEST(ProfileAnalysisTest, UsesFullSpatialTransformForBaselineAndSamples)
{
    famp::cloud::SpatialReference spatial;
    spatial.origin = {1000.0, 2000.0, 10.0};
    spatial.transform = {
        0.0, -1.0, 0.0, 50.0,
        1.0,  0.0, 0.0, 75.0,
        0.0,  0.0, 1.0,  5.0,
        0.0,  0.0, 0.0,  1.0};
    const auto result = famp::profile::extract(
        corridorCloud(), spatial, {0.0, 0.0, 0.0},
        {10.0, 0.0, 0.0}, baseOptions());
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_DOUBLE_EQ(result.baseline.start[0], -1950.0);
    EXPECT_DOUBLE_EQ(result.baseline.start[1], 1075.0);
    EXPECT_DOUBLE_EQ(result.baseline.start[2], 15.0);
    EXPECT_DOUBLE_EQ(result.length, 10.0);
    EXPECT_NEAR(result.samples.front().coordinate[0], -1949.8, 1.0e-5);
    EXPECT_NEAR(result.samples.front().coordinate[1], 1075.5, 1.0e-5);
}

TEST(ProfileAnalysisTest, AppliesMinimumPointCountAndPreservesGaps)
{
    auto options = baseOptions();
    options.binSize = 1.0;
    options.minimumPointsPerBin = 2;
    const auto result = famp::profile::extract(
        corridorCloud(), shiftedSpatial(), {0.0, 0.0, 0.0},
        {10.0, 0.0, 0.0}, options);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.error.contains(QStringLiteral("采样段")));

    options.binSize = 2.0;
    const auto valid = famp::profile::extract(
        corridorCloud(), shiftedSpatial(), {0.0, 0.0, 0.0},
        {10.0, 0.0, 0.0}, options);
    ASSERT_TRUE(valid.succeeded()) << valid.error.toStdString();
    EXPECT_EQ(valid.populatedBinCount, 5);
}

TEST(ProfileAnalysisTest, KeepsInteriorEmptyBinsAsProfileBreaks)
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (const float x : {0.25f, 0.75f, 2.25f, 2.75f})
    {
        pcl::PointXYZRGB point;
        point.x = x;
        point.y = 0.0f;
        point.z = x;
        cloud->push_back(point);
    }
    auto options = baseOptions();
    options.binSize = 1.0;
    const auto result = famp::profile::extract(
        cloud, {}, {0.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, options);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    ASSERT_EQ(result.bins.size(), 3);
    EXPECT_TRUE(result.bins.at(0).hasSelectedValue());
    EXPECT_FALSE(result.bins.at(1).hasSelectedValue());
    EXPECT_TRUE(result.bins.at(2).hasSelectedValue());
    EXPECT_EQ(result.populatedBinCount, 2);
}

TEST(ProfileAnalysisTest, RejectsUnsafeBinAndSampleCountsAtomically)
{
    auto options = baseOptions();
    options.binSize = 0.001;
    options.maximumBinCount = 100;
    auto result = famp::profile::extract(
        corridorCloud(), shiftedSpatial(), {0.0, 0.0, 0.0},
        {10.0, 0.0, 0.0}, options);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.error.contains(QStringLiteral("采样段过多")));
    EXPECT_TRUE(result.bins.isEmpty());

    options = baseOptions();
    options.maximumSampleCount = 4;
    result = famp::profile::extract(
        corridorCloud(), shiftedSpatial(), {0.0, 0.0, 0.0},
        {10.0, 0.0, 0.0}, options);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.error.contains(QStringLiteral("安全上限")));
    EXPECT_TRUE(result.samples.isEmpty());
}

TEST(ProfileAnalysisTest, RejectsDegenerateBaselineAndSupportsCancellation)
{
    auto result = famp::profile::extract(
        corridorCloud(), shiftedSpatial(), {1.0, 1.0, 0.0},
        {1.0, 1.0, 2.0}, baseOptions());
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.error.contains(QStringLiteral("水平长度")));

    std::atomic_bool cancel{true};
    result = famp::profile::extract(
        corridorCloud(), shiftedSpatial(), {0.0, 0.0, 0.0},
        {10.0, 0.0, 0.0}, baseOptions(),
        [&cancel]() { return cancel.load(); });
    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.samples.isEmpty());
}
