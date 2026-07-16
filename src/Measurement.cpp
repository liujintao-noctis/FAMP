#include "Measurement.h"

#include <cmath>
#include <algorithm>
#include <limits>
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

double polygonPerimeter(const QVector<QPointF>& meterPoints) noexcept
{
    if (meterPoints.size() < 2)
        return 0.0;

    const QPointF& first = meterPoints.front();
    const QPointF& last = meterPoints.back();
    return polylineLength(meterPoints)
        + std::hypot(first.x() - last.x(), first.y() - last.y());
}

double angleDegrees(const QVector<QPointF>& meterPoints) noexcept
{
    if (meterPoints.size() != 3)
        return 0.0;

    const QPointF first = meterPoints.at(0) - meterPoints.at(1);
    const QPointF second = meterPoints.at(2) - meterPoints.at(1);
    const double firstLength = std::hypot(first.x(), first.y());
    const double secondLength = std::hypot(second.x(), second.y());
    if (firstLength <= 1.0e-12 || secondLength <= 1.0e-12)
        return 0.0;

    const double cosine = std::clamp(
        (first.x() * second.x() + first.y() * second.y())
            / (firstLength * secondLength),
        -1.0,
        1.0);
    return std::acos(cosine) * 180.0 / std::acos(-1.0);
}

int minimumPointCount(Kind kind) noexcept
{
    return kind == Kind::Area || kind == Kind::Angle ? 3 : 2;
}

double value(Kind kind, const QVector<QPointF>& meterPoints) noexcept
{
    switch (kind)
    {
    case Kind::Distance:
        return polylineLength(meterPoints);
    case Kind::Area:
        return polygonArea(meterPoints);
    case Kind::Angle:
        return angleDegrees(meterPoints);
    }
    return 0.0;
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

    if (kind == Kind::Area && value >= 10000.0)
        return QStringLiteral("%1 ha").arg(value / 10000.0, 0, 'f', 3);
    if (kind == Kind::Area)
        return QStringLiteral("%1 m²").arg(value, 0, 'f', 3);
    return QStringLiteral("%1°").arg(value, 0, 'f', 2);
}

QString formatSummary(Kind kind, const QVector<QPointF>& meterPoints)
{
    const QString primary = formatValue(kind, value(kind, meterPoints));
    if (kind != Kind::Area || primary == QStringLiteral("无效测量"))
        return primary;

    return QStringLiteral("面积 %1 · 周长 %2")
        .arg(primary,
             formatValue(Kind::Distance, polygonPerimeter(meterPoints)));
}

bool finitePoints(const QVector<QVector3D>& meterPoints) noexcept
{
    return std::all_of(meterPoints.cbegin(), meterPoints.cend(),
                       [](const QVector3D& point) {
                           return std::isfinite(point.x())
                               && std::isfinite(point.y())
                               && std::isfinite(point.z());
                       });
}

double polylineLength(const QVector<QVector3D>& meterPoints) noexcept
{
    if (!finitePoints(meterPoints))
        return std::numeric_limits<double>::quiet_NaN();

    double length = 0.0;
    for (int index = 1; index < meterPoints.size(); ++index)
    {
        const QVector3D& current = meterPoints.at(index);
        const QVector3D& previous = meterPoints.at(index - 1);
        length += std::hypot(
            std::hypot(static_cast<double>(current.x()) - previous.x(),
                       static_cast<double>(current.y()) - previous.y()),
            static_cast<double>(current.z()) - previous.z());
    }
    return length;
}

double polygonArea(const QVector<QVector3D>& meterPoints) noexcept
{
    if (meterPoints.size() < 3)
        return 0.0;
    if (!finitePoints(meterPoints))
        return std::numeric_limits<double>::quiet_NaN();

    // Newell's method gives the area of a planar polygon in arbitrary 3D
    // orientation without projecting it onto a particular coordinate plane.
    double normalX = 0.0;
    double normalY = 0.0;
    double normalZ = 0.0;
    for (int index = 0; index < meterPoints.size(); ++index)
    {
        const QVector3D& current = meterPoints.at(index);
        const QVector3D& next = meterPoints.at(
            (index + 1) % meterPoints.size());
        normalX += (static_cast<double>(current.y()) - next.y())
            * (static_cast<double>(current.z()) + next.z());
        normalY += (static_cast<double>(current.z()) - next.z())
            * (static_cast<double>(current.x()) + next.x());
        normalZ += (static_cast<double>(current.x()) - next.x())
            * (static_cast<double>(current.y()) + next.y());
    }
    return 0.5 * std::hypot(std::hypot(normalX, normalY), normalZ);
}

double polygonPerimeter(const QVector<QVector3D>& meterPoints) noexcept
{
    if (meterPoints.size() < 2)
        return 0.0;
    const double openLength = polylineLength(meterPoints);
    if (!std::isfinite(openLength))
        return openLength;

    const QVector3D& first = meterPoints.front();
    const QVector3D& last = meterPoints.back();
    return openLength
        + std::hypot(
            std::hypot(static_cast<double>(first.x()) - last.x(),
                       static_cast<double>(first.y()) - last.y()),
            static_cast<double>(first.z()) - last.z());
}

double angleDegrees(const QVector<QVector3D>& meterPoints) noexcept
{
    if (meterPoints.size() != 3)
        return 0.0;
    if (!finitePoints(meterPoints))
        return std::numeric_limits<double>::quiet_NaN();

    const QVector3D first = meterPoints.at(0) - meterPoints.at(1);
    const QVector3D second = meterPoints.at(2) - meterPoints.at(1);
    const double firstLength = std::hypot(
        std::hypot(static_cast<double>(first.x()), first.y()), first.z());
    const double secondLength = std::hypot(
        std::hypot(static_cast<double>(second.x()), second.y()), second.z());
    if (firstLength <= 1.0e-12 || secondLength <= 1.0e-12)
        return 0.0;

    const double dot = static_cast<double>(first.x()) * second.x()
        + static_cast<double>(first.y()) * second.y()
        + static_cast<double>(first.z()) * second.z();
    const double cosine = std::clamp(
        dot / (firstLength * secondLength), -1.0, 1.0);
    return std::acos(cosine) * 180.0 / std::acos(-1.0);
}

double value(Kind kind, const QVector<QVector3D>& meterPoints) noexcept
{
    switch (kind)
    {
    case Kind::Distance:
        return polylineLength(meterPoints);
    case Kind::Area:
        return polygonArea(meterPoints);
    case Kind::Angle:
        return angleDegrees(meterPoints);
    }
    return 0.0;
}

QString formatSummary(Kind kind, const QVector<QVector3D>& meterPoints)
{
    const QString primary = formatValue(kind, value(kind, meterPoints));
    if (kind != Kind::Area || primary == QStringLiteral("无效测量"))
        return primary;

    return QStringLiteral("面积 %1 · 周长 %2")
        .arg(primary,
             formatValue(Kind::Distance, polygonPerimeter(meterPoints)));
}
}
