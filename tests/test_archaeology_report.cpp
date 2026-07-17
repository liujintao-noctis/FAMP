#include "ArchaeologyReport.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include <limits>

namespace
{
famp::report::Data sampleReport()
{
    famp::report::Data data;
    data.projectName = QStringLiteral("遗址 & 探方");
    data.projectPath = QStringLiteral("/项目/田野.famp-project.json");
    data.projectCrs = QStringLiteral("EPSG:4490");
    data.mapScale = QStringLiteral("1:50");
    data.applicationVersion = QStringLiteral("0.5.0-test");
    data.generatedAt = QDateTime::fromString(QStringLiteral("2026-07-12T12:00:00+08:00"), Qt::ISODate);
    famp::report::CloudEntry cloud;
    cloud.name = QStringLiteral("T1 <表土层>");
    cloud.path = QStringLiteral("/点云/探方.pcd");
    cloud.crs = QStringLiteral("EPSG:4490");
    cloud.pointCount = 1234;
    cloud.spatial.origin = {100.0, 200.0, 5.0};
    cloud.archaeologyFields.insert(
        QStringLiteral("context"), QStringLiteral("Locus & 12"));
    cloud.archaeologyFields.insert(
        QStringLiteral("现场备注"), QStringLiteral("<script>alert(1)</script>"));
    const QVector<famp::cloud::Point3d> controlLocals{
        {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
    for (int index = 0; index < controlLocals.size(); ++index)
    {
        famp::control::Point controlPoint;
        controlPoint.id = famp::control::createPointId();
        controlPoint.name = index == 0
            ? QStringLiteral("GCP <基准>")
            : QStringLiteral("GCP-%1").arg(index + 1);
        controlPoint.local = controlLocals.at(index);
        controlPoint.target = {
            controlPoint.local[0] + cloud.spatial.origin[0],
            controlPoint.local[1] + cloud.spatial.origin[1],
            controlPoint.local[2] + cloud.spatial.origin[2]};
        cloud.controlPoints.append(controlPoint);
    }
    data.clouds.append(cloud);
    QJsonObject measurement;
    measurement.insert(QStringLiteral("type"), QStringLiteral("measurement"));
    measurement.insert(QStringLiteral("kind"), QStringLiteral("area"));
    measurement.insert(QStringLiteral("meterPoints"), QJsonArray{
        QJsonArray{0.0, 0.0}, QJsonArray{2.0, 0.0},
        QJsonArray{2.0, 3.0}, QJsonArray{0.0, 3.0}});
    data.graphicsState.insert(QStringLiteral("items"), QJsonArray{measurement});
    famp::measurement::Record3D measurement3d;
    measurement3d.id =
        QStringLiteral("00000000-0000-0000-0000-000000000001");
    measurement3d.layerId =
        QStringLiteral("00000000-0000-0000-0000-000000000002");
    measurement3d.crs = QStringLiteral("EPSG:4490");
    measurement3d.kind = famp::measurement::Kind::Distance;
    measurement3d.points = {
        QVector3D(0.0F, 0.0F, 0.0F),
        QVector3D(0.0F, 0.0F, 12.0F)};
    data.measurements3d = {measurement3d};
    return data;
}
}

TEST(ArchaeologyReportTest, GeneratesEscapedStructuredHtml)
{
    QString error;
    const QString html = famp::report::toHtml(sampleReport(), &error);
    ASSERT_FALSE(html.isEmpty()) << error.toStdString();
    EXPECT_TRUE(html.contains(QStringLiteral("遗址 &amp; 探方")));
    EXPECT_TRUE(html.contains(QStringLiteral("EPSG:4490")));
    EXPECT_TRUE(html.contains(QStringLiteral("1234")));
    EXPECT_TRUE(html.contains(QStringLiteral("T1 &lt;表土层&gt;")));
    EXPECT_TRUE(html.contains(QStringLiteral("考古图层记录")));
    EXPECT_TRUE(html.contains(QStringLiteral("地层/堆积单位")));
    EXPECT_TRUE(html.contains(QStringLiteral("Locus &amp; 12")));
    EXPECT_TRUE(html.contains(
        QStringLiteral("&lt;script&gt;alert(1)&lt;/script&gt;")));
    EXPECT_FALSE(html.contains(QStringLiteral("<script>alert(1)</script>")));
    EXPECT_TRUE(html.contains(QStringLiteral("控制点与配准质量")));
    EXPECT_TRUE(html.contains(QStringLiteral("GCP &lt;基准&gt;")));
    EXPECT_TRUE(html.contains(QStringLiteral("RMSE")));
    EXPECT_TRUE(html.contains(QStringLiteral("最大残差")));
    EXPECT_TRUE(html.contains(QStringLiteral("面积")));
    EXPECT_TRUE(html.contains(QStringLiteral("6")));
    EXPECT_TRUE(html.contains(QStringLiteral("10")));
    EXPECT_TRUE(html.contains(QStringLiteral("三维点云测量成果")));
    EXPECT_TRUE(html.contains(QStringLiteral("12.000 m")));
    EXPECT_TRUE(html.contains(
        QStringLiteral("00000000-0000-0000-0000-000000000002")));
}

TEST(ArchaeologyReportTest, SavesUnicodeHtmlAndPdf)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QString error;
    const QString htmlPath = directory.filePath(QStringLiteral("考古报告.html"));
    const QString pdfPath = directory.filePath(QStringLiteral("考古报告.pdf"));
    ASSERT_TRUE(famp::report::saveHtml(htmlPath, sampleReport(), &error))
        << error.toStdString();
    ASSERT_TRUE(famp::report::savePdf(pdfPath, sampleReport(), &error))
        << error.toStdString();
    QFile pdf(pdfPath);
    ASSERT_TRUE(pdf.open(QIODevice::ReadOnly));
    EXPECT_TRUE(pdf.read(5).startsWith("%PDF-"));
}

TEST(ArchaeologyReportTest, RejectsInvalidTimestampWithoutWriting)
{
    QTemporaryDir directory;
    auto data = sampleReport();
    data.generatedAt = {};
    const QString path = directory.filePath(QStringLiteral("invalid.html"));
    EXPECT_FALSE(famp::report::saveHtml(path, data));
    EXPECT_FALSE(QFile::exists(path));
}

TEST(ArchaeologyReportTest, RejectsInvalidCloudSpatialReference)
{
    auto data = sampleReport();
    data.clouds[0].spatial.origin[1] =
        std::numeric_limits<double>::quiet_NaN();
    QString error;
    EXPECT_TRUE(famp::report::toHtml(data, &error).isEmpty());
    EXPECT_TRUE(error.contains(QStringLiteral("空间参考")));
}

TEST(ArchaeologyReportTest, RejectsInvalidThreeDimensionalMeasurement)
{
    auto data = sampleReport();
    data.measurements3d[0].id = QStringLiteral("invalid-id");
    QString error;
    EXPECT_TRUE(famp::report::toHtml(data, &error).isEmpty());
    EXPECT_TRUE(error.contains(QStringLiteral("三维测量")));
}

TEST(ArchaeologyReportTest, RejectsInvalidArchaeologyFields)
{
    auto data = sampleReport();
    data.clouds[0].archaeologyFields.insert(
        QString(), QStringLiteral("invalid"));
    QString error;
    EXPECT_TRUE(famp::report::toHtml(data, &error).isEmpty());
    EXPECT_TRUE(error.contains(QStringLiteral("考古图层字段")));
}

TEST(ArchaeologyReportTest, RejectsInvalidControlPoints)
{
    auto data = sampleReport();
    data.clouds[0].controlPoints[1].id =
        data.clouds[0].controlPoints[0].id;
    QString error;
    EXPECT_TRUE(famp::report::toHtml(data, &error).isEmpty());
    EXPECT_TRUE(error.contains(QStringLiteral("控制点")));
}
