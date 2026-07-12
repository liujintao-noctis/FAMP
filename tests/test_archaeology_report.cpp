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
    cloud.path = QStringLiteral("/点云/探方.pcd");
    cloud.pointCount = 1234;
    cloud.spatial.origin = {100.0, 200.0, 5.0};
    data.clouds.append(cloud);
    QJsonObject measurement;
    measurement.insert(QStringLiteral("type"), QStringLiteral("measurement"));
    measurement.insert(QStringLiteral("kind"), QStringLiteral("area"));
    measurement.insert(QStringLiteral("meterPoints"), QJsonArray{
        QJsonArray{0.0, 0.0}, QJsonArray{2.0, 0.0},
        QJsonArray{2.0, 3.0}, QJsonArray{0.0, 3.0}});
    data.graphicsState.insert(QStringLiteral("items"), QJsonArray{measurement});
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
    EXPECT_TRUE(html.contains(QStringLiteral("面积")));
    EXPECT_TRUE(html.contains(QStringLiteral("6")));
    EXPECT_TRUE(html.contains(QStringLiteral("10")));
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
