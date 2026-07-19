#include <gtest/gtest.h>

#include "GraphicsExport.h"

#include <QFile>
#include <QBrush>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QImage>
#include <QPen>
#include <QPrinter>
#include <QTemporaryDir>
#include <QtGlobal>

namespace
{
bool hasRedGridPixelNear(const QImage& image, int expectedX, int y)
{
    for (int x = expectedX - 2; x <= expectedX + 2; ++x)
    {
        if (x < 0 || x >= image.width() || y < 0 || y >= image.height())
            continue;
        const QColor color = image.pixelColor(x, y);
        if (color.red() > 220
            && color.green() < 210
            && color.blue() < 210)
        {
            return true;
        }
    }
    return false;
}
}

TEST(GraphicsExportTest, EnforcesFormatSuffixCaseInsensitively)
{
    EXPECT_EQ(famp::exporting::pathWithFormatSuffix(
                  QStringLiteral("map.PDF"), famp::exporting::Format::Pdf),
              QStringLiteral("map.PDF"));
    EXPECT_EQ(famp::exporting::pathWithFormatSuffix(
                  QStringLiteral("map"), famp::exporting::Format::Png),
              QStringLiteral("map.png"));
    EXPECT_EQ(famp::exporting::pathWithFormatSuffix(
                  QStringLiteral("map."), famp::exporting::Format::Bmp),
              QStringLiteral("map.bmp"));
    EXPECT_EQ(famp::exporting::pathWithFormatSuffix(
                  QStringLiteral("map.SVG"), famp::exporting::Format::Svg),
              QStringLiteral("map.SVG"));
}

TEST(GraphicsExportTest, ExportsCustomPaperSvgAtomically)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    scene.addRect(QRectF(0.0, 0.0, 240.0, 120.0),
                  QPen(Qt::black), QBrush(Qt::green));

    famp::exporting::Options options;
    options.format = famp::exporting::Format::Svg;
    options.paperSize = famp::exporting::PaperSize::Custom;
    options.customPageWidthMillimeters = 180.0;
    options.customPageHeightMillimeters = 120.0;
    options.orientation = famp::exporting::Orientation::Landscape;
    options.scaleMode = famp::exporting::ScaleMode::FitToPage;
    options.dotsPerInch = 150;
    const QString path = directory.filePath(QStringLiteral("自定义成果.svg"));
    QString error;

    ASSERT_TRUE(famp::exporting::exportScene(
        &scene, path, options, &error)) << error.toStdString();
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray contents = file.readAll();
    EXPECT_TRUE(contents.contains("<svg"));
    EXPECT_TRUE(contents.contains("viewBox"));
    EXPECT_GT(contents.size(), 200);
}

TEST(GraphicsExportTest, RejectsInvalidCustomPaperWithoutCreatingFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    scene.addText(QStringLiteral("map"));
    famp::exporting::Options options;
    options.format = famp::exporting::Format::Svg;
    options.paperSize = famp::exporting::PaperSize::Custom;
    options.customPageWidthMillimeters = 20.0;
    const QString path = directory.filePath(QStringLiteral("invalid.svg"));
    QString error;

    EXPECT_FALSE(famp::exporting::exportScene(
        &scene, path, options, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("50")));
    EXPECT_FALSE(QFile::exists(path));
}

TEST(GraphicsExportTest, ExportsA4LandscapePngWithPhysicalResolution)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    scene.addRect(QRectF(0.0, 0.0, 100.0, 50.0),
                  QPen(Qt::black), QBrush(Qt::red));

    famp::exporting::Options options;
    options.format = famp::exporting::Format::Png;
    options.paperSize = famp::exporting::PaperSize::A4;
    options.orientation = famp::exporting::Orientation::Landscape;
    options.dotsPerInch = 150;
    const QString requestedPath = directory.filePath(QStringLiteral("成果图"));
    QString error;

    ASSERT_TRUE(famp::exporting::exportScene(
        &scene, requestedPath, options, &error)) << error.toStdString();
    const QImage image(requestedPath + QStringLiteral(".png"));
    ASSERT_FALSE(image.isNull());
    EXPECT_GT(image.width(), image.height());
    EXPECT_NEAR(image.width(), 1754, 2);
    EXPECT_NEAR(image.height(), 1240, 2);
    EXPECT_NEAR(image.dotsPerMeterX(), 5906, 2);
    EXPECT_NEAR(image.dotsPerMeterY(), 5906, 2);

    int minimumX = image.width();
    int maximumX = -1;
    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            if (qRed(line[x]) > 200
                && qGreen(line[x]) < 80
                && qBlue(line[x]) < 80)
            {
                minimumX = qMin(minimumX, x);
                maximumX = qMax(maximumX, x);
            }
        }
    }
    ASSERT_GE(maximumX, minimumX);
    EXPECT_NEAR(maximumX - minimumX + 1, 156, 4);
}

TEST(GraphicsExportTest, ExportsA4RasterGridAtExactlyOneMillimeter)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    scene.addRect(QRectF(0.0, 0.0, 10.0, 10.0), QPen(Qt::black));

    famp::exporting::Options options;
    options.format = famp::exporting::Format::Png;
    options.paperSize = famp::exporting::PaperSize::A4;
    options.orientation = famp::exporting::Orientation::Landscape;
    options.dotsPerInch = 254;
    options.includeMetricGrid = true;
    const QString previewPath = qEnvironmentVariable(
        "FAMP_EXPORT_GRID_PNG_PATH");
    const QString path = previewPath.isEmpty()
        ? directory.filePath(QStringLiteral("一毫米米格.png"))
        : previewPath;
    QString error;

    ASSERT_TRUE(famp::exporting::exportScene(
        &scene, path, options, &error)) << error.toStdString();
    const QImage image(path);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.width(), 2970);
    EXPECT_EQ(image.height(), 2100);
    EXPECT_EQ(image.dotsPerMeterX(), 10000);
    EXPECT_EQ(image.dotsPerMeterY(), 10000);

    // A4 uses a 10 mm export margin. At 254 DPI, both the margin and every
    // following millimetre are exact multiples of 10 pixels.
    constexpr int sampleY = 105;
    for (int expectedX = 110; expectedX <= 290; expectedX += 10)
    {
        EXPECT_TRUE(hasRedGridPixelNear(image, expectedX, sampleY))
            << "missing 1 mm grid line at x=" << expectedX;
    }
    const QColor betweenLines = image.pixelColor(105, sampleY);
    EXPECT_GT(betweenLines.green(), 240);
    EXPECT_GT(betweenLines.blue(), 240);
}

TEST(GraphicsExportTest, ExportsA3RasterGridAtExactlyOneMillimeter)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    scene.addRect(QRectF(0.0, 0.0, 10.0, 10.0), QPen(Qt::black));

    famp::exporting::Options options;
    options.format = famp::exporting::Format::Png;
    options.paperSize = famp::exporting::PaperSize::A3;
    options.orientation = famp::exporting::Orientation::Landscape;
    options.dotsPerInch = 254;
    options.includeMetricGrid = true;
    const QString path = directory.filePath(QStringLiteral("A3一毫米米格.png"));
    QString error;

    ASSERT_TRUE(famp::exporting::exportScene(
        &scene, path, options, &error)) << error.toStdString();
    const QImage image(path);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.width(), 4200);
    EXPECT_EQ(image.height(), 2970);
    EXPECT_EQ(image.dotsPerMeterX(), 10000);
    EXPECT_EQ(image.dotsPerMeterY(), 10000);

    constexpr int sampleY = 105;
    for (int expectedX = 110; expectedX <= 390; expectedX += 10)
    {
        EXPECT_TRUE(hasRedGridPixelNear(image, expectedX, sampleY))
            << "missing A3 1 mm grid line at x=" << expectedX;
    }
}

TEST(GraphicsExportTest, ConfiguresA3PrintPreviewWithExactPhysicalPage)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    scene.addRect(QRectF(0.0, 0.0, 100.0, 50.0), QPen(Qt::black));

    famp::exporting::Options options;
    options.paperSize = famp::exporting::PaperSize::A3;
    options.orientation = famp::exporting::Orientation::Landscape;
    options.scaleMode = famp::exporting::ScaleMode::PreservePhysicalScale;
    options.includeMetricGrid = true;
    options.dotsPerInch = 254;
    options.sceneUnitsPerMillimeterX = 10.0;
    options.sceneUnitsPerMillimeterY = 10.0;
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(
        directory.filePath(QStringLiteral("A3打印预览.pdf")));
    QString error;

    ASSERT_TRUE(famp::exporting::printScene(
        &scene, &printer, options, &error)) << error.toStdString();
    const QRectF pageMillimeters = printer.pageLayout().fullRect(
        QPageLayout::Millimeter);
    EXPECT_NEAR(pageMillimeters.width(), 420.0, 0.01);
    EXPECT_NEAR(pageMillimeters.height(), 297.0, 0.01);
    EXPECT_EQ(printer.resolution(), 254);
    EXPECT_TRUE(QFile::exists(printer.outputFileName()));
}

TEST(GraphicsExportTest, RejectsInexactRasterDpiForMetricGrid)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    scene.addRect(QRectF(0.0, 0.0, 10.0, 10.0));

    famp::exporting::Options options;
    options.format = famp::exporting::Format::Png;
    options.dotsPerInch = 300;
    options.includeMetricGrid = true;
    const QString path = directory.filePath(QStringLiteral("非整数像素米格.png"));
    QString error;

    EXPECT_FALSE(famp::exporting::exportScene(
        &scene, path, options, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("254 或 508 DPI")));
    EXPECT_FALSE(QFile::exists(path));
}

TEST(GraphicsExportTest, ExportsA4SvgWithPhysicalMillimeterGrid)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    scene.addRect(QRectF(0.0, 0.0, 10.0, 10.0), QPen(Qt::black));

    famp::exporting::Options options;
    options.format = famp::exporting::Format::Svg;
    options.paperSize = famp::exporting::PaperSize::A4;
    options.orientation = famp::exporting::Orientation::Landscape;
    options.dotsPerInch = 254;
    options.includeMetricGrid = true;
    const QString previewPath = qEnvironmentVariable(
        "FAMP_EXPORT_GRID_SVG_PATH");
    const QString path = previewPath.isEmpty()
        ? directory.filePath(QStringLiteral("一毫米米格.svg"))
        : previewPath;
    QString error;

    ASSERT_TRUE(famp::exporting::exportScene(
        &scene, path, options, &error)) << error.toStdString();
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray contents = file.readAll();
    EXPECT_TRUE(contents.contains("width=\"297mm\""));
    EXPECT_TRUE(contents.contains("height=\"210mm\""));
    EXPECT_TRUE(contents.contains("viewBox=\"0 0 2970 2100\""));
    EXPECT_TRUE(contents.contains("#ff0000"));
}

TEST(GraphicsExportTest, ExportsAtomicPdfWithMetadata)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    auto* title = scene.addText(QStringLiteral("FAMP Archaeology Map"));
    QFont titleFont = title->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setPos(20.0, 10.0);
    scene.addRect(QRectF(20.0, 60.0, 360.0, 180.0),
                  QPen(Qt::black, 2.0), QBrush(QColor(235, 242, 232)));
    scene.addLine(QLineF(50.0, 200.0, 350.0, 90.0),
                  QPen(QColor(120, 55, 25), 4.0));
    auto* scale = scene.addText(QStringLiteral("Scale 1:50 | EPSG:4490"));
    scale->setPos(20.0, 255.0);

    famp::exporting::Options options;
    options.format = famp::exporting::Format::Pdf;
    options.dotsPerInch = 254;
    options.includeMetricGrid = true;
    options.title = QStringLiteral("Test map");
    options.creator = QStringLiteral("FAMP test");
    const QString previewPath = qEnvironmentVariable("FAMP_EXPORT_PREVIEW_PATH");
    const QString path = previewPath.isEmpty()
        ? directory.filePath(QStringLiteral("专业成果.pdf"))
        : previewPath;
    QString error;

    ASSERT_TRUE(famp::exporting::exportScene(
        &scene, path, options, &error)) << error.toStdString();
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.read(5), QByteArrayLiteral("%PDF-"));
    EXPECT_GT(file.size(), 1000);
}

TEST(GraphicsExportTest, RejectsEmptySceneWithoutCreatingFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    famp::exporting::Options options;
    const QString path = directory.filePath(QStringLiteral("empty.pdf"));
    QString error;

    EXPECT_FALSE(famp::exporting::exportScene(
        &scene, path, options, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(QFile::exists(path));
}

TEST(GraphicsExportTest, RejectsUnsupportedDpiWithoutCreatingFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    scene.addRect(QRectF(0.0, 0.0, 10.0, 10.0));
    famp::exporting::Options options;
    options.format = famp::exporting::Format::Png;
    options.dotsPerInch = 72;
    const QString path = directory.filePath(QStringLiteral("invalid.png"));
    QString error;

    EXPECT_FALSE(famp::exporting::exportScene(
        &scene, path, options, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(QFile::exists(path));
}

TEST(GraphicsExportTest, RejectsOversizedPhysicalScaleButAllowsFitToPage)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    scene.addRect(QRectF(0.0, 0.0, 2000.0, 1200.0));
    famp::exporting::Options options;
    options.format = famp::exporting::Format::Png;
    options.dotsPerInch = 150;
    const QString preservedPath = directory.filePath(QStringLiteral("preserved.png"));
    QString error;

    EXPECT_FALSE(famp::exporting::exportScene(
        &scene, preservedPath, options, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("超出页面")));
    EXPECT_FALSE(QFile::exists(preservedPath));

    options.scaleMode = famp::exporting::ScaleMode::FitToPage;
    const QString fittedPath = directory.filePath(QStringLiteral("fitted.png"));
    error.clear();
    EXPECT_TRUE(famp::exporting::exportScene(
        &scene, fittedPath, options, &error)) << error.toStdString();
    EXPECT_TRUE(QFile::exists(fittedPath));
}

TEST(GraphicsExportTest, RejectsUnknownFormatWithoutCreatingFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    scene.addRect(QRectF(0.0, 0.0, 10.0, 10.0));
    famp::exporting::Options options;
    options.format = static_cast<famp::exporting::Format>(999);
    const QString path = directory.filePath(QStringLiteral("unknown"));
    QString error;

    EXPECT_FALSE(famp::exporting::exportScene(
        &scene, path, options, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("不支持")));
    EXPECT_FALSE(QFile::exists(path));
    EXPECT_FALSE(QFile::exists(path + QStringLiteral(".png")));
}

TEST(GraphicsExportTest, ReportsUnwritableDestination)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QGraphicsScene scene;
    scene.addText(QStringLiteral("map"));
    famp::exporting::Options options;
    options.format = famp::exporting::Format::Pdf;
    const QString path = directory.filePath(
        QStringLiteral("missing-directory/map.pdf"));
    QString error;

    EXPECT_FALSE(famp::exporting::exportScene(
        &scene, path, options, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(QFile::exists(path));
}
