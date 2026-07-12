#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ProjectDocument.h"

TEST(ProjectDocumentTest, RoundTripsRelativeCloudPathsAndScale)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_TRUE(QDir(directory.path()).mkpath(QStringLiteral("clouds")));
    const QString cloudPath = directory.filePath(QStringLiteral("clouds/探方.pcd"));
    QFile cloudFile(cloudPath);
    ASSERT_TRUE(cloudFile.open(QIODevice::WriteOnly));
    cloudFile.close();

    const QString projectPath = directory.filePath(QStringLiteral("项目.famp"));
    famp::project::Document source;
    source.cloudFiles = {cloudPath, cloudPath};
    source.mapScale = QStringLiteral("1:100");
    QString error;
    ASSERT_TRUE(famp::project::save(projectPath, source, QStringLiteral("0.2.0"), &error))
        << error.toStdString();

    QFile serialized(projectPath);
    ASSERT_TRUE(serialized.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(serialized.readAll()).object();
    EXPECT_EQ(root.value(QStringLiteral("schemaVersion")).toInt(),
              famp::project::SchemaVersion);
    EXPECT_EQ(root.value(QStringLiteral("cloudFiles")).toArray().size(), 1);
    EXPECT_FALSE(root.value(QStringLiteral("cloudFiles")).toArray().at(0)
                     .toString().startsWith(QLatin1Char('/')));

    famp::project::Document loaded;
    ASSERT_TRUE(famp::project::load(projectPath, loaded, &error))
        << error.toStdString();
    EXPECT_EQ(loaded.cloudFiles, QStringList{QFileInfo(cloudPath).canonicalFilePath()});
    EXPECT_EQ(loaded.mapScale, source.mapScale);
}

TEST(ProjectDocumentTest, RejectsInvalidJsonWithoutMutatingOutput)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString projectPath = directory.filePath(QStringLiteral("broken.famp"));
    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_GT(file.write("not json"), 0);
    file.close();

    famp::project::Document document;
    document.cloudFiles = {QStringLiteral("keep.pcd")};
    document.mapScale = QStringLiteral("1:20");
    QString error;
    EXPECT_FALSE(famp::project::load(projectPath, document, &error));
    EXPECT_EQ(document.cloudFiles, QStringList{QStringLiteral("keep.pcd")});
    EXPECT_EQ(document.mapScale, QStringLiteral("1:20"));
    EXPECT_FALSE(error.isEmpty());
}

TEST(ProjectDocumentTest, RejectsUnsupportedSchema)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString projectPath = directory.filePath(QStringLiteral("future.famp"));
    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("FAMP Project")},
        {QStringLiteral("schemaVersion"), 999},
        {QStringLiteral("mapScale"), QStringLiteral("1:50")},
        {QStringLiteral("cloudFiles"), QJsonArray()}};
    file.write(QJsonDocument(root).toJson());
    file.close();

    famp::project::Document document;
    QString error;
    EXPECT_FALSE(famp::project::load(projectPath, document, &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(ProjectDocumentTest, EnforcesProjectSuffix)
{
    EXPECT_EQ(famp::project::pathWithProjectSuffix(QStringLiteral("site")),
              QStringLiteral("site.famp"));
    EXPECT_EQ(famp::project::pathWithProjectSuffix(QStringLiteral("site.FAMP")),
              QStringLiteral("site.FAMP"));
    EXPECT_TRUE(famp::project::pathWithProjectSuffix({}).isEmpty());
}

TEST(ProjectDocumentTest, RejectsUnsupportedScaleWithoutCreatingFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString projectPath = directory.filePath(QStringLiteral("invalid.famp"));
    famp::project::Document document;
    document.mapScale = QStringLiteral("1:25");
    QString error;

    EXPECT_FALSE(famp::project::save(
        projectPath, document, QStringLiteral("0.2.0"), &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(QFileInfo::exists(projectPath));
}
