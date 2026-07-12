#pragma once

#include <QString>

class QGraphicsScene;
class QPrinter;

namespace famp::exporting
{
enum class Format
{
    Pdf,
    Png,
    Bmp,
    Svg
};

enum class PaperSize
{
    A4,
    A3,
    Custom
};

enum class Orientation
{
    Portrait,
    Landscape
};

enum class ScaleMode
{
    PreservePhysicalScale,
    FitToPage
};

struct Options
{
    Format format = Format::Pdf;
    PaperSize paperSize = PaperSize::A4;
    qreal customPageWidthMillimeters = 210.0;
    qreal customPageHeightMillimeters = 297.0;
    Orientation orientation = Orientation::Landscape;
    ScaleMode scaleMode = ScaleMode::PreservePhysicalScale;
    int dotsPerInch = 300;
    qreal marginMillimeters = 10.0;
    qreal sceneUnitsPerMillimeterX = 96.0 / 25.4;
    qreal sceneUnitsPerMillimeterY = 96.0 / 25.4;
    QString title;
    QString creator = QStringLiteral("FAMP");
};

QString requiredSuffix(Format format);
QString pathWithFormatSuffix(const QString& path, Format format);
bool exportScene(QGraphicsScene* scene,
                 const QString& path,
                 const Options& options,
                 QString* errorMessage = nullptr);
bool printScene(QGraphicsScene* scene,
                QPrinter* printer,
                const Options& options,
                QString* errorMessage = nullptr);
}
