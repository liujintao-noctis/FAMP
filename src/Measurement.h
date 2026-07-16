#pragma once

#include <QPointF>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QVector3D>

namespace famp::measurement
{
enum class Kind
{
    Distance,
    Area,
    Angle
};

struct Record3D
{
    QString id;
    QString layerId;
    QString crs;
    Kind kind = Kind::Distance;
    QVector<QVector3D> points;
};

QString createRecordId();
bool isValidRecordId(const QString& id);
QString kindName(Kind kind);
bool kindFromName(const QString& name, Kind& kind);
bool validateRecord3D(const Record3D& record,
                      QString* errorMessage = nullptr);

bool sceneToMeters(const QVector<QPointF>& scenePoints,
                   const QPointF& sceneUnitsPerMeter,
                   QVector<QPointF>& meterPoints,
                   QString* errorMessage = nullptr);

double polylineLength(const QVector<QPointF>& meterPoints) noexcept;
double polygonArea(const QVector<QPointF>& meterPoints) noexcept;
double polygonPerimeter(const QVector<QPointF>& meterPoints) noexcept;
double angleDegrees(const QVector<QPointF>& meterPoints) noexcept;
int minimumPointCount(Kind kind) noexcept;
double value(Kind kind, const QVector<QPointF>& meterPoints) noexcept;
QString formatValue(Kind kind, double value);
QString formatSummary(Kind kind, const QVector<QPointF>& meterPoints);

bool finitePoints(const QVector<QVector3D>& meterPoints) noexcept;
double polylineLength(const QVector<QVector3D>& meterPoints) noexcept;
double polygonArea(const QVector<QVector3D>& meterPoints) noexcept;
double polygonPerimeter(const QVector<QVector3D>& meterPoints) noexcept;
double angleDegrees(const QVector<QVector3D>& meterPoints) noexcept;
double value(Kind kind, const QVector<QVector3D>& meterPoints) noexcept;
QString formatSummary(Kind kind, const QVector<QVector3D>& meterPoints);
}

Q_DECLARE_METATYPE(famp::measurement::Record3D)
