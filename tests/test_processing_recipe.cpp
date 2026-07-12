#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>

#include "ProcessingRecipe.h"

TEST(ProcessingRecipeTest, RoundTripsAllOperationsAtomically)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    famp::processing::Options processing;
    processing.method = famp::processing::Method::StatisticalOutlierRemoval;
    processing.meanNeighbors = 37;
    processing.standardDeviationMultiplier = 1.75;
    const auto source = directory.filePath(QStringLiteral("输入.pcd"));
    QFile sourceFile(source);
    ASSERT_TRUE(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("cloud");
    sourceFile.close();

    const auto recipe = famp::recipe::forProcessing(processing, source);
    QString savedPath;
    QString error;
    ASSERT_TRUE(famp::recipe::save(
        directory.filePath(QStringLiteral("统计去噪方案")),
        recipe, &savedPath, &error)) << error.toStdString();
    EXPECT_TRUE(savedPath.endsWith(QStringLiteral(".json")));

    famp::recipe::Recipe loaded;
    ASSERT_TRUE(famp::recipe::load(savedPath, loaded, &error))
        << error.toStdString();
    EXPECT_EQ(loaded.operation,
              famp::recipe::Operation::StatisticalOutlierRemoval);
    EXPECT_EQ(loaded.processing.meanNeighbors, 37);
    EXPECT_DOUBLE_EQ(loaded.processing.standardDeviationMultiplier, 1.75);
    EXPECT_EQ(loaded.source.path, source);
    EXPECT_EQ(loaded.source.size, 5);
    EXPECT_TRUE(famp::recipe::sourceMatches(loaded, source, &error));

    ASSERT_TRUE(sourceFile.open(QIODevice::Append));
    sourceFile.write("changed");
    sourceFile.close();
    EXPECT_FALSE(famp::recipe::sourceMatches(loaded, source, &error));
    EXPECT_FALSE(error.isEmpty());

    famp::crop::Options crop;
    crop.minimumX = -10.0;
    crop.maximumX = 20.0;
    crop.keepInside = false;
    const QString cropPath = directory.filePath(QStringLiteral("crop.json"));
    ASSERT_TRUE(famp::recipe::save(
        cropPath, famp::recipe::forCrop(crop), nullptr, &error));
    ASSERT_TRUE(famp::recipe::load(cropPath, loaded, &error));
    EXPECT_EQ(loaded.operation, famp::recipe::Operation::RangeCrop);
    EXPECT_DOUBLE_EQ(loaded.crop.minimumX, -10.0);
    EXPECT_DOUBLE_EQ(loaded.crop.maximumX, 20.0);
    EXPECT_FALSE(loaded.crop.keepInside);
}

TEST(ProcessingRecipeTest, RejectsMalformedOrUnsupportedRecipeAtomically)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("bad.json"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(R"({"schemaVersion":99,"operation":"voxelDownsample","parameters":{}})");
    file.close();

    famp::recipe::Recipe preserved;
    preserved.operation = famp::recipe::Operation::RangeCrop;
    QString error;
    EXPECT_FALSE(famp::recipe::load(path, preserved, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(preserved.operation, famp::recipe::Operation::RangeCrop);
}

TEST(ProcessingRecipeTest, DerivesStableAutomaticSidecarPath)
{
    EXPECT_EQ(famp::recipe::automaticSidecarPath(QStringLiteral("result.pcd")),
              QStringLiteral("result.pcd.famp-process.json"));
}
