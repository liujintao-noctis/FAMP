#pragma once

#include <QString>
#include <QStringList>

namespace famp::project
{
inline constexpr int SchemaVersion = 1;

struct Document
{
    QStringList cloudFiles;
    QString mapScale = QStringLiteral("1:50");
    QString projectCrs;
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
