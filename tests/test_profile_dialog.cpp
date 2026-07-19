#include "ProfileDialog.h"

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QLabel>
#include <QTemporaryDir>

TEST(ProfileDialogTest, DerivesStableSiblingExportPaths)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto paths = famp::profileui::derivedExportPaths(
        directory.filePath(QStringLiteral("探方 剖面.famp-profile")));
    EXPECT_TRUE(paths.sidecar.endsWith(
        QStringLiteral("探方 剖面.famp-profile")));
    EXPECT_TRUE(paths.binsCsv.endsWith(
        QStringLiteral("探方 剖面_bins.csv")));
    EXPECT_TRUE(paths.samplesCsv.endsWith(
        QStringLiteral("探方 剖面_points.csv")));
    EXPECT_TRUE(paths.svg.endsWith(QStringLiteral("探方 剖面.svg")));
}

TEST(ProfileDialogTest, ValidatesOutputDirectoryAndAnalysisOptions)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    famp::profileui::Options options;
    options.sidecarPath = directory.filePath(QStringLiteral("profile"));
    QString error;
    EXPECT_TRUE(famp::profileui::validateOptions(options, &error))
        << error.toStdString();

    options.analysis.corridorWidth = 0.0;
    EXPECT_FALSE(famp::profileui::validateOptions(options, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("走廊")));

    options.analysis.corridorWidth = 1.0;
    options.sidecarPath = directory.filePath(
        QStringLiteral("missing/profile.famp-profile"));
    EXPECT_FALSE(famp::profileui::validateOptions(options, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("目录")));
}

TEST(ProfileDialogTest, ConvertsMetreInputsToProjectedUnits)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    famp::profile::Baseline baseline;
    baseline.start = {500000.0, 3400000.0, 20.0};
    baseline.end = {500100.0, 3400000.0, 21.0};
    famp::profileui::ProfileDialog dialog(
        QStringLiteral("layer"), QStringLiteral("EPSG:1234"),
        QStringLiteral("foot"), 0.3048, baseline,
        directory.filePath(QStringLiteral("profile.famp-profile")));
    const auto options = dialog.options();
    EXPECT_DOUBLE_EQ(options.analysis.horizontalUnitToMetre, 0.3048);
    EXPECT_GT(options.analysis.corridorWidth, 0.0);
    EXPECT_GT(options.analysis.binSize, 0.0);
    EXPECT_TRUE(options.exportBinsCsv);
    EXPECT_TRUE(options.exportSvg);
    const auto* saveImmediately = dialog.findChild<QCheckBox*>(
        QStringLiteral("profileSaveImmediately"));
    ASSERT_NE(saveImmediately, nullptr);
    EXPECT_FALSE(saveImmediately->isChecked());
    EXPECT_TRUE(options.sidecarPath.isEmpty());
}

TEST(ProfileDialogTest, ResultDialogExposesSummaryAndPlot)
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (int index = 0; index < 4; ++index)
    {
        pcl::PointXYZRGB point;
        point.x = static_cast<float>(index) + 0.25f;
        point.y = 0.0f;
        point.z = static_cast<float>(index);
        cloud->push_back(point);
    }
    famp::profile::Options options;
    options.binSize = 1.0;
    auto result = famp::profile::extract(
        cloud, {}, {0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}, options);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    result.sourceLayerName = QStringLiteral("测试点云");
    famp::profileui::ProfileResultDialog dialog(
        result, {QStringLiteral("profile.famp-profile")});
    const auto* summary = dialog.findChild<QLabel*>(
        QStringLiteral("profileSummary"));
    ASSERT_NE(summary, nullptr);
    EXPECT_TRUE(summary->text().contains(QStringLiteral("测试点云")));
    EXPECT_NE(dialog.findChild<QWidget*>(QStringLiteral("profilePlot")),
              nullptr);
}
