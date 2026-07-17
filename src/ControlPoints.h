#pragma once

#include "CloudCoordinates.h"

#include <QString>
#include <QVector>

namespace famp::control
{
struct Point
{
    QString id;
    QString name;
    famp::cloud::Point3d local{0.0, 0.0, 0.0};
    famp::cloud::Point3d target{0.0, 0.0, 0.0};
    bool enabled = true;
};

struct Residual
{
    QString pointId;
    QString pointName;
    famp::cloud::Point3d delta{0.0, 0.0, 0.0};
    double distance = 0.0;
};

struct Quality
{
    int enabledPointCount = 0;
    double rootMeanSquare = 0.0;
    double mean = 0.0;
    double maximum = 0.0;
    QVector<Residual> residuals;
};

struct Solution
{
    famp::cloud::SpatialReference spatial;
    Quality quality;
};

QString createPointId();
bool isValidPointId(const QString& id);
bool pointsEqual(const QVector<Point>& first,
                 const QVector<Point>& second) noexcept;
bool validatePoints(const QVector<Point>& points,
                    QString* errorMessage = nullptr);

bool evaluate(const QVector<Point>& points,
              const famp::cloud::SpatialReference& spatial,
              Quality& quality,
              QString* errorMessage = nullptr);

bool transformDisplayPoint(
    const famp::cloud::SpatialReference& before,
    const famp::cloud::SpatialReference& after,
    const famp::cloud::Point3d& input,
    famp::cloud::Point3d& output,
    QString* errorMessage = nullptr);

bool solveRigid(const QVector<Point>& points,
                Solution& solution,
                QString* errorMessage = nullptr);
}
