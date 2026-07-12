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
    source.projectCrs = QStringLiteral("epsg:4490");
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
    EXPECT_EQ(loaded.projectCrs, QStringLiteral("EPSG:4490"));
}

TEST(ProjectDocumentTest, RoundTripsGraphicsAndWindowState)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString projectPath = directory.filePath(QStringLiteral("完整项目.famp"));
    famp::project::Document source;
    source.graphicsState = QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("sceneRect"), QJsonArray{-10.0, -20.0, 30.0, 40.0}},
        {QStringLiteral("items"), QJsonArray()},
        {QStringLiteral("metricGridVisible"), true}};
    source.windowGeometry = QByteArray::fromHex("01020304aabb");
    source.windowState = QByteArray::fromHex("10203040ccdd");
    source.xoyLabelVisible = false;
    source.scaleVisible = false;
    QString error;

    ASSERT_TRUE(famp::project::save(
        projectPath, source, QStringLiteral("0.3.1"), &error))
        << error.toStdString();
    famp::project::Document loaded;
    ASSERT_TRUE(famp::project::load(projectPath, loaded, &error))
        << error.toStdString();
    EXPECT_EQ(loaded.graphicsState, source.graphicsState);
    EXPECT_EQ(loaded.windowGeometry, source.windowGeometry);
    EXPECT_EQ(loaded.windowState, source.windowState);
    EXPECT_FALSE(loaded.xoyLabelVisible);
    EXPECT_FALSE(loaded.scaleVisible);
}

TEST(ProjectDocumentTest, PreservesCloudMetadataAndUsesAbsoluteFallback)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_TRUE(QDir(directory.path()).mkpath(QStringLiteral("source")));
    ASSERT_TRUE(QDir(directory.path()).mkpath(QStringLiteral("projects/a")));
    ASSERT_TRUE(QDir(directory.path()).mkpath(QStringLiteral("projects/b")));
    const QString cloudPath = directory.filePath(QStringLiteral("source/site.pcd"));
    QFile cloudFile(cloudPath);
    ASSERT_TRUE(cloudFile.open(QIODevice::WriteOnly));
    ASSERT_EQ(cloudFile.write("pcd-placeholder"), 15);
    cloudFile.close();

    famp::project::CloudReference cloud;
    cloud.path = cloudPath;
    cloud.visible = false;
    cloud.sha256 = QByteArray(32, '\x5a');
    cloud.spatial.origin = {123456.25, 3456789.5, 88.75};
    cloud.spatial.transform[3] = 12.0;
    cloud.spatial.transform[7] = -4.0;
    famp::project::Document source;
    source.clouds = {cloud};
    const QString originalProject = directory.filePath(
        QStringLiteral("projects/a/site.famp"));
    QString error;
    ASSERT_TRUE(famp::project::save(
        originalProject, source, QStringLiteral("0.3.1"), &error))
        << error.toStdString();

    const QString movedProject = directory.filePath(
        QStringLiteral("projects/b/site.famp"));
    ASSERT_TRUE(QFile::copy(originalProject, movedProject));
    famp::project::Document loaded;
    ASSERT_TRUE(famp::project::load(movedProject, loaded, &error))
        << error.toStdString();
    ASSERT_EQ(loaded.clouds.size(), 1);
    EXPECT_EQ(loaded.clouds.front().path,
              QFileInfo(cloudPath).canonicalFilePath());
    EXPECT_EQ(loaded.cloudFiles,
              QStringList{QFileInfo(cloudPath).canonicalFilePath()});
    EXPECT_EQ(loaded.clouds.front().size, 15);
    EXPECT_FALSE(loaded.clouds.front().visible);
    EXPECT_EQ(loaded.clouds.front().sha256, QByteArray(32, '\x5a'));
    EXPECT_EQ(loaded.clouds.front().spatial.origin, cloud.spatial.origin);
    EXPECT_EQ(loaded.clouds.front().spatial.transform,
              cloud.spatial.transform);
}

TEST(ProjectDocumentTest, LoadsSchemaOneWithSafeDefaults)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString projectPath = directory.filePath(QStringLiteral("legacy.famp"));
    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("FAMP Project")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("mapScale"), QStringLiteral("1:50")},
        {QStringLiteral("projectCrs"), QString()},
        {QStringLiteral("cloudFiles"), QJsonArray()}};
    file.write(QJsonDocument(root).toJson());
    file.close();

    famp::project::Document loaded;
    QString error;
    ASSERT_TRUE(famp::project::load(projectPath, loaded, &error))
        << error.toStdString();
    EXPECT_TRUE(loaded.graphicsState.isEmpty());
    EXPECT_TRUE(loaded.windowGeometry.isEmpty());
    EXPECT_TRUE(loaded.windowState.isEmpty());
    EXPECT_TRUE(loaded.xoyLabelVisible);
    EXPECT_TRUE(loaded.scaleVisible);
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

TEST(ProjectDocumentTest, RejectsMalformedProjectCrs)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString projectPath = directory.filePath(QStringLiteral("invalid-crs.famp"));
    famp::project::Document document;
    document.projectCrs = QStringLiteral("not-an-epsg-code");
    QString error;

    EXPECT_FALSE(famp::project::save(
        projectPath, document, QStringLiteral("0.2.0"), &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(QFileInfo::exists(projectPath));
}
