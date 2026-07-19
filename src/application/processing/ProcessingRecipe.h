#pragma once

#include "CloudCrop.h"
#include "CloudProcessing.h"

#include <QString>

namespace famp::recipe
{
inline constexpr int SchemaVersion = 1;

enum class Operation
{
    VoxelDownsample,
    StatisticalOutlierRemoval,
    RangeCrop
};

struct SourceInfo
{
    QString path;
    qint64 size = -1;
    QString modifiedUtc;
};

struct Recipe
{
    Operation operation = Operation::VoxelDownsample;
    famp::processing::Options processing;
    famp::crop::Options crop;
    SourceInfo source;
};

Recipe forProcessing(const famp::processing::Options& options,
                     const QString& sourcePath = QString());
Recipe forCrop(const famp::crop::Options& options,
               const QString& sourcePath = QString());

QString automaticSidecarPath(const QString& outputPath);
bool save(const QString& requestedPath,
          const Recipe& recipe,
          QString* savedPath = nullptr,
          QString* errorMessage = nullptr);
bool load(const QString& path,
          Recipe& recipe,
          QString* errorMessage = nullptr);
bool sourceMatches(const Recipe& recipe,
                   const QString& currentSourcePath,
                   QString* warningMessage = nullptr);
}
