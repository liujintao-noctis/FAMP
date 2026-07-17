#pragma once

#include "ControlPoints.h"

#include <QString>

class QWidget;

namespace famp::control
{
struct EditResult
{
    QVector<Point> points;
    bool applySolution = false;
    Solution solution;
};

bool editControlPoints(
    QWidget* parent,
    const QString& layerName,
    const QString& sourcePath,
    const famp::cloud::SpatialReference& currentSpatial,
    const QVector<Point>& currentPoints,
    EditResult& result);
}
