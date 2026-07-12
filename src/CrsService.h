#pragma once

#include <QString>

namespace famp::crs
{
struct Info
{
    QString identifier;
    QString name;
    QString type;
    bool geographic = false;
};

struct Coordinate
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

QString normalizedEpsg(const QString& identifier);
QString runtimeVersion();
bool inspect(const QString& identifier,
             Info& info,
             QString* errorMessage = nullptr);
bool transform(const QString& sourceIdentifier,
               const QString& targetIdentifier,
               const Coordinate& source,
               Coordinate& target,
               QString* errorMessage = nullptr);
}
