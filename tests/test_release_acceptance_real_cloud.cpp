#include "CloudAttributes.h"
#include "CloudCrop.h"
#include "CloudLoader.h"
#include "CloudProcessing.h"
#include "CloudProjection.h"
#include "CloudRegistration.h"
#include "CutFillAnalysis.h"
#include "ProfileAnalysis.h"
#include "PlaneWidgetPlacement.h"
#include "TerrainAnalysis.h"
#include "MyGraphicsView.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <Eigen/Geometry>
#include <pcl/common/transforms.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#ifndef FAMP_ACCEPTANCE_CLOUD_PATH
#error "FAMP_ACCEPTANCE_CLOUD_PATH must identify the optional real acceptance cloud"
#endif

namespace
{
QString acceptanceCloudPath()
{
    const QString overridePath = qEnvironmentVariable(
        "FAMP_ACCEPTANCE_CLOUD");
    return overridePath.trimmed().isEmpty()
        ? QString::fromUtf8(FAMP_ACCEPTANCE_CLOUD_PATH)
        : QFileInfo(overridePath).absoluteFilePath();
}

const famp::cloud::LoadResult& acceptanceCloud()
{
    static const famp::cloud::LoadResult loaded =
        famp::cloud::load(acceptanceCloudPath());
    return loaded;
}

bool realCloudAvailable()
{
    return QFileInfo::exists(acceptanceCloudPath());
}

double horizontalSpan(const famp::crop::Options& bounds)
{
    return std::max(bounds.maximumX - bounds.minimumX,
                    bounds.maximumY - bounds.minimumY);
}

double placementEdgeLength(const std::array<double, 3>& left,
                           const std::array<double, 3>& right)
{
    double squared = 0.0;
    for (std::size_t axis = 0; axis < left.size(); ++axis)
    {
        const double delta = left[axis] - right[axis];
        squared += delta * delta;
    }
    return std::sqrt(squared);
}

pcl::PointCloud<pcl::PointXYZRGB>::Ptr uniformlySampledRealCloud(
    const pcl::PointCloud<pcl::PointXYZRGB>& input,
    std::size_t maximumPoints)
{
    auto output = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    const std::size_t stride = std::max<std::size_t>(
        1, (input.size() + maximumPoints - 1) / maximumPoints);
    output->reserve((input.size() + stride - 1) / stride);
    for (std::size_t index = 0; index < input.size(); index += stride)
        output->push_back(input[index]);
    output->width = static_cast<std::uint32_t>(output->size());
    output->height = 1;
    output->is_dense = true;
    return output;
}
}

TEST(ReleaseAcceptanceRealCloudTest, LoadsUnicodePcdAndRestoresLocalFrame)
{
    if (!realCloudAvailable())
        GTEST_SKIP() << "real acceptance cloud is not present";
    const auto& loaded = acceptanceCloud();
    ASSERT_TRUE(loaded.succeeded()) << loaded.error.toStdString();
    ASSERT_TRUE(loaded.sourceWasPcd);
    ASSERT_TRUE(loaded.sourceCloud);
    ASSERT_TRUE(loaded.displayCloud);
    EXPECT_EQ(loaded.path, QFileInfo(acceptanceCloudPath()).absoluteFilePath());
    EXPECT_EQ(loaded.displayCloud->size(), 25'550U);
    EXPECT_EQ(loaded.sourceCloud->size(), loaded.displayCloud->size());
    EXPECT_TRUE(loaded.attributes.validate(
        static_cast<qint64>(loaded.displayCloud->size())));

    famp::crop::Options bounds;
    QString error;
    ASSERT_TRUE(famp::crop::dataBounds(
        loaded.displayCloud, bounds, &error)) << error.toStdString();
    EXPECT_GT(bounds.maximumX, bounds.minimumX);
    EXPECT_GT(bounds.maximumY, bounds.minimumY);
    EXPECT_GT(bounds.maximumZ, bounds.minimumZ);
    EXPECT_TRUE(std::isfinite(loaded.spatial.origin[0]));
    EXPECT_TRUE(std::isfinite(loaded.spatial.origin[1]));
    EXPECT_TRUE(std::isfinite(loaded.spatial.origin[2]));
}

TEST(ReleaseAcceptanceRealCloudTest, FitsClipPlaneToMeasuredCloudBounds)
{
    if (!realCloudAvailable())
        GTEST_SKIP() << "real acceptance cloud is not present";
    const auto& loaded = acceptanceCloud();
    ASSERT_TRUE(loaded.succeeded()) << loaded.error.toStdString();

    famp::crop::Options bounds;
    QString error;
    ASSERT_TRUE(famp::crop::dataBounds(
        loaded.displayCloud, bounds, &error)) << error.toStdString();
    const auto placement = famp::viewport::fitPlaneWidgetToCloud(
        *loaded.displayCloud,
        {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0});
    ASSERT_TRUE(placement.has_value());

    EXPECT_NEAR(
        placementEdgeLength(placement->origin, placement->point1),
        (bounds.maximumX - bounds.minimumX) * 1.05,
        1e-5);
    EXPECT_NEAR(
        placementEdgeLength(placement->origin, placement->point2),
        (bounds.maximumY - bounds.minimumY) * 1.05,
        1e-5);
}

TEST(ReleaseAcceptanceRealCloudTest,
     CreatesMemoryOnlyPlanAndProfileProjectionPreviews)
{
    if (!realCloudAvailable())
        GTEST_SKIP() << "real acceptance cloud is not present";
    const auto& loaded = acceptanceCloud();
    ASSERT_TRUE(loaded.succeeded()) << loaded.error.toStdString();

    QTemporaryDir untouchedDirectory;
    ASSERT_TRUE(untouchedDirectory.isValid());
    const QStringList before = QDir(untouchedDirectory.path()).entryList(
        QDir::Files | QDir::NoDotAndDotDot);

    const auto plan = famp::projection::projectToMinimumPlane(
        loaded.displayCloud, famp::projection::Plane::Overlook);
    const auto profile = famp::projection::projectToMinimumPlane(
        loaded.displayCloud, famp::projection::Plane::XOZ);
    ASSERT_TRUE(plan.succeeded()) << plan.error.toStdString();
    ASSERT_TRUE(profile.succeeded()) << profile.error.toStdString();
    ASSERT_EQ(plan.points->size(), loaded.displayCloud->size());
    ASSERT_EQ(profile.points->size(), loaded.displayCloud->size());

    for (std::size_t index = 0; index < loaded.displayCloud->size(); ++index)
    {
        EXPECT_FLOAT_EQ(plan.points->at(index).z,
                        static_cast<float>(plan.origin[2]));
        EXPECT_FLOAT_EQ(profile.points->at(index).y,
                        static_cast<float>(profile.origin[1]));
        EXPECT_EQ(plan.points->at(index).rgba,
                  loaded.displayCloud->at(index).rgba);
        EXPECT_EQ(profile.points->at(index).rgba,
                  loaded.displayCloud->at(index).rgba);
    }
    EXPECT_EQ(QDir(untouchedDirectory.path()).entryList(
                  QDir::Files | QDir::NoDotAndDotDot),
              before);
}

TEST(ReleaseAcceptanceRealCloudTest,
     BuildsAlignedThreeViewDrawingsAtEveryMapScale)
{
    if (!realCloudAvailable())
        GTEST_SKIP() << "real acceptance cloud is not present";
    const auto& loaded = acceptanceCloud();
    ASSERT_TRUE(loaded.succeeded()) << loaded.error.toStdString();
    const auto sampled = uniformlySampledRealCloud(
        *loaded.displayCloud, 1'500);
    ASSERT_GT(sampled->size(), 500U);

    MyGraphicsView view(nullptr);
    for (famp::projection::Plane plane : {
             famp::projection::Plane::Overlook,
             famp::projection::Plane::XOZ,
             famp::projection::Plane::YOZ})
    {
        const auto projection = famp::projection::projectToMinimumPlane(
            sampled, plane);
        ASSERT_TRUE(projection.succeeded())
            << projection.error.toStdString();
        QString error;
        view.getDBItemCloud(sampled);
        ASSERT_TRUE(view.setProjectionInput(
            projection.points, plane, &error)) << error.toStdString();
        view.slotOn_actProjLine_triggered();
        ASSERT_TRUE(view.hasProjectionDrawing(plane));
    }

    for (int scaleIndex = 0; scaleIndex < 4; ++scaleIndex)
    {
        view.getScaleComBoxCurrentIndexChanged(scaleIndex);
        const QRectF overlook = view.projectionDrawingSceneBounds(
            famp::projection::Plane::Overlook);
        const QRectF xoz = view.projectionDrawingSceneBounds(
            famp::projection::Plane::XOZ);
        const QRectF yoz = view.projectionDrawingSceneBounds(
            famp::projection::Plane::YOZ);
        ASSERT_TRUE(overlook.isValid());
        ASSERT_TRUE(xoz.isValid());
        ASSERT_TRUE(yoz.isValid());
        EXPECT_LT(yoz.bottom(), overlook.top());
        EXPECT_GT(xoz.left(), overlook.right());
        EXPECT_FALSE(overlook.intersects(xoz));
        EXPECT_FALSE(overlook.intersects(yoz));
        EXPECT_FALSE(xoz.intersects(yoz));
        EXPECT_NEAR(overlook.center().x(), 0.0, 0.01);
        EXPECT_NEAR(overlook.center().y(), 0.0, 0.01);
        EXPECT_TRUE(view.drawingSceneRect().adjusted(
            -0.5, -0.5, 0.5, 0.5).contains(
                view.projectionLayoutSceneBounds()));

        const QRectF xozSection = view.sectionCutLineSceneBounds(
            famp::projection::Plane::XOZ);
        const QRectF yozSection = view.sectionCutLineSceneBounds(
            famp::projection::Plane::YOZ);
        ASSERT_TRUE(xozSection.isValid());
        ASSERT_TRUE(yozSection.isValid());
        EXPECT_NEAR(xoz.center().y(), xozSection.center().y(), 0.01);
        EXPECT_NEAR(yoz.center().x(), yozSection.center().x(), 0.01);
    }

    for (const int rotation : {90, 180, 270})
    {
        ASSERT_TRUE(view.setOrthographicRotationDegrees(rotation));
        const QRectF overlook = view.projectionDrawingSceneBounds(
            famp::projection::Plane::Overlook);
        const QRectF xoz = view.projectionDrawingSceneBounds(
            famp::projection::Plane::XOZ);
        const QRectF yoz = view.projectionDrawingSceneBounds(
            famp::projection::Plane::YOZ);
        EXPECT_NEAR(overlook.center().x(), 0.0, 0.01);
        EXPECT_NEAR(overlook.center().y(), 0.0, 0.01);
        EXPECT_FALSE(overlook.intersects(xoz));
        EXPECT_FALSE(overlook.intersects(yoz));
        EXPECT_FALSE(xoz.intersects(yoz));
        EXPECT_LT(yoz.bottom(), overlook.top());
        EXPECT_GT(xoz.left(), overlook.right());
        const QRectF xozSection = view.sectionCutLineSceneBounds(
            famp::projection::Plane::XOZ);
        const QRectF yozSection = view.sectionCutLineSceneBounds(
            famp::projection::Plane::YOZ);
        ASSERT_TRUE(xozSection.isValid());
        ASSERT_TRUE(yozSection.isValid());
        EXPECT_NEAR(xoz.center().y(), xozSection.center().y(), 0.01);
        EXPECT_NEAR(yoz.center().x(), yozSection.center().x(), 0.01);
    }
}

TEST(ReleaseAcceptanceRealCloudTest,
     RunsMemoryOnlyPreprocessCropAndAttributeMapping)
{
    if (!realCloudAvailable())
        GTEST_SKIP() << "real acceptance cloud is not present";
    const auto& loaded = acceptanceCloud();
    ASSERT_TRUE(loaded.succeeded()) << loaded.error.toStdString();
    QTemporaryDir untouchedDirectory;
    ASSERT_TRUE(untouchedDirectory.isValid());

    famp::crop::Options bounds;
    QString error;
    ASSERT_TRUE(famp::crop::dataBounds(
        loaded.displayCloud, bounds, &error)) << error.toStdString();
    const double span = horizontalSpan(bounds);
    ASSERT_GT(span, 0.0);

    famp::processing::Options voxelOptions;
    voxelOptions.method = famp::processing::Method::VoxelDownsample;
    voxelOptions.voxelLeafSizeMeters = std::max(0.001, span / 100.0);
    const auto voxel = famp::processing::process(
        loaded.displayCloud, voxelOptions);
    ASSERT_TRUE(voxel.succeeded()) << voxel.error.toStdString();
    EXPECT_LT(voxel.outputPointCount, voxel.inputPointCount);
    EXPECT_EQ(voxel.sourceIndices.size(),
              static_cast<qsizetype>(voxel.outputPointCount));

    famp::processing::Options noiseOptions;
    noiseOptions.method =
        famp::processing::Method::StatisticalOutlierRemoval;
    noiseOptions.meanNeighbors = 20;
    noiseOptions.standardDeviationMultiplier = 1.0;
    const auto filtered = famp::processing::process(
        loaded.displayCloud, noiseOptions);
    ASSERT_TRUE(filtered.succeeded()) << filtered.error.toStdString();
    EXPECT_LE(filtered.outputPointCount, filtered.inputPointCount);

    famp::crop::Options cropOptions = bounds;
    const double centerX = (bounds.minimumX + bounds.maximumX) * 0.5;
    const double centerY = (bounds.minimumY + bounds.maximumY) * 0.5;
    const double halfX = (bounds.maximumX - bounds.minimumX) * 0.35;
    const double halfY = (bounds.maximumY - bounds.minimumY) * 0.35;
    cropOptions.minimumX = centerX - halfX;
    cropOptions.maximumX = centerX + halfX;
    cropOptions.minimumY = centerY - halfY;
    cropOptions.maximumY = centerY + halfY;
    const auto cropped = famp::crop::process(
        loaded.displayCloud, cropOptions);
    ASSERT_TRUE(cropped.succeeded()) << cropped.error.toStdString();
    EXPECT_LT(cropped.outputPointCount, cropped.inputPointCount);
    EXPECT_EQ(cropped.sourceIndices.size(),
              static_cast<qsizetype>(cropped.outputPointCount));

    famp::cloud::CloudAttributes attributes;
    famp::cloud::AttributeChannel sourceIndex;
    sourceIndex.name = QStringLiteral("source_index");
    sourceIndex.unit = QStringLiteral("id");
    sourceIndex.type = famp::cloud::AttributeValueType::UnsignedInteger;
    sourceIndex.unsignedValues.reserve(
        static_cast<qsizetype>(loaded.displayCloud->size()));
    for (std::size_t index = 0; index < loaded.displayCloud->size(); ++index)
        sourceIndex.unsignedValues.append(static_cast<quint64>(index));
    ASSERT_TRUE(attributes.insert(
        std::move(sourceIndex),
        static_cast<qint64>(loaded.displayCloud->size()), &error))
        << error.toStdString();
    famp::cloud::CloudAttributes croppedAttributes;
    ASSERT_TRUE(attributes.select(
        cropped.sourceIndices, croppedAttributes, &error))
        << error.toStdString();
    const auto* selected = croppedAttributes.channel(
        QStringLiteral("source_index"));
    ASSERT_NE(selected, nullptr);
    ASSERT_EQ(selected->unsignedValues.size(), cropped.sourceIndices.size());
    for (qsizetype index = 0; index < selected->unsignedValues.size(); ++index)
    {
        EXPECT_EQ(selected->unsignedValues.at(index),
                  static_cast<quint64>(cropped.sourceIndices.at(index)));
    }

    EXPECT_TRUE(QDir(untouchedDirectory.path()).entryList(
        QDir::Files | QDir::NoDotAndDotDot).isEmpty());
}

TEST(ReleaseAcceptanceRealCloudTest, AlignsSimulatedRigidVariantWithIcp)
{
    if (!realCloudAvailable())
        GTEST_SKIP() << "real acceptance cloud is not present";
    const auto& loaded = acceptanceCloud();
    ASSERT_TRUE(loaded.succeeded()) << loaded.error.toStdString();
    const auto source = uniformlySampledRealCloud(
        *loaded.displayCloud, 4'500);
    ASSERT_GT(source->size(), 1'000U);

    Eigen::Affine3f known = Eigen::Affine3f::Identity();
    known.translate(Eigen::Vector3f(0.08F, -0.05F, 0.03F));
    known.rotate(Eigen::AngleAxisf(0.01F, Eigen::Vector3f::UnitZ()));
    auto target = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    pcl::transformPointCloud(*source, *target, known.matrix());

    famp::crop::Options bounds;
    QString error;
    ASSERT_TRUE(famp::crop::dataBounds(source, bounds, &error))
        << error.toStdString();
    famp::registration::Options options;
    options.maximumIterations = 100;
    options.maximumCorrespondenceDistance = std::max(
        0.25, horizontalSpan(bounds) / 20.0);
    options.transformationEpsilon = 1.0e-10;
    options.fitnessEpsilon = 1.0e-10;
    const auto aligned = famp::registration::align(
        source, target, options);
    ASSERT_TRUE(aligned.succeeded()) << aligned.error.toStdString();
    ASSERT_EQ(aligned.cloud->size(), target->size());
    ASSERT_EQ(aligned.sourceIndices.size(),
              static_cast<qsizetype>(source->size()));

    long double squaredError = 0.0L;
    for (std::size_t index = 0; index < target->size(); ++index)
    {
        const auto& actual = aligned.cloud->at(index);
        const auto& expected = target->at(index);
        const long double dx = actual.x - expected.x;
        const long double dy = actual.y - expected.y;
        const long double dz = actual.z - expected.z;
        squaredError += dx * dx + dy * dy + dz * dz;
        EXPECT_EQ(actual.r, source->at(index).r);
        EXPECT_EQ(actual.g, source->at(index).g);
        EXPECT_EQ(actual.b, source->at(index).b);
    }
    const double rmse = std::sqrt(static_cast<double>(
        squaredError / static_cast<long double>(target->size())));
    EXPECT_LT(rmse, 0.02);
    EXPECT_NEAR(aligned.transform(0, 3), known.matrix()(0, 3), 0.02);
    EXPECT_NEAR(aligned.transform(1, 3), known.matrix()(1, 3), 0.02);
    EXPECT_NEAR(aligned.transform(2, 3), known.matrix()(2, 3), 0.02);
}

TEST(ReleaseAcceptanceRealCloudTest,
     RejectsLowOverlapAndBadInitialVariantsWithIcpQualityGate)
{
    if (!realCloudAvailable())
        GTEST_SKIP() << "real acceptance cloud is not present";
    const auto& loaded = acceptanceCloud();
    ASSERT_TRUE(loaded.succeeded()) << loaded.error.toStdString();
    const auto base = uniformlySampledRealCloud(
        *loaded.displayCloud, 1'000);
    ASSERT_GT(base->size(), 500U);
    famp::crop::Options bounds;
    QString error;
    ASSERT_TRUE(famp::crop::dataBounds(base, bounds, &error))
        << error.toStdString();
    const float clusterSeparation = static_cast<float>(
        std::max(10.0, horizontalSpan(bounds) * 4.0));

    auto partialSource = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    partialSource->reserve(base->size() * 5);
    for (int cluster = 0; cluster < 5; ++cluster)
    {
        for (const pcl::PointXYZRGB& basePoint : base->points)
        {
            pcl::PointXYZRGB point = basePoint;
            point.x += static_cast<float>(cluster) * clusterSeparation;
            partialSource->push_back(point);
        }
    }
    partialSource->width = static_cast<std::uint32_t>(partialSource->size());
    partialSource->height = 1;
    partialSource->is_dense = true;

    Eigen::Affine3f smallTranslation = Eigen::Affine3f::Identity();
    smallTranslation.translate(Eigen::Vector3f(0.03F, -0.02F, 0.01F));
    auto partialTarget = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    pcl::transformPointCloud(
        *base, *partialTarget, smallTranslation.matrix());
    partialTarget->width = static_cast<std::uint32_t>(partialTarget->size());
    partialTarget->height = 1;
    partialTarget->is_dense = true;

    famp::registration::Options options;
    options.maximumCorrespondenceDistance = std::max(
        0.15, horizontalSpan(bounds) / 100.0);
    options.minimumOverlapRatio = 0.25;
    const auto rejectedPartial = famp::registration::align(
        partialSource, partialTarget, options);
    EXPECT_FALSE(rejectedPartial.succeeded());
    EXPECT_NEAR(rejectedPartial.sourceOverlapRatio, 0.2, 0.01);
    EXPECT_DOUBLE_EQ(rejectedPartial.targetOverlapRatio, 1.0);
    EXPECT_NE(rejectedPartial.error.indexOf(QStringLiteral("重叠率")), -1);

    options.minimumOverlapRatio = 0.15;
    const auto acceptedPartial = famp::registration::align(
        partialSource, partialTarget, options);
    ASSERT_TRUE(acceptedPartial.succeeded())
        << acceptedPartial.error.toStdString();
    EXPECT_NEAR(acceptedPartial.overlapRatio, 0.2, 0.01);

    Eigen::Affine3f badInitialPose = Eigen::Affine3f::Identity();
    badInitialPose.translate(Eigen::Vector3f(
        clusterSeparation * 2.0F, -clusterSeparation, 3.0F));
    badInitialPose.rotate(
        Eigen::AngleAxisf(0.5F, Eigen::Vector3f::UnitZ()));
    auto separatedTarget = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    pcl::transformPointCloud(
        *base, *separatedTarget, badInitialPose.matrix());
    options.maximumCorrespondenceDistance = 0.05;
    options.minimumOverlapRatio = 0.25;
    const auto rejectedPose = famp::registration::align(
        base, separatedTarget, options);
    EXPECT_FALSE(rejectedPose.succeeded());
    EXPECT_FALSE(rejectedPose.error.isEmpty());
}

TEST(ReleaseAcceptanceRealCloudTest, RunsTerrainProfileAndCutFillInMemory)
{
    if (!realCloudAvailable())
        GTEST_SKIP() << "real acceptance cloud is not present";
    const auto& loaded = acceptanceCloud();
    ASSERT_TRUE(loaded.succeeded()) << loaded.error.toStdString();
    QTemporaryDir untouchedDirectory;
    ASSERT_TRUE(untouchedDirectory.isValid());

    famp::crop::Options bounds;
    QString error;
    ASSERT_TRUE(famp::crop::dataBounds(
        loaded.displayCloud, bounds, &error)) << error.toStdString();
    const double span = horizontalSpan(bounds);
    ASSERT_GT(span, 0.0);

    famp::terrain::GridOptions gridOptions;
    gridOptions.automaticResolution = false;
    gridOptions.resolution = std::max(0.001, span / 110.0);
    gridOptions.horizontalUnitToMetre = 1.0;
    gridOptions.statistic = famp::terrain::CellStatistic::Median;
    gridOptions.fillSmallHoles = true;
    gridOptions.maximumHoleCells = 3;
    gridOptions.maximumCellCount = 1'000'000;
    famp::terrain::ContourOptions contourOptions;
    contourOptions.automaticInterval = true;
    contourOptions.automaticBase = true;
    contourOptions.smoothingIterations = 1;
    contourOptions.maximumSegmentCount = 1'000'000;
    auto terrain = famp::terrain::analyze(
        loaded.displayCloud, loaded.spatial,
        gridOptions, contourOptions);
    ASSERT_TRUE(terrain.succeeded()) << terrain.error.toStdString();
    EXPECT_GT(terrain.grid.populatedCellCount, 0);
    EXPECT_GT(terrain.grid.columns, 1);
    EXPECT_GT(terrain.grid.rows, 1);
    EXPECT_FALSE(terrain.contours.isEmpty());

    famp::profile::Options profileOptions;
    profileOptions.corridorWidth = std::max(0.01, span / 5.0);
    profileOptions.binSize = std::max(0.005, span / 100.0);
    profileOptions.horizontalUnitToMetre = 1.0;
    profileOptions.statistic = famp::profile::Statistic::Median;
    profileOptions.minimumPointsPerBin = 1;
    const famp::cloud::Point3d profileStart{
        bounds.minimumX, bounds.minimumY, 0.0};
    const famp::cloud::Point3d profileEnd{
        bounds.maximumX, bounds.maximumY, 0.0};
    auto profile = famp::profile::extract(
        loaded.displayCloud, loaded.spatial,
        profileStart, profileEnd, profileOptions);
    ASSERT_TRUE(profile.succeeded()) << profile.error.toStdString();
    EXPECT_GT(profile.selectedPointCount, 0U);
    EXPECT_GT(profile.populatedBinCount, 0);
    EXPECT_GT(profile.length, 0.0);

    double minimumElevation = std::numeric_limits<double>::infinity();
    double maximumElevation = -std::numeric_limits<double>::infinity();
    for (double elevation : terrain.grid.elevations)
    {
        if (!std::isfinite(elevation))
            continue;
        minimumElevation = std::min(minimumElevation, elevation);
        maximumElevation = std::max(maximumElevation, elevation);
    }
    ASSERT_TRUE(std::isfinite(minimumElevation));
    ASSERT_GT(maximumElevation, minimumElevation);
    famp::cutfill::Options cutFillOptions;
    cutFillOptions.referenceMode =
        famp::cutfill::ReferenceMode::ConstantElevation;
    cutFillOptions.referenceElevation =
        (minimumElevation + maximumElevation) * 0.5;
    cutFillOptions.zeroTolerance = 0.0;
    auto cutFill = famp::cutfill::compareToConstant(
        terrain.grid, cutFillOptions);
    ASSERT_TRUE(cutFill.succeeded()) << cutFill.error.toStdString();
    EXPECT_EQ(cutFill.cutCellCount + cutFill.fillCellCount
                  + cutFill.unchangedCellCount,
              cutFill.comparedCellCount);
    EXPECT_GT(cutFill.cutCellCount, 0U);
    EXPECT_GT(cutFill.fillCellCount, 0U);
    EXPECT_TRUE(std::isfinite(cutFill.cutVolumeCubicMetres));
    EXPECT_TRUE(std::isfinite(cutFill.fillVolumeCubicMetres));
    EXPECT_TRUE(std::isfinite(cutFill.signedVolumeCubicMetres));

    EXPECT_TRUE(QDir(untouchedDirectory.path()).entryList(
        QDir::Files | QDir::NoDotAndDotDot).isEmpty());
}
