#pragma once

#include "CloudAttributes.h"
#include "CloudCoordinates.h"
#include "CloudDisplaySettings.h"

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace famp::project
{
inline constexpr int SchemaVersion = 3;

struct CloudReference
{
    QString path;
    QString layerId;
    QString name;
    QString crs;
    qint64 size = -1;
    qint64 modifiedUtcMilliseconds = -1;
    QByteArray sha256;
    bool visible = true;
    bool locked = false;
    famp::cloud::SpatialReference spatial;
    famp::display::Settings display;
    QVector<famp::cloud::AttributeSummary> attributes;
    QMap<QString, QString> archaeologyFields;
};

struct Document
{
    QStringList cloudFiles;
    QVector<CloudReference> clouds;
    QString mapScale = QStringLiteral("1:50");
    QString projectCrs;
    QJsonObject graphicsState;
    QByteArray windowGeometry;
    QByteArray windowState;
    bool xoyLabelVisible = true;
    bool scaleVisible = true;
};

QString pathWithProjectSuffix(const QString& path);
bool save(const QString& projectPath,
          const Document& document,
          const QString& applicationVersion,
          QString* errorMessage = nullptr);
bool load(const QString& projectPath,
          Document& document,
          QString* errorMessage = nullptr);
}
