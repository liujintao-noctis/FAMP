#include "TerrainIO.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <limits>

namespace
{
famp::terrain::Grid sampleGrid()
{
    famp::terrain::Grid grid;
    grid.columns = 2;
    grid.rows = 2;
    grid.originX = 500000.25;
    grid.originY = 3400000.5;
    grid.resolution = 0.5;
    grid.horizontalUnitToMetre = 1.0;
    grid.statistic = famp::terrain::CellStatistic::Median;
    grid.sourcePointCount = 4;
    grid.populatedCellCount = 3;
    grid.filledCellCount = 0;
    grid.sourceLayerId = QStringLiteral("layer-田野");
    grid.sourceLayerName = QStringLiteral("探方一号");
    grid.sourceCrs = QStringLiteral("EPSG:4547");
    grid.horizontalUnitName = QStringLiteral("metre");
    grid.elevations = {
        10.25, std::numeric_limits<double>::quiet_NaN(), 11.5, 12.75};
    return grid;
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
}

TEST(TerrainIoTest, RoundTripsVersionedGridWithUnicodeAndNoData)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("地形成果.famp-dem"));
    const auto input = sampleGrid();
    QString error;
    ASSERT_TRUE(famp::terrainio::saveGridAtomically(path, input, &error))
        << error.toStdString();

    famp::terrain::Grid output;
    ASSERT_TRUE(famp::terrainio::loadGrid(path, output, &error))
        << error.toStdString();
    EXPECT_EQ(output.columns, input.columns);
    EXPECT_EQ(output.rows, input.rows);
    EXPECT_DOUBLE_EQ(output.originX, input.originX);
    EXPECT_DOUBLE_EQ(output.originY, input.originY);
    EXPECT_DOUBLE_EQ(output.resolution, input.resolution);
    EXPECT_EQ(output.sourceLayerId, input.sourceLayerId);
    EXPECT_EQ(output.sourceLayerName, input.sourceLayerName);
    EXPECT_EQ(output.sourceCrs, input.sourceCrs);
    ASSERT_EQ(output.elevations.size(), input.elevations.size());
    EXPECT_TRUE(std::isnan(output.elevations.at(1)));
    EXPECT_DOUBLE_EQ(output.elevations.at(3), 12.75);
}

TEST(TerrainIoTest, RejectsMalformedSidecarWithoutMutatingOutput)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("broken.famp-dem"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("FDEM\x01\x00", 6), 6);
    file.close();

    famp::terrain::Grid output;
    output.columns = 99;
    QString error;
    EXPECT_FALSE(famp::terrainio::loadGrid(path, output, &error));
    EXPECT_EQ(output.columns, 99);
    EXPECT_FALSE(error.isEmpty());
}

TEST(TerrainIoTest, ExportsEsriAsciiAndGridCsvDeterministically)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto grid = sampleGrid();
    QString error;
    const QString ascPath = directory.filePath(QStringLiteral("dem.asc"));
    ASSERT_TRUE(famp::terrainio::exportAsciiGridAtomically(
        ascPath, grid, &error)) << error.toStdString();
    const QByteArray asc = readAll(ascPath);
    EXPECT_TRUE(asc.startsWith("ncols 2\nnrows 2\n"));
    EXPECT_TRUE(asc.contains("xllcorner 500000.25\n"));
    EXPECT_TRUE(asc.contains("NODATA_value -9999\n"));
    EXPECT_TRUE(asc.endsWith("11.5 12.75\n10.25 -9999\n"));

    const QString csvPath = directory.filePath(QStringLiteral("dem.csv"));
    ASSERT_TRUE(famp::terrainio::exportGridCsvAtomically(
        csvPath, grid, &error)) << error.toStdString();
    const QByteArray csv = readAll(csvPath);
    EXPECT_TRUE(csv.startsWith("column,row,x,y,elevation\n"));
    EXPECT_TRUE(csv.contains("1,0,500001,3400000.75,NoData\n"));
    EXPECT_EQ(csv.count('\n'), 5);
}

TEST(TerrainIoTest, ExportsContourCsvAndNormalizedSvg)
{
    const QVector<famp::terrain::ContourLine> contours{
        {10.0, {{{500000.0, 3400000.0},
                  {500001.0, 3400001.0},
                  {500002.0, 3400000.0}}}}};
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QString error;
    const QString csvPath = directory.filePath(QStringLiteral("contours.csv"));
    ASSERT_TRUE(famp::terrainio::exportContoursCsvAtomically(
        csvPath, contours, &error)) << error.toStdString();
    const QByteArray csv = readAll(csvPath);
    EXPECT_TRUE(csv.startsWith("line_id,elevation,point_index,x,y\n"));
    EXPECT_TRUE(csv.contains("0,10,2,500002,3400000\n"));

    const QString svgPath = directory.filePath(QStringLiteral("contours.svg"));
    ASSERT_TRUE(famp::terrainio::exportContoursSvgAtomically(
        svgPath, contours, QStringLiteral("EPSG:4547"), &error))
        << error.toStdString();
    const QByteArray svg = readAll(svgPath);
    EXPECT_TRUE(svg.contains("viewBox=\"0 0 2 1\""));
    EXPECT_TRUE(svg.contains("CRS=EPSG:4547"));
    EXPECT_TRUE(svg.contains("data-elevation=\"10\""));
    EXPECT_TRUE(svg.contains("M 0 1 L 1 0 L 2 1"));
}

TEST(TerrainIoTest, AddsSidecarSuffixOnlyOnce)
{
    EXPECT_EQ(famp::terrainio::pathWithDemSuffix(QStringLiteral("result")),
              QStringLiteral("result.famp-dem"));
    EXPECT_EQ(famp::terrainio::pathWithDemSuffix(
                  QStringLiteral("result.FAMP-DEM")),
              QStringLiteral("result.FAMP-DEM"));
}

TEST(TerrainIoTest, CancellationPreservesExistingAtomicOutputs)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString sidecar = directory.filePath(QStringLiteral("dem.famp-dem"));
    QString error;
    auto grid = sampleGrid();
    ASSERT_TRUE(famp::terrainio::saveGridAtomically(sidecar, grid, &error));
    const QByteArray original = readAll(sidecar);
    grid.elevations[0] = 999.0;
    EXPECT_FALSE(famp::terrainio::saveGridAtomically(
        sidecar, grid, &error, []() { return true; }));
    EXPECT_TRUE(error.contains(QStringLiteral("取消")));
    EXPECT_EQ(readAll(sidecar), original);

    const QString csv = directory.filePath(QStringLiteral("dem.csv"));
    QFile file(csv);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("keep"), 4);
    file.close();
    EXPECT_FALSE(famp::terrainio::exportGridCsvAtomically(
        csv, grid, &error, []() { return true; }));
    EXPECT_TRUE(error.contains(QStringLiteral("取消")));
    EXPECT_EQ(readAll(csv), QByteArray("keep"));
}
