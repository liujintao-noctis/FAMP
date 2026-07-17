#include "ProfileIO.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>

namespace
{
famp::profile::Result profileResult()
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (int index = 0; index < 8; ++index)
    {
        pcl::PointXYZRGB point;
        point.x = static_cast<float>(index) + 0.25f;
        point.y = (index % 2 == 0) ? -0.1f : 0.1f;
        point.z = static_cast<float>(index % 4);
        cloud->push_back(point);
    }
    famp::profile::Options options;
    options.corridorWidth = 1.0;
    options.binSize = 2.0;
    auto result = famp::profile::extract(
        cloud, {}, {0.0, 0.0, 0.0}, {8.0, 0.0, 0.0}, options);
    result.sourceLayerId = QStringLiteral("layer-1");
    result.sourceLayerName = QStringLiteral("探方 点云");
    result.sourcePath = QStringLiteral("/tmp/点云.laz");
    result.sourceCrs = QStringLiteral("EPSG:4547");
    result.horizontalUnitName = QStringLiteral("米");
    return result;
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
}

TEST(ProfileIOTest, RoundTripsVersionedSidecarWithUnicodeMetadata)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(
        QStringLiteral("探方剖面.famp-profile"));
    const auto input = profileResult();
    ASSERT_TRUE(input.succeeded()) << input.error.toStdString();
    QString error;
    ASSERT_TRUE(famp::profileio::saveResultAtomically(path, input, &error))
        << error.toStdString();

    famp::profile::Result loaded;
    ASSERT_TRUE(famp::profileio::loadResult(path, loaded, &error))
        << error.toStdString();
    EXPECT_EQ(loaded.sourceLayerName, input.sourceLayerName);
    EXPECT_EQ(loaded.sourcePath, input.sourcePath);
    EXPECT_EQ(loaded.samples.size(), input.samples.size());
    EXPECT_EQ(loaded.bins.size(), input.bins.size());
    EXPECT_DOUBLE_EQ(loaded.bins.at(1).median, input.bins.at(1).median);
    EXPECT_DOUBLE_EQ(loaded.samples.back().coordinate[2],
                     input.samples.back().coordinate[2]);
}

TEST(ProfileIOTest, ExportsBinsSamplesAndScalableSvg)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto result = profileResult();
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    const QString bins = directory.filePath(QStringLiteral("bins.csv"));
    const QString samples = directory.filePath(QStringLiteral("points.csv"));
    const QString svg = directory.filePath(QStringLiteral("profile.svg"));
    QString error;
    ASSERT_TRUE(famp::profileio::exportBinsCsvAtomically(
        bins, result, &error)) << error.toStdString();
    ASSERT_TRUE(famp::profileio::exportSamplesCsvAtomically(
        samples, result, &error)) << error.toStdString();
    ASSERT_TRUE(famp::profileio::exportSvgAtomically(
        svg, result, &error)) << error.toStdString();
    EXPECT_TRUE(readAll(bins).startsWith("bin_index,station_start"));
    EXPECT_TRUE(readAll(samples).contains("signed_offset_m,x,y,z"));
    const QByteArray svgBytes = readAll(svg);
    EXPECT_TRUE(svgBytes.contains("<svg"));
    EXPECT_TRUE(svgBytes.contains("vector-effect=\"non-scaling-stroke\""));
    EXPECT_TRUE(svgBytes.contains("EPSG:4547"));
}

TEST(ProfileIOTest, RejectsMalformedSidecarWithoutMutatingOutput)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("bad.famp-profile"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("FPRF\x01", 5);
    file.close();

    famp::profile::Result output;
    output.sourceLayerName = QStringLiteral("保留");
    QString error;
    EXPECT_FALSE(famp::profileio::loadResult(path, output, &error));
    EXPECT_EQ(output.sourceLayerName, QStringLiteral("保留"));
    EXPECT_FALSE(error.isEmpty());
}

TEST(ProfileIOTest, CancellationPreservesExistingTarget)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("profile.csv"));
    QFile existing(path);
    ASSERT_TRUE(existing.open(QIODevice::WriteOnly));
    existing.write("keep-me");
    existing.close();

    QString error;
    EXPECT_FALSE(famp::profileio::exportSamplesCsvAtomically(
        path, profileResult(), &error, []() { return true; }));
    EXPECT_EQ(readAll(path), QByteArray("keep-me"));
    EXPECT_TRUE(error.contains(QStringLiteral("取消")));

    EXPECT_FALSE(famp::profileio::saveResultAtomically(
        path, profileResult(), &error, []() { return true; }));
    EXPECT_EQ(readAll(path), QByteArray("keep-me"));
    EXPECT_TRUE(error.contains(QStringLiteral("取消")));
}

TEST(ProfileIOTest, RejectsSampleAssignedToWrongBin)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto result = profileResult();
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    result.samples.front().binIndex = 1;
    const QString path = directory.filePath(
        QStringLiteral("invalid.famp-profile"));
    QString error;
    EXPECT_FALSE(famp::profileio::saveResultAtomically(path, result, &error));
    EXPECT_FALSE(QFile::exists(path));
    EXPECT_TRUE(error.contains(QStringLiteral("索引")));
}

TEST(ProfileIOTest, EnforcesStableProfileSuffix)
{
    EXPECT_EQ(famp::profileio::pathWithProfileSuffix(
                  QStringLiteral("result")),
              QStringLiteral("result.famp-profile"));
    EXPECT_EQ(famp::profileio::pathWithProfileSuffix(
                  QStringLiteral("result.FAMP-PROFILE")),
              QStringLiteral("result.FAMP-PROFILE"));
}
