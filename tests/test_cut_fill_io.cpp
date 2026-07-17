#include "CutFillIO.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <limits>

namespace
{
famp::cutfill::Result sampleResult()
{
    famp::terrain::Grid grid;
    grid.columns = 2;
    grid.rows = 2;
    grid.originX = 500000.0;
    grid.originY = 3400000.0;
    grid.resolution = 1.0;
    grid.horizontalUnitToMetre = 1.0;
    grid.statistic = famp::terrain::CellStatistic::Median;
    grid.sourcePointCount = 4;
    grid.populatedCellCount = 3;
    grid.sourceLayerId = QStringLiteral("layer-田野");
    grid.sourceLayerName = QStringLiteral("当前地表");
    grid.sourceCrs = QStringLiteral("EPSG:4547");
    grid.horizontalUnitName = QStringLiteral("米");
    grid.elevations = {
        12.0, 8.0, 10.0,
        std::numeric_limits<double>::quiet_NaN()};
    famp::cutfill::Options options;
    options.referenceElevation = 10.0;
    options.zeroTolerance = 0.01;
    auto result = famp::cutfill::compareToConstant(
        std::move(grid), options);
    EXPECT_TRUE(result.succeeded()) << result.error.toStdString();
    result.referencePath = QStringLiteral("固定高程 10 米");
    return result;
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
}

TEST(CutFillIoTest, RoundTripsVersionedUnicodeResult)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(
        QStringLiteral("探方挖填方.famp-volume"));
    const auto input = sampleResult();
    QString error;
    ASSERT_TRUE(famp::cutfillio::saveResultAtomically(
        path, input, &error)) << error.toStdString();

    famp::cutfill::Result output;
    ASSERT_TRUE(famp::cutfillio::loadResult(path, output, &error))
        << error.toStdString();
    ASSERT_TRUE(output.succeeded()) << output.error.toStdString();
    EXPECT_EQ(output.currentGrid.sourceLayerId,
              input.currentGrid.sourceLayerId);
    EXPECT_EQ(output.currentGrid.sourceLayerName,
              input.currentGrid.sourceLayerName);
    EXPECT_EQ(output.referencePath, input.referencePath);
    EXPECT_EQ(output.referenceMode, input.referenceMode);
    EXPECT_EQ(output.comparedCellCount, input.comparedCellCount);
    EXPECT_DOUBLE_EQ(output.cutVolumeCubicMetres,
                     input.cutVolumeCubicMetres);
    EXPECT_DOUBLE_EQ(output.fillVolumeCubicMetres,
                     input.fillVolumeCubicMetres);
    ASSERT_EQ(output.differences.size(), input.differences.size());
    EXPECT_TRUE(std::isnan(output.differences.at(3)));
}

TEST(CutFillIoTest, RejectsMalformedOrTruncatedSidecarWithoutMutation)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("broken.famp-volume"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("FVOL\x01\x00", 6), 6);
    file.close();

    famp::cutfill::Result output;
    output.cutCellCount = 99;
    QString error;
    EXPECT_FALSE(famp::cutfillio::loadResult(path, output, &error));
    EXPECT_EQ(output.cutCellCount, 99U);
    EXPECT_FALSE(error.isEmpty());

    const auto input = sampleResult();
    ASSERT_TRUE(famp::cutfillio::saveResultAtomically(path, input, &error));
    QByteArray contents = readAll(path);
    contents.chop(5);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(contents), contents.size());
    file.close();
    EXPECT_FALSE(famp::cutfillio::loadResult(path, output, &error));
    EXPECT_EQ(output.cutCellCount, 99U);
}

TEST(CutFillIoTest, ExportsSummaryCellsAndDownsampledSvg)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto result = sampleResult();
    QString error;

    const QString summaryPath = directory.filePath(QStringLiteral("summary.csv"));
    ASSERT_TRUE(famp::cutfillio::exportSummaryCsvAtomically(
        summaryPath, result, &error)) << error.toStdString();
    const QByteArray summary = readAll(summaryPath);
    EXPECT_TRUE(summary.startsWith("metric,value,unit\n"));
    EXPECT_TRUE(summary.contains("cut_volume,2,m3\n"));
    EXPECT_TRUE(summary.contains("fill_volume,2,m3\n"));
    EXPECT_TRUE(summary.contains("signed_volume_cut_minus_fill,0,m3\n"));
    EXPECT_TRUE(summary.contains("reference_elevation,10,m\n"));
    EXPECT_TRUE(summary.contains("zero_tolerance,0.01,m\n"));
    EXPECT_TRUE(summary.contains("cut_cells,1,cell\n"));

    const QString cellsPath = directory.filePath(QStringLiteral("cells.csv"));
    ASSERT_TRUE(famp::cutfillio::exportCellsCsvAtomically(
        cellsPath, result, &error)) << error.toStdString();
    const QByteArray cells = readAll(cellsPath);
    EXPECT_TRUE(cells.startsWith("column,row,x,y,current_elevation"));
    EXPECT_TRUE(cells.contains("0,0,500000.5,3400000.5,12,10,2,\"挖方\",1,2\n"));
    EXPECT_TRUE(cells.contains("1,0,500001.5,3400000.5,8,10,-2,\"填方\",1,-2\n"));
    EXPECT_EQ(cells.count('\n'), 4);

    const QString svgPath = directory.filePath(QStringLiteral("map.svg"));
    ASSERT_TRUE(famp::cutfillio::exportSvgAtomically(
        svgPath, result, &error)) << error.toStdString();
    const QByteArray svg = readAll(svgPath);
    EXPECT_TRUE(svg.contains("viewBox=\"0 0 2 2\""));
    EXPECT_TRUE(svg.contains("data-cut-volume-m3=\"2\""));
    EXPECT_TRUE(svg.contains("data-fill-volume-m3=\"2\""));
    EXPECT_TRUE(svg.contains("data-mean-difference-m=\"2\""));
    EXPECT_TRUE(svg.contains("data-mean-difference-m=\"-2\""));
}

TEST(CutFillIoTest, AddsVolumeSuffixOnlyOnce)
{
    EXPECT_EQ(famp::cutfillio::pathWithVolumeSuffix(
                  QStringLiteral("result")),
              QStringLiteral("result.famp-volume"));
    EXPECT_EQ(famp::cutfillio::pathWithVolumeSuffix(
                  QStringLiteral("result.FAMP-VOLUME")),
              QStringLiteral("result.FAMP-VOLUME"));
}

TEST(CutFillIoTest, CancellationPreservesExistingAtomicFiles)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(
        QStringLiteral("volume.famp-volume"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("keep"), 4);
    file.close();
    QString error;
    const auto result = sampleResult();
    EXPECT_FALSE(famp::cutfillio::saveResultAtomically(
        path, result, &error, []() { return true; }));
    EXPECT_TRUE(error.contains(QStringLiteral("取消")));
    EXPECT_EQ(readAll(path), QByteArray("keep"));

    const QString csvPath = directory.filePath(QStringLiteral("cells.csv"));
    file.setFileName(csvPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("keep"), 4);
    file.close();
    EXPECT_FALSE(famp::cutfillio::exportCellsCsvAtomically(
        csvPath, result, &error, []() { return true; }));
    EXPECT_EQ(readAll(csvPath), QByteArray("keep"));
}

TEST(CutFillIoTest, RejectsTamperedResultBeforeWriting)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto result = sampleResult();
    result.fillVolumeCubicMetres += 1.0;
    const QString path = directory.filePath(QStringLiteral("invalid.famp-volume"));
    QString error;
    EXPECT_FALSE(famp::cutfillio::saveResultAtomically(path, result, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("无效")));
    EXPECT_FALSE(QFileInfo::exists(path));

    result = sampleResult();
    result.cancelled = true;
    EXPECT_FALSE(famp::cutfillio::saveResultAtomically(
        path, result, &error));
    EXPECT_FALSE(QFileInfo::exists(path));
}
