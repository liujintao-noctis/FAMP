#include "TerrainDialog.h"

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QTemporaryDir>

TEST(TerrainDialogTest, DerivesStableSiblingExportPaths)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto paths = famp::terrainui::derivedExportPaths(
        directory.filePath(QStringLiteral("探方 DEM.famp-dem")));
    EXPECT_TRUE(paths.sidecar.endsWith(QStringLiteral("探方 DEM.famp-dem")));
    EXPECT_TRUE(paths.asciiGrid.endsWith(QStringLiteral("探方 DEM.asc")));
    EXPECT_TRUE(paths.gridCsv.endsWith(QStringLiteral("探方 DEM_dem.csv")));
    EXPECT_TRUE(paths.contourCsv.endsWith(
        QStringLiteral("探方 DEM_contours.csv")));
    EXPECT_TRUE(paths.contourSvg.endsWith(
        QStringLiteral("探方 DEM_contours.svg")));
}

TEST(TerrainDialogTest, ValidatesOutputDirectoryAndAnalysisOptions)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    famp::terrainui::Options options;
    options.sidecarPath = directory.filePath(QStringLiteral("dem"));
    QString error;
    EXPECT_TRUE(famp::terrainui::validateOptions(options, &error))
        << error.toStdString();

    options.grid.automaticResolution = false;
    options.grid.resolution = 0.0;
    EXPECT_FALSE(famp::terrainui::validateOptions(options, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("分辨率")));

    options.grid.automaticResolution = true;
    options.sidecarPath = directory.filePath(
        QStringLiteral("missing/subdir/dem.famp-dem"));
    EXPECT_FALSE(famp::terrainui::validateOptions(options, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("目录")));
}

TEST(TerrainDialogTest, DefaultsToMemoryOnlyOutput)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    famp::terrainui::TerrainDialog dialog(
        QStringLiteral("layer"), QStringLiteral("EPSG:4547"),
        QStringLiteral("metre"), 1.0,
        directory.filePath(QStringLiteral("dem.famp-dem")));
    const auto* saveImmediately = dialog.findChild<QCheckBox*>(
        QStringLiteral("terrainSaveImmediately"));
    ASSERT_NE(saveImmediately, nullptr);
    EXPECT_FALSE(saveImmediately->isChecked());
    const auto options = dialog.options();
    EXPECT_TRUE(options.sidecarPath.isEmpty());
    QString error;
    EXPECT_TRUE(famp::terrainui::validateOptions(options, &error))
        << error.toStdString();
}
