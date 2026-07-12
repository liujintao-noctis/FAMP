#include "Measurement.h"

#include <cmath>
#include <utility>

namespace famp::measurement
{
namespace
{
void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool finitePoint(const QPointF& point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}
}

bool sceneToMeters(const QVector<QPointF>& scenePoints,
                   const QPointF& sceneUnitsPerMeter,
                   QVector<QPointF>& meterPoints,
                   QString* errorMessage)
{
    if (!finitePoint(sceneUnitsPerMeter)
        || sceneUnitsPerMeter.x() <= 0.0
        || sceneUnitsPerMeter.y() <= 0.0)
    {
        setError(errorMessage, QStringLiteral("当前制图比例尺无效，无法测量。"));
        return false;
    }
    if (scenePoints.isEmpty())
    {
        setError(errorMessage, QStringLiteral("测量点为空。"));
        return false;
    }

    QVector<QPointF> converted;
    converted.reserve(scenePoints.size());
    for (const QPointF& point : scenePoints)
    {
        if (!finitePoint(point))
        {
            setError(errorMessage, QStringLiteral("测量点包含无效坐标。"));
            return false;
        }
        converted.append(QPointF(point.x() / sceneUnitsPerMeter.x(),
                                 point.y() / sceneUnitsPerMeter.y()));
    }

    meterPoints = std::move(converted);
    if (errorMessage)
        errorMessage->clear();
    return true;
}

double polylineLength(const QVector<QPointF>& meterPoints) noexcept
{
    double length = 0.0;
    for (int index = 1; index < meterPoints.size(); ++index)
    {
        const double dx = meterPoints.at(index).x()
            - meterPoints.at(index - 1).x();
        const double dy = meterPoints.at(index).y()
            - meterPoints.at(index - 1).y();
        length += std::hypot(dx, dy);
    }
    return length;
}

double polygonArea(const QVector<QPointF>& meterPoints) noexcept
{
    if (meterPoints.size() < 3)
        return 0.0;

    double twiceSignedArea = 0.0;
    for (int index = 0; index < meterPoints.size(); ++index)
    {
        const QPointF& current = meterPoints.at(index);
        const QPointF& next = meterPoints.at((index + 1) % meterPoints.size());
        twiceSignedArea += current.x() * next.y() - next.x() * current.y();
    }
    return std::abs(twiceSignedArea) * 0.5;
}

QString formatValue(Kind kind, double value)
{
    if (!std::isfinite(value) || value < 0.0)
        return QStringLiteral("无效测量");

    if (kind == Kind::Distance)
    {
        if (value >= 1000.0)
            return QStringLiteral("%1 km").arg(value / 1000.0, 0, 'f', 3);
        return QStringLiteral("%1 m").arg(value, 0, 'f', 3);
    }

    if (value >= 10000.0)
        return QStringLiteral("%1 ha").arg(value / 10000.0, 0, 'f', 3);
    return QStringLiteral("%1 m²").arg(value, 0, 'f', 3);
}
}
