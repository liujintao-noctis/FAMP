#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ProjectDocument.h"
#include "CloudLayer.h"

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

TEST(ProjectDocumentTest, RoundTripsSchemaThreeLayerState)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString cloudPath = directory.filePath(QStringLiteral("遗址点云.pcd"));
    QFile cloudFile(cloudPath);
    ASSERT_TRUE(cloudFile.open(QIODevice::WriteOnly));
    cloudFile.write("placeholder");
    cloudFile.close();

    famp::project::CloudReference cloud;
    cloud.path = cloudPath;
    cloud.layerId = famp::cloud::createLayerId();
    cloud.name = QStringLiteral("T1 探方表土层");
    cloud.crs = QStringLiteral("epsg:4490");
    cloud.visible = false;
    cloud.locked = true;
    cloud.display.pointSize = 4.5;
    cloud.display.opacity = 0.65;
    cloud.display.colorMode = famp::display::ColorMode::Attribute;
    cloud.display.attributeName = QStringLiteral("intensity");
    cloud.display.automaticScalarRange = false;
    cloud.display.scalarMinimum = 10.0;
    cloud.display.scalarMaximum = 20.0;
    famp::cloud::AttributeSummary intensity;
    intensity.name = QStringLiteral("intensity");
    intensity.unit = QStringLiteral("raw");
    intensity.type = famp::cloud::AttributeValueType::UnsignedInteger;
    intensity.valueCount = 3;
    intensity.finiteValueCount = 3;
    intensity.hasFiniteRange = true;
    intensity.minimum = 2.0;
    intensity.maximum = 99.0;
    cloud.attributes = {intensity};
    cloud.archaeologyFields.insert(
        QStringLiteral("context"), QStringLiteral("Locus-12"));
    famp::control::Point controlPoint;
    controlPoint.id = famp::control::createPointId();
    controlPoint.name = QStringLiteral("基准点 A");
    controlPoint.local = {1.25, -2.5, 0.75};
    controlPoint.target = {500001.25, 3400002.5, 10.75};
    controlPoint.enabled = false;
    cloud.controlPoints = {controlPoint};

    famp::project::Document source;
    source.projectCrs = QStringLiteral("EPSG:4490");
    source.clouds = {cloud};
    famp::measurement::Record3D measurement;
    measurement.id = famp::measurement::createRecordId();
    measurement.layerId = cloud.layerId;
    measurement.crs = QStringLiteral("epsg:4490");
    measurement.kind = famp::measurement::Kind::Distance;
    measurement.points = {
        QVector3D(0.0F, 0.0F, 0.0F),
        QVector3D(3.0F, 4.0F, 0.0F)};
    source.measurements3d = {measurement};
    const QString projectPath = directory.filePath(QStringLiteral("schema3.famp"));
    QString error;
    ASSERT_TRUE(famp::project::save(
        projectPath, source, QStringLiteral("0.6.0"), &error))
        << error.toStdString();

    famp::project::Document loaded;
    ASSERT_TRUE(famp::project::load(projectPath, loaded, &error))
        << error.toStdString();
    ASSERT_EQ(loaded.clouds.size(), 1);
    const auto& result = loaded.clouds.front();
    EXPECT_EQ(result.layerId, cloud.layerId);
    EXPECT_EQ(result.name, cloud.name);
    EXPECT_EQ(result.crs, QStringLiteral("EPSG:4490"));
    EXPECT_FALSE(result.visible);
    EXPECT_TRUE(result.locked);
    EXPECT_DOUBLE_EQ(result.display.pointSize, 4.5);
    EXPECT_DOUBLE_EQ(result.display.opacity, 0.65);
    EXPECT_EQ(result.display.colorMode, famp::display::ColorMode::Attribute);
    EXPECT_EQ(result.display.attributeName, QStringLiteral("intensity"));
    ASSERT_EQ(result.attributes.size(), 1);
    EXPECT_EQ(result.attributes.front().name, QStringLiteral("intensity"));
    EXPECT_EQ(result.attributes.front().valueCount, 3);
    EXPECT_EQ(result.archaeologyFields.value(QStringLiteral("context")),
              QStringLiteral("Locus-12"));
    ASSERT_EQ(result.controlPoints.size(), 1);
    EXPECT_EQ(result.controlPoints.front().id, controlPoint.id);
    EXPECT_EQ(result.controlPoints.front().name, controlPoint.name);
    EXPECT_EQ(result.controlPoints.front().local, controlPoint.local);
    EXPECT_EQ(result.controlPoints.front().target, controlPoint.target);
    EXPECT_FALSE(result.controlPoints.front().enabled);
    ASSERT_EQ(loaded.measurements3d.size(), 1);
    const auto& loadedMeasurement = loaded.measurements3d.front();
    EXPECT_EQ(loadedMeasurement.id, measurement.id);
    EXPECT_EQ(loadedMeasurement.layerId, cloud.layerId);
    EXPECT_EQ(loadedMeasurement.crs, QStringLiteral("EPSG:4490"));
    EXPECT_EQ(loadedMeasurement.kind, famp::measurement::Kind::Distance);
    EXPECT_EQ(loadedMeasurement.points, measurement.points);
}

TEST(ProjectDocumentTest, MigratesSchemaTwoLayerDefaultsDeterministically)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString cloudPath = directory.filePath(QStringLiteral("legacy.pcd"));
    QFile cloudFile(cloudPath);
    ASSERT_TRUE(cloudFile.open(QIODevice::WriteOnly));
    cloudFile.close();
    const QString projectPath = directory.filePath(QStringLiteral("legacy-v2.famp"));

    famp::project::Document source;
    source.cloudFiles = {cloudPath};
    QString error;
    ASSERT_TRUE(famp::project::save(
        projectPath, source, QStringLiteral("0.5.2"), &error));
    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    root.insert(QStringLiteral("schemaVersion"), 2);
    root.remove(QStringLiteral("measurements3d"));
    QJsonArray clouds = root.value(QStringLiteral("clouds")).toArray();
    QJsonObject serialized = clouds.at(0).toObject();
    for (const QString& field : {
             QStringLiteral("layerId"), QStringLiteral("name"),
             QStringLiteral("crs"), QStringLiteral("locked"),
             QStringLiteral("display"), QStringLiteral("attributes"),
             QStringLiteral("archaeologyFields")})
    {
        serialized.remove(field);
    }
    clouds[0] = serialized;
    root.insert(QStringLiteral("clouds"), clouds);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(root).toJson());
    file.close();

    famp::project::Document first;
    famp::project::Document second;
    ASSERT_TRUE(famp::project::load(projectPath, first, &error))
        << error.toStdString();
    ASSERT_TRUE(famp::project::load(projectPath, second, &error))
        << error.toStdString();
    ASSERT_EQ(first.clouds.size(), 1);
    EXPECT_EQ(first.clouds.front().layerId, second.clouds.front().layerId);
    EXPECT_TRUE(famp::cloud::isValidLayerId(first.clouds.front().layerId));
    EXPECT_EQ(first.clouds.front().name, QStringLiteral("legacy.pcd"));
    EXPECT_FALSE(first.clouds.front().locked);
    EXPECT_TRUE(first.clouds.front().attributes.isEmpty());
    EXPECT_TRUE(first.measurements3d.isEmpty());
}

TEST(ProjectDocumentTest, LoadsEarlySchemaThreeDisplayWithoutAttributeName)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString cloudPath = directory.filePath(QStringLiteral("early-v3.pcd"));
    QFile cloudFile(cloudPath);
    ASSERT_TRUE(cloudFile.open(QIODevice::WriteOnly));
    cloudFile.write("placeholder");
    cloudFile.close();

    famp::project::CloudReference cloud;
    cloud.path = cloudPath;
    cloud.layerId = famp::cloud::createLayerId();
    cloud.name = QStringLiteral("early-v3.pcd");
    famp::project::Document source;
    source.clouds = {cloud};
    const QString projectPath = directory.filePath(QStringLiteral("early-v3.famp"));
    QString error;
    ASSERT_TRUE(famp::project::save(
        projectPath, source, QStringLiteral("0.6.0"), &error));

    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QJsonArray clouds = root.value(QStringLiteral("clouds")).toArray();
    QJsonObject serializedCloud = clouds.at(0).toObject();
    QJsonObject display = serializedCloud.value(QStringLiteral("display")).toObject();
    display.remove(QStringLiteral("attributeName"));
    serializedCloud.insert(QStringLiteral("display"), display);
    clouds[0] = serializedCloud;
    root.insert(QStringLiteral("clouds"), clouds);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(root).toJson());
    file.close();

    famp::project::Document loaded;
    ASSERT_TRUE(famp::project::load(projectPath, loaded, &error))
        << error.toStdString();
    ASSERT_EQ(loaded.clouds.size(), 1);
    EXPECT_EQ(loaded.clouds.front().display.colorMode,
              famp::display::ColorMode::PointRgb);
    EXPECT_TRUE(loaded.clouds.front().display.attributeName.isEmpty());
}

TEST(ProjectDocumentTest, RejectsMissingDisplayAttributeBeforeWriting)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString cloudPath = directory.filePath(QStringLiteral("attribute.pcd"));
    QFile cloudFile(cloudPath);
    ASSERT_TRUE(cloudFile.open(QIODevice::WriteOnly));
    cloudFile.close();

    famp::project::CloudReference cloud;
    cloud.path = cloudPath;
    cloud.layerId = famp::cloud::createLayerId();
    cloud.display.colorMode = famp::display::ColorMode::Attribute;
    cloud.display.attributeName = QStringLiteral("missing");
    famp::project::Document document;
    document.clouds = {cloud};
    const QString projectPath = directory.filePath(QStringLiteral("invalid.famp"));
    QString error;
    EXPECT_FALSE(famp::project::save(
        projectPath, document, QStringLiteral("0.6.0"), &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(QFileInfo::exists(projectPath));
}

TEST(ProjectDocumentTest, RejectsMeasurementForUnknownLayerBeforeWriting)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString cloudPath = directory.filePath(QStringLiteral("known.pcd"));
    QFile cloudFile(cloudPath);
    ASSERT_TRUE(cloudFile.open(QIODevice::WriteOnly));
    cloudFile.close();

    famp::project::CloudReference cloud;
    cloud.path = cloudPath;
    cloud.layerId = famp::cloud::createLayerId();
    famp::measurement::Record3D measurement;
    measurement.id = famp::measurement::createRecordId();
    measurement.layerId = famp::cloud::createLayerId();
    measurement.kind = famp::measurement::Kind::Distance;
    measurement.points = {
        QVector3D(0.0F, 0.0F, 0.0F),
        QVector3D(1.0F, 0.0F, 0.0F)};

    famp::project::Document document;
    document.clouds = {cloud};
    document.measurements3d = {measurement};
    const QString projectPath = directory.filePath(
        QStringLiteral("unknown-layer.famp"));
    QString error;
    EXPECT_FALSE(famp::project::save(
        projectPath, document, QStringLiteral("0.6.0"), &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(QFileInfo::exists(projectPath));
}

TEST(ProjectDocumentTest, RejectsMeasurementWithStaleCoordinateSystem)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString cloudPath = directory.filePath(QStringLiteral("crs.pcd"));
    QFile cloudFile(cloudPath);
    ASSERT_TRUE(cloudFile.open(QIODevice::WriteOnly));
    cloudFile.close();

    famp::project::CloudReference cloud;
    cloud.path = cloudPath;
    cloud.layerId = famp::cloud::createLayerId();
    cloud.crs = QStringLiteral("EPSG:4490");
    famp::measurement::Record3D measurement;
    measurement.id = famp::measurement::createRecordId();
    measurement.layerId = cloud.layerId;
    measurement.crs = QStringLiteral("EPSG:3857");
    measurement.points = {
        QVector3D(0.0F, 0.0F, 0.0F),
        QVector3D(1.0F, 0.0F, 0.0F)};

    famp::project::Document document;
    document.clouds = {cloud};
    document.measurements3d = {measurement};
    const QString projectPath = directory.filePath(
        QStringLiteral("stale-crs.famp"));
    QString error;
    EXPECT_FALSE(famp::project::save(
        projectPath, document, QStringLiteral("0.6.0"), &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(QFileInfo::exists(projectPath));
}

TEST(ProjectDocumentTest, RejectsDuplicateLayerIdsBeforeWriting)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString firstPath = directory.filePath(QStringLiteral("first.pcd"));
    const QString secondPath = directory.filePath(QStringLiteral("second.pcd"));
    for (const QString& path : {firstPath, secondPath})
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    }

    const QString duplicateId = famp::cloud::createLayerId();
    famp::project::CloudReference first;
    first.path = firstPath;
    first.layerId = duplicateId;
    famp::project::CloudReference second;
    second.path = secondPath;
    second.layerId = duplicateId;
    famp::project::Document document;
    document.clouds = {first, second};
    const QString projectPath = directory.filePath(QStringLiteral("duplicate.famp"));
    QString error;
    EXPECT_FALSE(famp::project::save(
        projectPath, document, QStringLiteral("0.6.0"), &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(QFileInfo::exists(projectPath));
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

TEST(ProjectDocumentTest, RejectsUnsafeNumericCloudMetadata)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString cloudPath = directory.filePath(QStringLiteral("site.pcd"));
    QFile cloudFile(cloudPath);
    ASSERT_TRUE(cloudFile.open(QIODevice::WriteOnly));
    cloudFile.close();
    famp::project::CloudReference cloud;
    cloud.path = cloudPath;
    famp::project::Document source;
    source.clouds = {cloud};
    const QString projectPath = directory.filePath(QStringLiteral("unsafe.famp"));
    QString error;
    ASSERT_TRUE(famp::project::save(
        projectPath, source, QStringLiteral("0.5.0"), &error));

    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QJsonArray clouds = root.value(QStringLiteral("clouds")).toArray();
    QJsonObject serializedCloud = clouds.at(0).toObject();
    serializedCloud.insert(QStringLiteral("size"), 1.0e300);
    clouds[0] = serializedCloud;
    root.insert(QStringLiteral("clouds"), clouds);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(root).toJson());
    file.close();

    famp::project::Document loaded;
    EXPECT_FALSE(famp::project::load(projectPath, loaded, &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(ProjectDocumentTest, RejectsDuplicateControlPointsWithoutMutation)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString cloudPath = directory.filePath(QStringLiteral("control.pcd"));
    QFile cloudFile(cloudPath);
    ASSERT_TRUE(cloudFile.open(QIODevice::WriteOnly));
    cloudFile.close();
    famp::project::CloudReference cloud;
    cloud.path = cloudPath;
    famp::project::Document source;
    source.clouds = {cloud};
    const QString projectPath = directory.filePath(
        QStringLiteral("invalid-control.famp"));
    QString error;
    ASSERT_TRUE(famp::project::save(
        projectPath, source, QStringLiteral("0.6.0"), &error));

    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QJsonArray clouds = root.value(QStringLiteral("clouds")).toArray();
    QJsonObject serializedCloud = clouds.at(0).toObject();
    const QString duplicateId = famp::control::createPointId();
    const QJsonObject first{
        {QStringLiteral("id"), duplicateId},
        {QStringLiteral("name"), QStringLiteral("CP-1")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("local"), QJsonArray{0.0, 0.0, 0.0}},
        {QStringLiteral("target"), QJsonArray{1.0, 2.0, 3.0}}};
    QJsonObject second = first;
    second.insert(QStringLiteral("name"), QStringLiteral("CP-2"));
    serializedCloud.insert(
        QStringLiteral("controlPoints"), QJsonArray{first, second});
    clouds[0] = serializedCloud;
    root.insert(QStringLiteral("clouds"), clouds);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(root).toJson());
    file.close();

    famp::project::Document loaded;
    loaded.mapScale = QStringLiteral("1:20");
    EXPECT_FALSE(famp::project::load(projectPath, loaded, &error));
    EXPECT_EQ(loaded.mapScale, QStringLiteral("1:20"));
    EXPECT_FALSE(error.isEmpty());
}
