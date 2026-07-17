#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <lasreader.hpp>
#include <laswriter.hpp>

#include "CloudLoader.h"
#include "LasLoader.h"

#include <cmath>
#include <memory>

#ifndef FAMP_SAMPLE_DIR
#error "FAMP_SAMPLE_DIR must point to the repository samples directory"
#endif

namespace
{
bool convertLasToLaz(const QString& sourcePath, const QString& targetPath)
{
    const QByteArray source = QFile::encodeName(sourcePath);
    LASreadOpener readOpener;
    readOpener.set_file_name(const_cast<char*>(source.constData()));
    LASreader* reader = readOpener.open();
    if (!reader)
        return false;

    const QByteArray target = QFile::encodeName(targetPath);
    LASwriteOpener writeOpener;
    writeOpener.set_file_name(const_cast<char*>(target.constData()));
    if (!writeOpener.set_format("laz"))
    {
        reader->close();
        delete reader;
        return false;
    }
    LASwriter* writer = writeOpener.open(&reader->header);
    if (!writer)
    {
        reader->close();
        delete reader;
        return false;
    }

    bool succeeded = true;
    while (reader->read_point())
    {
        if (!writer->write_point(&reader->point))
        {
            succeeded = false;
            break;
        }
    }
    writer->close();
    delete writer;
    reader->close();
    delete reader;
    return succeeded && QFileInfo(targetPath).size() > 0;
}
}

TEST(LasLoaderTest, LoadsBundledSampleAndRecentersCoordinates)
{
    const QString path = QStringLiteral(FAMP_SAMPLE_DIR "/1.las");
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    std::array<double, 3> origin{};
    famp::cloud::CloudAttributes attributes;
    QString error;

    ASSERT_TRUE(loadLasAsRgb(path, cloud, &error, &origin, &attributes))
        << error.toStdString();
    ASSERT_TRUE(cloud);
    EXPECT_FALSE(cloud->empty());
    EXPECT_EQ(cloud->height, 1U);
    EXPECT_EQ(cloud->width, cloud->size());
    EXPECT_TRUE(std::isfinite(origin[0]));
    EXPECT_TRUE(std::isfinite(origin[1]));
    EXPECT_TRUE(std::isfinite(origin[2]));
    ASSERT_TRUE(attributes.validate(static_cast<qint64>(cloud->size())));
    for (const QString& name : {
             QStringLiteral("intensity"),
             QStringLiteral("classification"),
             QStringLiteral("return_number"),
             QStringLiteral("number_of_returns"),
             QStringLiteral("scan_angle"),
             QStringLiteral("user_data"),
             QStringLiteral("point_source_id")})
    {
        ASSERT_TRUE(attributes.contains(name)) << name.toStdString();
        EXPECT_EQ(attributes.channel(name)->valueCount(),
                  static_cast<qint64>(cloud->size()));
    }
}

TEST(LasLoaderTest, LoadsActualCompressedLazWithAttributes)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString source = QStringLiteral(FAMP_SAMPLE_DIR "/1.las");
    const QString compressed = directory.filePath(QStringLiteral("sample.laz"));
    ASSERT_TRUE(convertLasToLaz(source, compressed));
    EXPECT_LT(QFileInfo(compressed).size(), QFileInfo(source).size());
    const QString unicodePath = directory.filePath(
        QStringLiteral("探方压缩点云.laz"));
    ASSERT_TRUE(QFile::copy(compressed, unicodePath));

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    famp::cloud::CloudAttributes attributes;
    QString error;
    ASSERT_TRUE(loadLasAsRgb(
        unicodePath, cloud, &error, nullptr, &attributes))
        << error.toStdString();
    ASSERT_TRUE(cloud);
    EXPECT_FALSE(cloud->empty());
    EXPECT_TRUE(attributes.contains(QStringLiteral("intensity")));
    EXPECT_TRUE(attributes.validate(static_cast<qint64>(cloud->size())));

    const famp::cloud::LoadResult result = famp::cloud::load(unicodePath);
    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_TRUE(result.attributes.contains(QStringLiteral("classification")));
    EXPECT_EQ(result.displayCloud->size(), cloud->size());
}

TEST(LasLoaderTest, LoadsFromNonAsciiPath)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString source = QStringLiteral(FAMP_SAMPLE_DIR "/1.las");
    const QString destination = directory.filePath(QStringLiteral("探方一.las"));
    ASSERT_TRUE(QFile::copy(source, destination));

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud;
    QString error;
    ASSERT_TRUE(loadLasAsRgb(destination, cloud, &error)) << error.toStdString();
    ASSERT_TRUE(cloud);
    EXPECT_FALSE(cloud->empty());
}

TEST(LasLoaderTest, RejectsMissingFileWithoutMutatingOutput)
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr original(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    original->push_back(pcl::PointXYZRGB());
    auto output = original;
    famp::cloud::AttributeChannel sentinel;
    sentinel.name = QStringLiteral("sentinel");
    sentinel.type = famp::cloud::AttributeValueType::UnsignedInteger;
    sentinel.unsignedValues = {42};
    famp::cloud::CloudAttributes attributes;
    ASSERT_TRUE(attributes.insert(sentinel, 1));
    QString error;

    EXPECT_FALSE(loadLasAsRgb(
        QStringLiteral("/missing/input.las"), output, &error, nullptr,
        &attributes));
    EXPECT_EQ(output, original);
    EXPECT_TRUE(attributes.contains(QStringLiteral("sentinel")));
    EXPECT_FALSE(error.isEmpty());
}
