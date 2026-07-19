#include "ProcessingRecipe.h"

#include "FileIO.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>
#include <limits>

namespace famp::recipe
{
namespace
{
constexpr qint64 MaxRecipeBytes = 1024 * 1024;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

SourceInfo sourceInfo(const QString& path)
{
    SourceInfo result;
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists() || !info.isFile())
        return result;
    result.path = info.absoluteFilePath();
    result.size = info.size();
    result.modifiedUtc = info.lastModified().toUTC().toString(Qt::ISODateWithMs);
    return result;
}

QString operationName(Operation operation)
{
    switch (operation)
    {
    case Operation::VoxelDownsample:
        return QStringLiteral("voxelDownsample");
    case Operation::StatisticalOutlierRemoval:
        return QStringLiteral("statisticalOutlierRemoval");
    case Operation::RangeCrop:
        return QStringLiteral("rangeCrop");
    }
    return QString();
}

bool readFinite(const QJsonObject& object, const QString& key, double& value)
{
    const QJsonValue candidate = object.value(key);
    if (!candidate.isDouble() || !std::isfinite(candidate.toDouble()))
        return false;
    value = candidate.toDouble();
    return true;
}

bool validateProcessingRecipe(const Recipe& recipe, QString* errorMessage)
{
    if (recipe.operation == Operation::VoxelDownsample)
    {
        if (!std::isfinite(recipe.processing.voxelLeafSizeMeters)
            || recipe.processing.voxelLeafSizeMeters < 1.0e-6
            || recipe.processing.voxelLeafSizeMeters > 1000.0)
        {
            setError(errorMessage, QStringLiteral("处理方案的体素边长无效。"));
            return false;
        }
        return true;
    }
    if (recipe.operation == Operation::StatisticalOutlierRemoval)
    {
        if (recipe.processing.meanNeighbors < 2
            || !std::isfinite(recipe.processing.standardDeviationMultiplier)
            || recipe.processing.standardDeviationMultiplier <= 0.0
            || recipe.processing.standardDeviationMultiplier > 100.0)
        {
            setError(errorMessage, QStringLiteral("处理方案的统计去噪参数无效。"));
            return false;
        }
        return true;
    }
    return famp::crop::validateOptions(recipe.crop, errorMessage);
}

QJsonObject parameters(const Recipe& recipe)
{
    QJsonObject result;
    if (recipe.operation == Operation::VoxelDownsample)
    {
        result.insert(QStringLiteral("leafSizeMeters"),
                      recipe.processing.voxelLeafSizeMeters);
    }
    else if (recipe.operation == Operation::StatisticalOutlierRemoval)
    {
        result.insert(QStringLiteral("meanNeighbors"),
                      recipe.processing.meanNeighbors);
        result.insert(QStringLiteral("standardDeviationMultiplier"),
                      recipe.processing.standardDeviationMultiplier);
    }
    else
    {
        result.insert(QStringLiteral("minimumX"), recipe.crop.minimumX);
        result.insert(QStringLiteral("maximumX"), recipe.crop.maximumX);
        result.insert(QStringLiteral("minimumY"), recipe.crop.minimumY);
        result.insert(QStringLiteral("maximumY"), recipe.crop.maximumY);
        result.insert(QStringLiteral("minimumZ"), recipe.crop.minimumZ);
        result.insert(QStringLiteral("maximumZ"), recipe.crop.maximumZ);
        result.insert(QStringLiteral("keepInside"), recipe.crop.keepInside);
    }
    return result;
}
}

Recipe forProcessing(const famp::processing::Options& options,
                     const QString& sourcePath)
{
    Recipe result;
    result.processing = options;
    result.operation = options.method == famp::processing::Method::VoxelDownsample
        ? Operation::VoxelDownsample
        : Operation::StatisticalOutlierRemoval;
    result.source = sourceInfo(sourcePath);
    return result;
}

Recipe forCrop(const famp::crop::Options& options, const QString& sourcePath)
{
    Recipe result;
    result.operation = Operation::RangeCrop;
    result.crop = options;
    result.source = sourceInfo(sourcePath);
    return result;
}

QString automaticSidecarPath(const QString& outputPath)
{
    return outputPath + QStringLiteral(".famp-process.json");
}

bool save(const QString& requestedPath,
          const Recipe& recipe,
          QString* savedPath,
          QString* errorMessage)
{
    if (!validateProcessingRecipe(recipe, errorMessage))
        return false;

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), SchemaVersion);
    root.insert(QStringLiteral("operation"), operationName(recipe.operation));
    root.insert(QStringLiteral("parameters"), parameters(recipe));
    if (!recipe.source.path.isEmpty())
    {
        QJsonObject source;
        source.insert(QStringLiteral("path"), recipe.source.path);
        source.insert(QStringLiteral("size"), static_cast<double>(recipe.source.size));
        source.insert(QStringLiteral("modifiedUtc"), recipe.source.modifiedUtc);
        root.insert(QStringLiteral("source"), source);
    }

    const QString outputPath = famp::io::pathWithRequiredSuffix(
        requestedPath, QStringLiteral("json"));
    QSaveFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly))
    {
        setError(errorMessage,
                 QStringLiteral("无法写入处理方案：%1").arg(file.errorString()));
        return false;
    }
    const QByteArray content = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(content) != content.size() || !file.commit())
    {
        file.cancelWriting();
        setError(errorMessage,
                 QStringLiteral("处理方案写入失败：%1").arg(file.errorString()));
        return false;
    }
    if (savedPath)
        *savedPath = outputPath;
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool load(const QString& path, Recipe& recipe, QString* errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(errorMessage,
                 QStringLiteral("无法读取处理方案：%1").arg(file.errorString()));
        return false;
    }
    if (file.size() < 2 || file.size() > MaxRecipeBytes)
    {
        setError(errorMessage, QStringLiteral("处理方案文件大小无效。"));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        setError(errorMessage, QStringLiteral("处理方案不是有效 JSON。"));
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != SchemaVersion
        || !root.value(QStringLiteral("operation")).isString()
        || !root.value(QStringLiteral("parameters")).isObject())
    {
        setError(errorMessage, QStringLiteral("处理方案版本或结构不受支持。"));
        return false;
    }

    Recipe candidate;
    const QString operation = root.value(QStringLiteral("operation")).toString();
    if (operation == QStringLiteral("voxelDownsample"))
    {
        candidate.operation = Operation::VoxelDownsample;
        candidate.processing.method = famp::processing::Method::VoxelDownsample;
    }
    else if (operation == QStringLiteral("statisticalOutlierRemoval"))
    {
        candidate.operation = Operation::StatisticalOutlierRemoval;
        candidate.processing.method =
            famp::processing::Method::StatisticalOutlierRemoval;
    }
    else if (operation == QStringLiteral("rangeCrop"))
    {
        candidate.operation = Operation::RangeCrop;
    }
    else
    {
        setError(errorMessage, QStringLiteral("处理方案操作类型无效。"));
        return false;
    }

    const QJsonObject values = root.value(QStringLiteral("parameters")).toObject();
    if (candidate.operation == Operation::VoxelDownsample)
    {
        if (!readFinite(values, QStringLiteral("leafSizeMeters"),
                        candidate.processing.voxelLeafSizeMeters))
        {
            setError(errorMessage, QStringLiteral("处理方案参数无效。"));
            return false;
        }
    }
    else if (candidate.operation == Operation::StatisticalOutlierRemoval)
    {
        const QJsonValue neighbors = values.value(QStringLiteral("meanNeighbors"));
        double multiplier = 0.0;
        if (!neighbors.isDouble()
            || std::floor(neighbors.toDouble()) != neighbors.toDouble()
            || neighbors.toDouble() > std::numeric_limits<int>::max()
            || !readFinite(values, QStringLiteral("standardDeviationMultiplier"),
                           multiplier))
        {
            setError(errorMessage, QStringLiteral("处理方案参数无效。"));
            return false;
        }
        candidate.processing.meanNeighbors = neighbors.toInt();
        candidate.processing.standardDeviationMultiplier = multiplier;
    }
    else
    {
        if (!readFinite(values, QStringLiteral("minimumX"), candidate.crop.minimumX)
            || !readFinite(values, QStringLiteral("maximumX"), candidate.crop.maximumX)
            || !readFinite(values, QStringLiteral("minimumY"), candidate.crop.minimumY)
            || !readFinite(values, QStringLiteral("maximumY"), candidate.crop.maximumY)
            || !readFinite(values, QStringLiteral("minimumZ"), candidate.crop.minimumZ)
            || !readFinite(values, QStringLiteral("maximumZ"), candidate.crop.maximumZ)
            || !values.value(QStringLiteral("keepInside")).isBool())
        {
            setError(errorMessage, QStringLiteral("处理方案参数无效。"));
            return false;
        }
        candidate.crop.keepInside =
            values.value(QStringLiteral("keepInside")).toBool();
    }

    const QJsonValue sourceValue = root.value(QStringLiteral("source"));
    if (!sourceValue.isUndefined())
    {
        if (!sourceValue.isObject())
        {
            setError(errorMessage, QStringLiteral("处理方案来源信息无效。"));
            return false;
        }
        const QJsonObject source = sourceValue.toObject();
        if (!source.value(QStringLiteral("path")).isString()
            || !source.value(QStringLiteral("size")).isDouble()
            || !source.value(QStringLiteral("modifiedUtc")).isString())
        {
            setError(errorMessage, QStringLiteral("处理方案来源信息无效。"));
            return false;
        }
        const double sourceSize = source.value(QStringLiteral("size")).toDouble();
        if (!std::isfinite(sourceSize) || sourceSize < 0.0
            || std::floor(sourceSize) != sourceSize
            || sourceSize >= static_cast<double>(std::numeric_limits<qint64>::max()))
        {
            setError(errorMessage, QStringLiteral("处理方案来源文件大小无效。"));
            return false;
        }
        candidate.source.path = source.value(QStringLiteral("path")).toString();
        candidate.source.size = static_cast<qint64>(sourceSize);
        candidate.source.modifiedUtc =
            source.value(QStringLiteral("modifiedUtc")).toString();
    }
    if (!validateProcessingRecipe(candidate, errorMessage))
        return false;

    recipe = candidate;
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool sourceMatches(const Recipe& recipe,
                   const QString& currentSourcePath,
                   QString* warningMessage)
{
    if (recipe.source.path.isEmpty())
    {
        if (warningMessage)
            warningMessage->clear();
        return true;
    }

    const SourceInfo current = sourceInfo(currentSourcePath);
    if (current.path == recipe.source.path
        && current.size == recipe.source.size
        && current.modifiedUtc == recipe.source.modifiedUtc)
    {
        if (warningMessage)
            warningMessage->clear();
        return true;
    }

    setError(warningMessage,
             QStringLiteral("该方案记录的源文件与当前点云的路径、大小或修改时间不一致；参数仍可载入，但结果不一定完全相同。"));
    return false;
}
}
