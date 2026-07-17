#include "ContourItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
constexpr double MaximumCoordinateMagnitude = 1.0e15;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool finiteCoordinate(double value)
{
    return std::isfinite(value)
        && std::abs(value) <= MaximumCoordinateMagnitude;
}

bool majorContour(double elevation, double interval, double base)
{
    const double index = (elevation - base) / interval;
    const double rounded = std::round(index);
    return std::abs(index - rounded) <= 1.0e-7
        * std::max(1.0, std::abs(index))
        && std::fmod(std::abs(rounded), 5.0) < 0.5;
}
}

ContourItem::ContourItem(ContourItemData data,
                         const QPointF& sceneUnitsPerMeter,
                         QGraphicsItem* parent)
    : QGraphicsItem(parent)
    , data_(std::move(data))
    , sceneUnitsPerMeter_(sceneUnitsPerMeter)
{
    setFlags(QGraphicsItem::ItemIsMovable
             | QGraphicsItem::ItemIsSelectable
             | QGraphicsItem::ItemIsFocusable);
    setCursor(Qt::SizeAllCursor);
    setZValue(50.0);
    rebuildGeometry();
}

QRectF ContourItem::boundingRect() const
{
    return bounds_;
}

void ContourItem::paint(QPainter* painter,
                        const QStyleOptionGraphicsItem* option,
                        QWidget*)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QPen minorPen(QColor(154, 92, 42));
    minorPen.setWidthF(1.0);
    minorPen.setCosmetic(true);
    minorPen.setJoinStyle(Qt::RoundJoin);
    minorPen.setCapStyle(Qt::RoundCap);
    painter->setPen(minorPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(minorPath_);

    QPen majorPen(QColor(103, 58, 22));
    majorPen.setWidthF(2.0);
    majorPen.setCosmetic(true);
    majorPen.setJoinStyle(Qt::RoundJoin);
    majorPen.setCapStyle(Qt::RoundCap);
    painter->setPen(majorPen);
    painter->drawPath(majorPath_);

    if (option && (option->state & QStyle::State_Selected))
    {
        QPen selectionPen(QColor(0, 120, 215));
        selectionPen.setWidthF(1.0);
        selectionPen.setCosmetic(true);
        selectionPen.setStyle(Qt::DashLine);
        painter->setPen(selectionPen);
        painter->drawRect(bounds_);
    }
    painter->restore();
}

void ContourItem::setSceneUnitsPerMeter(
    const QPointF& sceneUnitsPerMeter)
{
    if (!std::isfinite(sceneUnitsPerMeter.x())
        || !std::isfinite(sceneUnitsPerMeter.y())
        || sceneUnitsPerMeter.x() <= 0.0 || sceneUnitsPerMeter.y() <= 0.0)
    {
        return;
    }
    if (qFuzzyCompare(sceneUnitsPerMeter_.x() + 1.0,
                      sceneUnitsPerMeter.x() + 1.0)
        && qFuzzyCompare(sceneUnitsPerMeter_.y() + 1.0,
                         sceneUnitsPerMeter.y() + 1.0))
    {
        return;
    }
    prepareGeometryChange();
    sceneUnitsPerMeter_ = sceneUnitsPerMeter;
    rebuildGeometry();
    update();
}

QVector<famp::terrain::ContourLine> ContourItem::absoluteLines() const
{
    QVector<famp::terrain::ContourLine> result = data_.relativeLines;
    for (auto& line : result)
    {
        for (auto& point : line.points)
        {
            point[0] += data_.originX;
            point[1] += data_.originY;
        }
    }
    return result;
}

quint64 ContourItem::pointCount() const
{
    quint64 count = 0;
    for (const auto& line : data_.relativeLines)
        count += static_cast<quint64>(line.points.size());
    return count;
}

bool ContourItem::createDataFromAbsolute(
    const QVector<famp::terrain::ContourLine>& lines,
    double horizontalUnitToMetre,
    const QString& sourceCrs,
    const QString& sourceLayerId,
    const QString& sourceLayerName,
    const QString& demPath,
    double interval,
    double baseElevation,
    ContourItemData& data,
    QString* errorMessage)
{
    if (lines.isEmpty())
    {
        setError(errorMessage, QStringLiteral("没有可添加到二维画布的等高线。"));
        return false;
    }
    double minimumX = std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    quint64 count = 0;
    for (const auto& line : lines)
    {
        if (!std::isfinite(line.elevation) || line.points.size() < 2)
        {
            setError(errorMessage, QStringLiteral("等高线数据无效。"));
            return false;
        }
        count += static_cast<quint64>(line.points.size());
        if (count > MaximumDisplayPoints)
        {
            setError(errorMessage,
                     QStringLiteral("等高线包含 %1 个点，超过二维画布 %2 个点的安全上限；仍可导出文件。")
                         .arg(count).arg(MaximumDisplayPoints));
            return false;
        }
        for (const auto& point : line.points)
        {
            if (!finiteCoordinate(point[0]) || !finiteCoordinate(point[1]))
            {
                setError(errorMessage, QStringLiteral("等高线包含无效坐标。"));
                return false;
            }
            minimumX = std::min(minimumX, point[0]);
            minimumY = std::min(minimumY, point[1]);
        }
    }

    ContourItemData candidate;
    candidate.originX = minimumX;
    candidate.originY = minimumY;
    candidate.horizontalUnitToMetre = horizontalUnitToMetre;
    candidate.sourceCrs = sourceCrs;
    candidate.sourceLayerId = sourceLayerId;
    candidate.sourceLayerName = sourceLayerName;
    candidate.demPath = demPath;
    candidate.interval = interval;
    candidate.baseElevation = baseElevation;
    candidate.relativeLines = lines;
    for (auto& line : candidate.relativeLines)
    {
        for (auto& point : line.points)
        {
            point[0] -= minimumX;
            point[1] -= minimumY;
        }
    }
    if (!validateData(candidate, errorMessage))
        return false;
    data = std::move(candidate);
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool ContourItem::validateData(const ContourItemData& data,
                               QString* errorMessage)
{
    if (!finiteCoordinate(data.originX) || !finiteCoordinate(data.originY)
        || !std::isfinite(data.horizontalUnitToMetre)
        || data.horizontalUnitToMetre <= 0.0
        || data.horizontalUnitToMetre > 1.0e12
        || !std::isfinite(data.interval) || data.interval <= 0.0
        || !std::isfinite(data.baseElevation)
        || data.relativeLines.isEmpty())
    {
        setError(errorMessage, QStringLiteral("等高线图元元数据无效。"));
        return false;
    }
    quint64 count = 0;
    for (const auto& line : data.relativeLines)
    {
        if (!std::isfinite(line.elevation) || line.points.size() < 2)
        {
            setError(errorMessage, QStringLiteral("等高线图元线条无效。"));
            return false;
        }
        count += static_cast<quint64>(line.points.size());
        if (count > MaximumDisplayPoints)
        {
            setError(errorMessage, QStringLiteral("等高线图元点数超过安全上限。"));
            return false;
        }
        for (const auto& point : line.points)
        {
            if (!finiteCoordinate(point[0]) || !finiteCoordinate(point[1]))
            {
                setError(errorMessage, QStringLiteral("等高线图元坐标无效。"));
                return false;
            }
        }
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

void ContourItem::rebuildGeometry()
{
    minorPath_ = QPainterPath();
    majorPath_ = QPainterPath();
    const double scaleX = data_.horizontalUnitToMetre
        * sceneUnitsPerMeter_.x();
    const double scaleY = data_.horizontalUnitToMetre
        * sceneUnitsPerMeter_.y();
    for (const auto& line : data_.relativeLines)
    {
        QPainterPath& path = majorContour(
            line.elevation, data_.interval, data_.baseElevation)
            ? majorPath_ : minorPath_;
        const auto& first = line.points.front();
        path.moveTo(first[0] * scaleX, -first[1] * scaleY);
        for (qsizetype index = 1; index < line.points.size(); ++index)
        {
            const auto& point = line.points.at(index);
            path.lineTo(point[0] * scaleX, -point[1] * scaleY);
        }
    }
    QRectF geometry = minorPath_.boundingRect().united(
        majorPath_.boundingRect());
    if (!geometry.isValid() || geometry.isEmpty())
        geometry = QRectF(0.0, 0.0, 1.0, 1.0);
    bounds_ = geometry.adjusted(-4.0, -4.0, 4.0, 4.0);
}
