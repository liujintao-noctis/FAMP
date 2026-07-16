#pragma once

#include "CloudCoordinates.h"
#include "Measurement.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <cstddef>

namespace famp::report
{
struct CloudEntry
{
    QString path;
    std::size_t pointCount = 0;
    bool visible = true;
    famp::cloud::SpatialReference spatial;
};

struct Data
{
    QString projectName;
    QString projectPath;
    QString projectCrs;
    QString mapScale;
    QString applicationVersion;
    QDateTime generatedAt;
    QVector<CloudEntry> clouds;
    QJsonObject graphicsState;
    QVector<famp::measurement::Record3D> measurements3d;
};

QString toHtml(const Data& data, QString* errorMessage = nullptr);
bool saveHtml(const QString& path,
              const Data& data,
              QString* errorMessage = nullptr);
bool savePdf(const QString& path,
             const Data& data,
             QString* errorMessage = nullptr);
}
