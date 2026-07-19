#include "GraphicsExport.h"

#include "FileIO.h"
#include "MetricGrid.h"

#include <QGraphicsScene>
#include <QImage>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <QSaveFile>
#include <QSvgGenerator>

#include <cmath>

namespace
{
constexpr qint64 MaxRasterPixels = 80'000'000;

bool isRasterFormat(famp::exporting::Format format)
{
    return format == famp::exporting::Format::Png
        || format == famp::exporting::Format::Bmp;
}

bool hasIntegralPixelsPerMillimeter(int dotsPerInch)
{
    const qreal pixelsPerMillimeter = dotsPerInch / 25.4;
    return qAbs(pixelsPerMillimeter
                - std::round(pixelsPerMillimeter)) < 1.0e-9;
}

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

QPageSize pageSize(const famp::exporting::Options& options)
{
    QSizeF millimeters;
    QString name;
    if (options.paperSize == famp::exporting::PaperSize::Custom)
    {
        millimeters = QSizeF(options.customPageWidthMillimeters,
                             options.customPageHeightMillimeters);
        name = QStringLiteral("FAMP custom");
    }
    else if (options.paperSize == famp::exporting::PaperSize::A3)
    {
        millimeters = QSizeF(297.0, 420.0);
        name = QStringLiteral("FAMP A3 exact millimetres");
    }
    else
    {
        millimeters = QSizeF(210.0, 297.0);
        name = QStringLiteral("FAMP A4 exact millimetres");
    }
    return QPageSize(millimeters,
                     QPageSize::Millimeter,
                     name,
                     QPageSize::ExactMatch);
}

QPageLayout::Orientation pageOrientation(
    famp::exporting::Orientation orientation)
{
    return orientation == famp::exporting::Orientation::Landscape
        ? QPageLayout::Landscape
        : QPageLayout::Portrait;
}

QPageLayout makePageLayout(const famp::exporting::Options& options)
{
    // Rendering uses full-page coordinates and applies the requested content
    // margin in exactPaintRectPixels(). Giving the paged device the same
    // margin as well would translate the painter origin a second time, which
    // produces a 20 mm top/left margin and clips the right/bottom of a default
    // 10 mm A4 export.
    const QMarginsF margins;
    return QPageLayout(pageSize(options),
                       pageOrientation(options.orientation),
                       margins,
                       QPageLayout::Millimeter);
}

QSizeF orientedPageMillimeters(const famp::exporting::Options& options)
{
    QSizeF size = pageSize(options).size(QPageSize::Millimeter);
    const bool wantsLandscape =
        options.orientation == famp::exporting::Orientation::Landscape;
    if ((wantsLandscape && size.height() > size.width())
        || (!wantsLandscape && size.width() > size.height()))
    {
        size.transpose();
    }
    return size;
}

QSize exactPagePixels(const famp::exporting::Options& options)
{
    const QSizeF millimeters = orientedPageMillimeters(options);
    const qreal pixelsPerMillimeter = options.dotsPerInch / 25.4;
    return QSize(qRound(millimeters.width() * pixelsPerMillimeter),
                 qRound(millimeters.height() * pixelsPerMillimeter));
}

QRectF exactPaintRectPixels(const famp::exporting::Options& options)
{
    const QSize pagePixels = exactPagePixels(options);
    const qreal margin = options.marginMillimeters
        * options.dotsPerInch / 25.4;
    return QRectF(margin,
                  margin,
                  pagePixels.width() - 2.0 * margin,
                  pagePixels.height() - 2.0 * margin);
}

bool validate(const QString& path,
              const famp::exporting::Options& options,
              QString* errorMessage)
{
    if (path.trimmed().isEmpty())
    {
        setError(errorMessage, QStringLiteral("导出路径不能为空。"));
        return false;
    }
    if (options.format != famp::exporting::Format::Pdf
        && options.format != famp::exporting::Format::Png
        && options.format != famp::exporting::Format::Bmp
        && options.format != famp::exporting::Format::Svg)
    {
        setError(errorMessage, QStringLiteral("不支持的导出格式。"));
        return false;
    }
    if (options.paperSize != famp::exporting::PaperSize::A4
        && options.paperSize != famp::exporting::PaperSize::A3
        && options.paperSize != famp::exporting::PaperSize::Custom)
    {
        setError(errorMessage, QStringLiteral("不支持的纸张尺寸。"));
        return false;
    }
    if (options.paperSize == famp::exporting::PaperSize::Custom
        && (!std::isfinite(options.customPageWidthMillimeters)
            || !std::isfinite(options.customPageHeightMillimeters)
            || options.customPageWidthMillimeters < 50.0
            || options.customPageHeightMillimeters < 50.0
            || options.customPageWidthMillimeters > 2000.0
            || options.customPageHeightMillimeters > 2000.0))
    {
        setError(errorMessage,
                 QStringLiteral("自定义纸张宽度和高度必须在 50 到 2000 毫米之间。"));
        return false;
    }
    if (options.orientation != famp::exporting::Orientation::Portrait
        && options.orientation != famp::exporting::Orientation::Landscape)
    {
        setError(errorMessage, QStringLiteral("不支持的页面方向。"));
        return false;
    }
    if (options.scaleMode != famp::exporting::ScaleMode::PreservePhysicalScale
        && options.scaleMode != famp::exporting::ScaleMode::FitToPage)
    {
        setError(errorMessage, QStringLiteral("不支持的页面缩放模式。"));
        return false;
    }
    if (options.dotsPerInch != 150
        && options.dotsPerInch != 254
        && options.dotsPerInch != 300
        && options.dotsPerInch != 508
        && options.dotsPerInch != 600)
    {
        setError(errorMessage,
                 QStringLiteral(
                     "导出分辨率必须是 150、254、300、508 或 600 DPI。"));
        return false;
    }
    if (options.includeMetricGrid
        && isRasterFormat(options.format)
        && !hasIntegralPixelsPerMillimeter(options.dotsPerInch))
    {
        setError(errorMessage,
                 QStringLiteral(
                     "包含严格 1 mm 米格纸的 PNG/BMP 必须使用 254 或 508 DPI。"));
        return false;
    }
    if (!std::isfinite(options.marginMillimeters)
        || options.marginMillimeters < 0.0
        || options.marginMillimeters > 50.0)
    {
        setError(errorMessage,
                 QStringLiteral("页边距必须在 0 到 50 毫米之间。"));
        return false;
    }
    if (options.scaleMode == famp::exporting::ScaleMode::PreservePhysicalScale
        && (!std::isfinite(options.sceneUnitsPerMillimeterX)
            || !std::isfinite(options.sceneUnitsPerMillimeterY)
            || options.sceneUnitsPerMillimeterX <= 0.0
            || options.sceneUnitsPerMillimeterY <= 0.0))
    {
        setError(errorMessage,
                 QStringLiteral("画布物理比例参数无效。"));
        return false;
    }
    return true;
}

QRectF sourceBounds(QGraphicsScene* scene)
{
    QRectF bounds = scene->itemsBoundingRect();
    if (!bounds.isValid() || bounds.isEmpty())
        return {};

    const qreal padding = qMax<qreal>(5.0,
        qMax(bounds.width(), bounds.height()) * 0.02);
    return bounds.adjusted(-padding, -padding, padding, padding);
}

struct RenderGeometry
{
    QRectF gridBounds;
    QRectF target;
    Qt::AspectRatioMode aspectRatioMode = Qt::KeepAspectRatio;
};

bool makeRenderGeometry(const QRectF& source,
                        const famp::exporting::Options& options,
                        RenderGeometry& geometry,
                        QString* errorMessage)
{
    const QRectF paintRect = exactPaintRectPixels(options);
    geometry.gridBounds = paintRect;
    if (options.scaleMode == famp::exporting::ScaleMode::FitToPage)
    {
        geometry.target = paintRect;
        geometry.aspectRatioMode = Qt::KeepAspectRatio;
        return true;
    }

    const qreal pixelsPerMillimeter = options.dotsPerInch / 25.4;
    const QSizeF requiredSize(
        source.width() * pixelsPerMillimeter / options.sceneUnitsPerMillimeterX,
        source.height() * pixelsPerMillimeter / options.sceneUnitsPerMillimeterY);
    if (requiredSize.width() > paintRect.width() + 0.5
        || requiredSize.height() > paintRect.height() + 0.5)
    {
        setError(errorMessage,
                 QStringLiteral("当前制图比例下内容超出页面；"
                                "请改用更大纸张、调整方向或选择“自动适合页面”。"));
        return false;
    }

    geometry.target = QRectF(
        paintRect.center().x() - requiredSize.width() / 2.0,
        paintRect.center().y() - requiredSize.height() / 2.0,
        requiredSize.width(),
        requiredSize.height());
    geometry.aspectRatioMode = Qt::IgnoreAspectRatio;
    return true;
}

void renderScene(QGraphicsScene* scene,
                 QPainter& painter,
                 const RenderGeometry& geometry,
                 const QRectF& source,
                 const famp::exporting::Options& options)
{
    painter.fillRect(geometry.gridBounds, Qt::white);
    if (options.includeMetricGrid)
    {
        const qreal pixelsPerMillimeter = options.dotsPerInch / 25.4;
        famp::metricgrid::Style style;
        style.minorLineWidth = pixelsPerMillimeter * 0.10;
        style.halfCentimeterLineWidth = pixelsPerMillimeter * 0.18;
        style.centimeterLineWidth = pixelsPerMillimeter * 0.28;
        style.cosmetic = false;
        famp::metricgrid::draw(
            painter,
            geometry.gridBounds,
            QPointF(pixelsPerMillimeter, pixelsPerMillimeter),
            style);
    }
    scene->render(&painter,
                  geometry.target,
                  source,
                  geometry.aspectRatioMode);
}

bool exportPdf(QGraphicsScene* scene,
               const QString& path,
               const QRectF& source,
               const RenderGeometry& geometry,
               const famp::exporting::Options& options,
               QString* errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(errorMessage,
                 QStringLiteral("无法创建 PDF 文件：%1").arg(file.errorString()));
        return false;
    }

    bool rendered = false;
    {
        QPdfWriter writer(&file);
        writer.setPageLayout(makePageLayout(options));
        writer.setResolution(options.dotsPerInch);
        writer.setTitle(options.title);
        writer.setCreator(options.creator);

        QPainter painter(&writer);
        if (painter.isActive())
        {
            renderScene(scene, painter, geometry, source, options);
            rendered = painter.end();
        }
    }

    if (!rendered)
    {
        file.cancelWriting();
        setError(errorMessage, QStringLiteral("无法渲染 PDF 文档。"));
        return false;
    }
    if (!file.commit())
    {
        setError(errorMessage,
                 QStringLiteral("无法提交 PDF 文件：%1").arg(file.errorString()));
        return false;
    }
    return true;
}

bool exportRaster(QGraphicsScene* scene,
                  const QString& path,
                  const QRectF& source,
                  const RenderGeometry& geometry,
                  const famp::exporting::Options& options,
                  QString* errorMessage)
{
    const QSize pagePixels = exactPagePixels(options);
    if (!pagePixels.isValid()
        || static_cast<qint64>(pagePixels.width()) * pagePixels.height()
            > MaxRasterPixels)
    {
        setError(errorMessage,
                 QStringLiteral("导出尺寸过大，请降低 DPI 或使用 PDF。"));
        return false;
    }

    QImage image(pagePixels, QImage::Format_RGB32);
    if (image.isNull())
    {
        setError(errorMessage, QStringLiteral("无法分配导出图像内存。"));
        return false;
    }
    image.setDotsPerMeterX(qRound(options.dotsPerInch / 0.0254));
    image.setDotsPerMeterY(qRound(options.dotsPerInch / 0.0254));
    image.fill(Qt::white);

    QPainter painter(&image);
    if (!painter.isActive())
    {
        setError(errorMessage, QStringLiteral("无法初始化图像绘制器。"));
        return false;
    }
    renderScene(scene, painter, geometry, source, options);
    painter.end();

    const QByteArray format = options.format == famp::exporting::Format::Bmp
        ? QByteArrayLiteral("BMP")
        : QByteArrayLiteral("PNG");
    return famp::io::saveImageAtomically(
        path, image, format.constData(), 100, errorMessage);
}

bool exportSvg(QGraphicsScene* scene,
               const QString& path,
               const QRectF& source,
               const RenderGeometry& geometry,
               const famp::exporting::Options& options,
               QString* errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(errorMessage,
                 QStringLiteral("无法创建 SVG 文件：%1").arg(file.errorString()));
        return false;
    }

    const QSize pagePixels = exactPagePixels(options);
    bool rendered = false;
    {
        QSvgGenerator generator;
        generator.setOutputDevice(&file);
        generator.setSize(pagePixels);
        generator.setViewBox(QRect(QPoint(0, 0), pagePixels));
        generator.setResolution(options.dotsPerInch);
        generator.setTitle(options.title);
        generator.setDescription(
            QStringLiteral("Generated by %1").arg(options.creator));

        QPainter painter(&generator);
        if (painter.isActive())
        {
            renderScene(scene, painter, geometry, source, options);
            rendered = painter.end();
        }
    }

    if (!rendered)
    {
        file.cancelWriting();
        setError(errorMessage, QStringLiteral("无法渲染 SVG 文档。"));
        return false;
    }
    if (!file.commit())
    {
        setError(errorMessage,
                 QStringLiteral("无法提交 SVG 文件：%1").arg(file.errorString()));
        return false;
    }
    return true;
}
}

namespace famp::exporting
{
QString requiredSuffix(Format format)
{
    switch (format)
    {
    case Format::Pdf:
        return QStringLiteral("pdf");
    case Format::Bmp:
        return QStringLiteral("bmp");
    case Format::Svg:
        return QStringLiteral("svg");
    case Format::Png:
    default:
        return QStringLiteral("png");
    }
}

QString pathWithFormatSuffix(const QString& path, Format format)
{
    return famp::io::pathWithRequiredSuffix(path, requiredSuffix(format));
}

bool exportScene(QGraphicsScene* scene,
                 const QString& path,
                 const Options& options,
                 QString* errorMessage)
{
    const QString normalizedPath = pathWithFormatSuffix(path, options.format);
    if (!validate(normalizedPath, options, errorMessage))
        return false;
    if (!scene)
    {
        setError(errorMessage, QStringLiteral("导出画布不存在。"));
        return false;
    }

    const QRectF source = sourceBounds(scene);
    if (source.isEmpty())
    {
        setError(errorMessage, QStringLiteral("画布中没有可导出的图元。"));
        return false;
    }

    RenderGeometry geometry;
    if (!makeRenderGeometry(source, options, geometry, errorMessage))
    {
        return false;
    }

    if (options.format == Format::Pdf)
        return exportPdf(
            scene, normalizedPath, source, geometry, options, errorMessage);
    if (options.format == Format::Svg)
        return exportSvg(
            scene, normalizedPath, source, geometry, options, errorMessage);
    return exportRaster(
        scene, normalizedPath, source, geometry, options, errorMessage);
}

bool printScene(QGraphicsScene* scene,
                QPrinter* printer,
                const Options& options,
                QString* errorMessage)
{
    if (!validate(QStringLiteral("preview.pdf"), options, errorMessage))
        return false;
    if (!scene || !printer)
    {
        setError(errorMessage, QStringLiteral("打印画布或打印设备不存在。"));
        return false;
    }

    const QRectF source = sourceBounds(scene);
    if (source.isEmpty())
    {
        setError(errorMessage, QStringLiteral("画布中没有可预览的图元。"));
        return false;
    }

    printer->setPageLayout(makePageLayout(options));
    // Keep the painter origin at the physical page corner. The program's
    // explicit margin remains visible in previews, PDF printers and native
    // printers without being applied twice by the driver coordinate system.
    printer->setFullPage(true);
    printer->setResolution(options.dotsPerInch);
    printer->setDocName(options.title);
    Options deviceOptions = options;
    // Native printer drivers may choose the nearest supported resolution.
    // Render using the resolution actually accepted by the paged device so a
    // 1 mm grid remains 1 physical millimetre instead of options.dpi pixels.
    if (printer->resolution() > 0)
        deviceOptions.dotsPerInch = printer->resolution();
    RenderGeometry geometry;
    if (!makeRenderGeometry(
            source, deviceOptions, geometry, errorMessage))
    {
        return false;
    }

    QPainter painter(printer);
    if (!painter.isActive())
    {
        setError(errorMessage, QStringLiteral("无法初始化打印预览绘制器。"));
        return false;
    }
    renderScene(scene, painter, geometry, source, deviceOptions);
    if (!painter.end())
    {
        setError(errorMessage, QStringLiteral("无法完成打印预览渲染。"));
        return false;
    }
    return true;
}
}
