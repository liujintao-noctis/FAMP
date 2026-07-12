#pragma once

#include "CloudCoordinates.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace famp::project
{
inline constexpr int SchemaVersion = 2;

struct CloudReference
{
    QString path;
    qint64 size = -1;
    qint64 modifiedUtcMilliseconds = -1;
    QByteArray sha256;
    bool visible = true;
    famp::cloud::SpatialReference spatial;
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
