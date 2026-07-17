#include "CutFillDialog.h"

#include <gtest/gtest.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>

#include <limits>

namespace
{
famp::terrain::Grid referenceGrid()
{
    famp::terrain::Grid grid;
    grid.columns = 2;
    grid.rows = 2;
    grid.originX = 500000.0;
    grid.originY = 3400000.0;
    grid.resolution = 0.5;
    grid.horizontalUnitToMetre = 1.0;
    grid.statistic = famp::terrain::CellStatistic::Median;
    grid.sourcePointCount = 4;
    grid.populatedCellCount = 4;
    grid.sourceLayerId = QStringLiteral("reference");
    grid.sourceLayerName = QStringLiteral("参考地表");
    grid.sourceCrs = QStringLiteral("EPSG:4547");
    grid.horizontalUnitName = QStringLiteral("米");
    grid.elevations = {1.0, 1.0, 1.0, 1.0};
    return grid;
}
}

TEST(CutFillDialogTest, DerivesStableSiblingExportPaths)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto paths = famp::cutfillui::derivedExportPaths(
        directory.filePath(QStringLiteral("探方 土方.famp-volume")));
    EXPECT_TRUE(paths.sidecar.endsWith(
        QStringLiteral("探方 土方.famp-volume")));
    EXPECT_TRUE(paths.summaryCsv.endsWith(
        QStringLiteral("探方 土方_summary.csv")));
    EXPECT_TRUE(paths.cellsCsv.endsWith(
        QStringLiteral("探方 土方_cells.csv")));
    EXPECT_TRUE(paths.svg.endsWith(QStringLiteral("探方 土方.svg")));
}

TEST(CutFillDialogTest, ValidatesModesAndOutputDirectory)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    famp::cutfillui::Options options;
    options.grid.maximumCellCount = options.analysis.maximumCellCount;
    options.sidecarPath = directory.filePath(QStringLiteral("volume"));
    QString error;
    EXPECT_TRUE(famp::cutfillui::validateOptions(options, &error))
        << error.toStdString();

    options.analysis.zeroTolerance = -1.0;
    EXPECT_FALSE(famp::cutfillui::validateOptions(options, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("容差")));
    options.analysis.zeroTolerance = 0.0;

    options.analysis.referenceMode =
        famp::cutfill::ReferenceMode::DemGrid;
    EXPECT_FALSE(famp::cutfillui::validateOptions(options, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("参考 DEM")));

    options.referenceDemPath = directory.filePath(
        QStringLiteral("missing.famp-dem"));
    EXPECT_FALSE(famp::cutfillui::validateOptions(options, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("不存在")));

    options.analysis.referenceMode =
        famp::cutfill::ReferenceMode::ConstantElevation;
    options.sidecarPath = directory.filePath(
        QStringLiteral("missing/volume.famp-volume"));
    EXPECT_FALSE(famp::cutfillui::validateOptions(options, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("目录")));
}

TEST(CutFillDialogTest, ConvertsMetreInputsAndSwitchesReferenceMode)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    famp::cutfillui::CutFillDialog dialog(
        QStringLiteral("layer"), QStringLiteral("EPSG:1234"),
        QStringLiteral("foot"), 0.3048,
        directory.filePath(QStringLiteral("volume.famp-volume")));
    auto* elevation = dialog.findChild<QDoubleSpinBox*>(
        QStringLiteral("cutFillReferenceElevationMetres"));
    auto* tolerance = dialog.findChild<QDoubleSpinBox*>(
        QStringLiteral("cutFillZeroToleranceMetres"));
    auto* mode = dialog.findChild<QComboBox*>(
        QStringLiteral("cutFillReferenceMode"));
    ASSERT_NE(elevation, nullptr);
    ASSERT_NE(tolerance, nullptr);
    ASSERT_NE(mode, nullptr);
    elevation->setValue(3.048);
    tolerance->setValue(0.3048);
    auto options = dialog.options();
    EXPECT_NEAR(options.analysis.referenceElevation, 10.0, 1.0e-12);
    EXPECT_NEAR(options.analysis.zeroTolerance, 1.0, 1.0e-12);
    EXPECT_TRUE(options.grid.automaticResolution);

    mode->setCurrentIndex(mode->findData(
        static_cast<int>(famp::cutfill::ReferenceMode::DemGrid)));
    options = dialog.options();
    EXPECT_EQ(options.analysis.referenceMode,
              famp::cutfill::ReferenceMode::DemGrid);
    EXPECT_FALSE(options.grid.automaticResolution);
    EXPECT_FALSE(elevation->isEnabled());
}

TEST(CutFillDialogTest, AppliesReferenceResolutionAtomically)
{
    famp::cutfillui::Options options;
    options.analysis.referenceMode =
        famp::cutfill::ReferenceMode::DemGrid;
    options.grid.automaticResolution = true;
    options.grid.resolution = 9.0;
    QString error;
    EXPECT_TRUE(famp::cutfillui::applyReferenceGrid(
        referenceGrid(), QStringLiteral("EPSG:4547"), 1.0,
        options, &error)) << error.toStdString();
    EXPECT_FALSE(options.grid.automaticResolution);
    EXPECT_DOUBLE_EQ(options.grid.resolution, 0.5);

    auto mismatchOptions = options;
    mismatchOptions.grid.resolution = 7.0;
    EXPECT_FALSE(famp::cutfillui::applyReferenceGrid(
        referenceGrid(), QStringLiteral("EPSG:3857"), 1.0,
        mismatchOptions, &error));
    EXPECT_DOUBLE_EQ(mismatchOptions.grid.resolution, 7.0);
    EXPECT_TRUE(error.contains(QStringLiteral("CRS")));
}

TEST(CutFillDialogTest, ResultOverviewDownsamplesLargeGrid)
{
    famp::terrain::Grid grid;
    grid.columns = 600;
    grid.rows = 500;
    grid.originX = 0.0;
    grid.originY = 0.0;
    grid.resolution = 1.0;
    grid.horizontalUnitToMetre = 1.0;
    grid.statistic = famp::terrain::CellStatistic::Median;
    grid.sourcePointCount = grid.columns * grid.rows;
    grid.populatedCellCount = grid.sourcePointCount;
    grid.sourceLayerName = QStringLiteral("当前地表");
    grid.elevations.fill(1.0, grid.sourcePointCount);
    famp::cutfill::Options options;
    const auto result = famp::cutfill::compareToConstant(
        std::move(grid), options);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();

    famp::cutfillui::CutFillResultDialog dialog(
        result, {QStringLiteral("volume.famp-volume")});
    const auto* summary = dialog.findChild<QLabel*>(
        QStringLiteral("cutFillSummary"));
    const auto* overview = dialog.findChild<QWidget*>(
        QStringLiteral("cutFillOverview"));
    ASSERT_NE(summary, nullptr);
    ASSERT_NE(overview, nullptr);
    EXPECT_TRUE(summary->text().contains(QStringLiteral("净体积")));
    EXPECT_FALSE(summary->text().contains(QLatin1Char('%')));
    EXPECT_EQ(overview->property("sourceCellCount").toULongLong(),
              300'000U);
    EXPECT_LE(overview->property("renderedBlockCount").toULongLong(),
              240'000U);
    EXPECT_LT(overview->property("renderedBlockCount").toULongLong(),
              overview->property("sourceCellCount").toULongLong());
}
