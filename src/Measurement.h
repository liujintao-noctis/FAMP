#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

namespace famp::measurement
{
enum class Kind
{
    Distance,
    Area
};

bool sceneToMeters(const QVector<QPointF>& scenePoints,
                   const QPointF& sceneUnitsPerMeter,
                   QVector<QPointF>& meterPoints,
                   QString* errorMessage = nullptr);

double polylineLength(const QVector<QPointF>& meterPoints) noexcept;
double polygonArea(const QVector<QPointF>& meterPoints) noexcept;
QString formatValue(Kind kind, double value);
}
