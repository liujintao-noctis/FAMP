#include "CutFillAnalysis.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{
famp::terrain::Grid grid(
    int columns,
    int rows,
    double originX,
    double originY,
    double resolution,
    const QVector<double>& elevations,
    double unitToMetre = 1.0,
    const QString& crs = QStringLiteral("EPSG:4547"))
{
    famp::terrain::Grid result;
    result.columns = columns;
    result.rows = rows;
    result.originX = originX;
    result.originY = originY;
    result.resolution = resolution;
    result.horizontalUnitToMetre = unitToMetre;
    result.statistic = famp::terrain::CellStatistic::Median;
    result.sourcePointCount = elevations.size();
    result.sourceLayerId = QStringLiteral("layer-current");
    result.sourceLayerName = QStringLiteral("当前地表");
    result.sourceCrs = crs;
    result.horizontalUnitName = QStringLiteral("metre");
    result.elevations = elevations;
    for (double value : elevations)
    {
        if (std::isfinite(value))
            ++result.populatedCellCount;
    }
    return result;
}
}

TEST(CutFillAnalysisTest, ComputesConstantReferenceVolumesAndTolerance)
{
    auto current = grid(
        2, 2, 500000.0, 3400000.0, 2.0,
        {12.0, 8.0, 10.1,
         std::numeric_limits<double>::quiet_NaN()});
    famp::cutfill::Options options;
    options.referenceMode = famp::cutfill::ReferenceMode::ConstantElevation;
    options.referenceElevation = 10.0;
    options.zeroTolerance = 0.2;

    const auto result = famp::cutfill::compareToConstant(
        std::move(current), options);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_EQ(result.currentValidCellCount, 3U);
    EXPECT_EQ(result.currentNoDataCellCount, 1U);
    EXPECT_EQ(result.comparedCellCount, 3U);
    EXPECT_EQ(result.cutCellCount, 1U);
    EXPECT_EQ(result.fillCellCount, 1U);
    EXPECT_EQ(result.unchangedCellCount, 1U);
    EXPECT_DOUBLE_EQ(result.cellAreaSquareMetres, 4.0);
    EXPECT_DOUBLE_EQ(result.cutAreaSquareMetres, 4.0);
    EXPECT_DOUBLE_EQ(result.fillAreaSquareMetres, 4.0);
    EXPECT_NEAR(result.cutVolumeCubicMetres, 8.0, 1.0e-12);
    EXPECT_NEAR(result.fillVolumeCubicMetres, 8.0, 1.0e-12);
    EXPECT_NEAR(result.signedVolumeCubicMetres, 0.0, 1.0e-12);
    EXPECT_NEAR(result.minimumDifferenceMetres, -2.0, 1.0e-12);
    EXPECT_NEAR(result.maximumDifferenceMetres, 2.0, 1.0e-12);
    EXPECT_EQ(result.classificationAt(0), famp::cutfill::Classification::Cut);
    EXPECT_EQ(result.classificationAt(1), famp::cutfill::Classification::Fill);
    EXPECT_EQ(result.classificationAt(2),
              famp::cutfill::Classification::Unchanged);
    EXPECT_EQ(result.classificationAt(3),
              famp::cutfill::Classification::NoData);
    EXPECT_DOUBLE_EQ(result.referenceElevationAt(0), 10.0);
}

TEST(CutFillAnalysisTest, ConvertsAllThreeAxesToMetricVolume)
{
    const double feetToMetres = 0.3048;
    auto current = grid(
        1, 1, 0.0, 0.0, 1.0, {1.0}, feetToMetres,
        QStringLiteral("LOCAL:FEET"));
    famp::cutfill::Options options;
    options.referenceElevation = 0.0;
    const auto result = famp::cutfill::compareToConstant(
        std::move(current), options);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_NEAR(result.cellAreaSquareMetres,
                feetToMetres * feetToMetres, 1.0e-15);
    EXPECT_NEAR(result.cutVolumeCubicMetres,
                feetToMetres * feetToMetres * feetToMetres, 1.0e-15);
}

TEST(CutFillAnalysisTest, ComparesAlignedOverlappingDemGrids)
{
    auto current = grid(
        2, 2, 1.0, 0.0, 1.0,
        {12.0, 8.0, 9.0, 11.0});
    auto reference = grid(
        3, 2, 0.0, 0.0, 1.0,
        {7.0, 10.0, 10.0,
         7.0, std::numeric_limits<double>::quiet_NaN(), 10.0});
    reference.sourceLayerId = QStringLiteral("layer-reference");
    reference.sourceLayerName = QStringLiteral("历史地表");

    famp::cutfill::Options options;
    options.referenceMode = famp::cutfill::ReferenceMode::DemGrid;
    const auto result = famp::cutfill::compareToGrid(
        std::move(current), reference, options);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_EQ(result.comparedCellCount, 3U);
    EXPECT_EQ(result.missingReferenceCellCount, 1U);
    EXPECT_EQ(result.cutCellCount, 2U);
    EXPECT_EQ(result.fillCellCount, 1U);
    EXPECT_DOUBLE_EQ(result.cutVolumeCubicMetres, 3.0);
    EXPECT_DOUBLE_EQ(result.fillVolumeCubicMetres, 2.0);
    EXPECT_DOUBLE_EQ(result.signedVolumeCubicMetres, 1.0);
    EXPECT_EQ(result.referenceLayerId, QStringLiteral("layer-reference"));
    EXPECT_EQ(result.referenceLayerName, QStringLiteral("历史地表"));
    EXPECT_TRUE(std::isnan(result.differences.at(2)));
}

TEST(CutFillAnalysisTest, RejectsIncompatibleOrNonOverlappingReference)
{
    const auto current = grid(2, 2, 0.0, 0.0, 1.0,
                              {1.0, 1.0, 1.0, 1.0});
    qint64 columnOffset = 99;
    qint64 rowOffset = 99;
    QString error;

    auto reference = grid(2, 2, 0.5, 0.0, 1.0,
                          {0.0, 0.0, 0.0, 0.0});
    EXPECT_FALSE(famp::cutfill::validateAlignedReference(
        current, reference, &columnOffset, &rowOffset, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("原点")));
    EXPECT_EQ(columnOffset, 99);

    reference = grid(2, 2, 0.0, 0.0, 2.0,
                     {0.0, 0.0, 0.0, 0.0});
    EXPECT_FALSE(famp::cutfill::validateAlignedReference(
        current, reference, nullptr, nullptr, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("分辨率")));

    reference = grid(2, 2, 0.0, 0.0, 1.0,
                     {0.0, 0.0, 0.0, 0.0}, 1.0,
                     QStringLiteral("EPSG:3857"));
    EXPECT_FALSE(famp::cutfill::validateAlignedReference(
        current, reference, nullptr, nullptr, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("CRS")));

    reference = grid(2, 2, 10.0, 0.0, 1.0,
                     {0.0, 0.0, 0.0, 0.0});
    EXPECT_FALSE(famp::cutfill::validateAlignedReference(
        current, reference, nullptr, nullptr, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("不重叠")));
}

TEST(CutFillAnalysisTest, RejectsOversizedAndInvalidOptionsAtomically)
{
    auto current = grid(2, 2, 0.0, 0.0, 1.0,
                        {1.0, 1.0, 1.0, 1.0});
    famp::cutfill::Options options;
    options.maximumCellCount = 3;
    auto result = famp::cutfill::compareToConstant(current, options);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.error.contains(QStringLiteral("安全上限")));
    EXPECT_TRUE(result.differences.isEmpty());

    options.maximumCellCount = 100;
    options.zeroTolerance = -1.0;
    result = famp::cutfill::compareToConstant(std::move(current), options);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.error.contains(QStringLiteral("容差")));
}

TEST(CutFillAnalysisTest, CancelsWithoutPublishingPartialResult)
{
    auto current = grid(
        100, 100, 0.0, 0.0, 1.0,
        QVector<double>(10'000, 2.0));
    famp::cutfill::Options options;
    const auto result = famp::cutfill::compareToConstant(
        std::move(current), options, []() { return true; });
    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.error.contains(QStringLiteral("取消")));
    EXPECT_TRUE(result.differences.isEmpty());
    EXPECT_FALSE(result.currentGrid.isValid());
}

TEST(CutFillAnalysisTest, DetectsTamperedResultConsistency)
{
    auto current = grid(2, 1, 0.0, 0.0, 1.0, {2.0, -1.0});
    famp::cutfill::Options options;
    auto result = famp::cutfill::compareToConstant(
        std::move(current), options);
    ASSERT_TRUE(result.isValid());
    result.cutCellCount += 1;
    EXPECT_FALSE(result.isValid());

    result = famp::cutfill::compareToConstant(
        grid(1, 1, 0.0, 0.0, 1.0, {2.0}), options);
    ASSERT_TRUE(result.isValid());
    result.currentGrid.sourcePointCount = -1;
    EXPECT_FALSE(result.isValid());

    result = famp::cutfill::compareToConstant(
        grid(1, 1, 0.0, 0.0, 1.0, {2.0}), options);
    ASSERT_TRUE(result.isValid());
    result.cellAreaSquareMetres *= 2.0;
    result.cutAreaSquareMetres *= 2.0;
    result.cutVolumeCubicMetres *= 2.0;
    result.signedVolumeCubicMetres *= 2.0;
    EXPECT_FALSE(result.isValid());
}

TEST(CutFillAnalysisTest, ConstantReferenceRejectsMissingOrAlteredDifferences)
{
    auto current = grid(2, 1, 0.0, 0.0, 1.0, {2.0, -1.0});
    famp::cutfill::Options options;
    options.referenceElevation = 0.5;
    auto result = famp::cutfill::compareToConstant(
        std::move(current), options);
    ASSERT_TRUE(result.isValid());

    result.differences[0] =
        std::numeric_limits<double>::quiet_NaN();
    result.comparedCellCount -= 1;
    result.missingReferenceCellCount += 1;
    result.cutCellCount -= 1;
    result.cutAreaSquareMetres -= result.cellAreaSquareMetres;
    result.cutVolumeCubicMetres = 0.0;
    result.signedVolumeCubicMetres = -result.fillVolumeCubicMetres;
    result.maximumDifferenceMetres = result.minimumDifferenceMetres;
    EXPECT_FALSE(result.isValid());

    result = famp::cutfill::compareToConstant(
        grid(1, 1, 0.0, 0.0, 1.0, {2.0}), options);
    ASSERT_TRUE(result.isValid());
    result.constantReferenceElevation = 0.75;
    EXPECT_FALSE(result.isValid());
}

TEST(CutFillAnalysisTest, DetectsTamperedReferenceCrsMetadata)
{
    const auto current = grid(1, 1, 0.0, 0.0, 1.0, {2.0});
    auto reference = grid(1, 1, 0.0, 0.0, 1.0, {1.0});
    famp::cutfill::Options options;
    options.referenceMode = famp::cutfill::ReferenceMode::DemGrid;
    auto result = famp::cutfill::compareToGrid(
        current, reference, options);
    ASSERT_TRUE(result.isValid());
    EXPECT_EQ(result.referenceCrs, result.currentGrid.sourceCrs);

    result.referenceCrs = QStringLiteral("EPSG:3857");
    EXPECT_FALSE(result.isValid());
}
