#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>

#include "RecentFiles.h"

TEST(RecentFilesTest, RecognizesSupportedExtensionsCaseInsensitively)
{
    EXPECT_TRUE(famp::recent::isSupportedCloudFile(QStringLiteral("site.pcd")));
    EXPECT_TRUE(famp::recent::isSupportedCloudFile(QStringLiteral("SITE.LAS")));
    EXPECT_FALSE(famp::recent::isSupportedCloudFile(QStringLiteral("site.laz")));
    EXPECT_FALSE(famp::recent::isSupportedCloudFile(QStringLiteral("site")));
}

TEST(RecentFilesTest, MovesReopenedFileToFrontAndLimitsHistory)
{
    const QStringList current{
        QStringLiteral("/tmp/first.pcd"),
        QStringLiteral("/tmp/second.las"),
        QStringLiteral("/tmp/third.pcd")};

    const QStringList updated = famp::recent::updatedFiles(
        current, QStringLiteral("/tmp/second.las"), 2);

    ASSERT_EQ(updated.size(), 2);
    EXPECT_EQ(updated.at(0), famp::recent::normalizedPath(QStringLiteral("/tmp/second.las")));
    EXPECT_EQ(updated.at(1), famp::recent::normalizedPath(QStringLiteral("/tmp/first.pcd")));
}

TEST(RecentFilesTest, FiltersMissingUnsupportedAndDuplicateFiles)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    const QString cloudPath = directory.filePath(QStringLiteral("sample.pcd"));
    const QString unsupportedPath = directory.filePath(QStringLiteral("notes.txt"));
    QFile cloudFile(cloudPath);
    QFile unsupportedFile(unsupportedPath);
    ASSERT_TRUE(cloudFile.open(QIODevice::WriteOnly));
    cloudFile.close();
    ASSERT_TRUE(unsupportedFile.open(QIODevice::WriteOnly));
    unsupportedFile.close();

    const QStringList available = famp::recent::availableFiles({
        cloudPath,
        unsupportedPath,
        directory.filePath(QStringLiteral("missing.las")),
        cloudPath});

    ASSERT_EQ(available.size(), 1);
    EXPECT_EQ(available.front(), famp::recent::normalizedPath(cloudPath));
}

TEST(RecentFilesTest, UnsupportedCandidateDoesNotClearExistingHistory)
{
    const QString existing = QStringLiteral("/tmp/existing.pcd");
    const QStringList updated = famp::recent::updatedFiles(
        {existing}, QStringLiteral("/tmp/unsupported.txt"));

    ASSERT_EQ(updated.size(), 1);
    EXPECT_EQ(updated.front(), famp::recent::normalizedPath(existing));
}

TEST(RecentFilesTest, MaximumCountIncludesTheNewlyOpenedFile)
{
    const QStringList updated = famp::recent::updatedFiles(
        {QStringLiteral("/tmp/older.pcd")},
        QStringLiteral("/tmp/newer.las"),
        1);

    ASSERT_EQ(updated.size(), 1);
    EXPECT_EQ(updated.front(),
              famp::recent::normalizedPath(QStringLiteral("/tmp/newer.las")));
}
