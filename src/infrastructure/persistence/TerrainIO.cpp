#include "TerrainIO.h"

#include "FileIO.h"

#include <QDataStream>
#include <QFile>
#include <QLocale>
#include <QSaveFile>
#include <QTextStream>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr char SidecarMagic[]{'F', 'D', 'E', 'M'};
constexpr quint64 MaximumGridCells = 100'000'000;
constexpr quint64 MaximumContourPoints = 20'000'000;
constexpr quint32 MaximumTextBytes = 1'048'576;
constexpr double NoDataValue = -9999.0;
constexpr qsizetype CancellationInterval = 4096;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

void clearError(QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
}

bool cancelled(const famp::tasks::CancellationCheck& shouldCancel)
{
    return famp::tasks::isCancellationRequested(shouldCancel);
}

bool cancelSave(QSaveFile& file,
                const QString& message,
                QString* errorMessage)
{
    file.cancelWriting();
    setError(errorMessage, message);
    return false;
}

void configureStream(QDataStream& stream)
{
    stream.setVersion(QDataStream::Qt_5_15);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
}

bool validPath(const QString& path, QString* errorMessage)
{
    if (!path.trimmed().isEmpty())
        return true;
    setError(errorMessage, QStringLiteral("地形文件路径不能为空。"));
    return false;
}

bool validGrid(const famp::terrain::Grid& grid, QString* errorMessage)
{
    if (!grid.isValid()
        || grid.sourcePointCount < grid.populatedCellCount
        || static_cast<quint64>(grid.elevations.size()) > MaximumGridCells)
    {
        setError(errorMessage, QStringLiteral("DEM 网格数据无效。"));
        return false;
    }
    const QString metadata[]{
        grid.sourceLayerId, grid.sourceLayerName, grid.sourceCrs,
        grid.horizontalUnitName};
    for (const QString& text : metadata)
    {
        if (static_cast<quint64>(text.toUtf8().size()) > MaximumTextBytes)
        {
            setError(errorMessage, QStringLiteral("DEM 元数据超过安全上限。"));
            return false;
        }
    }
    return true;
}

bool openSaveFile(QSaveFile& file, QString* errorMessage)
{
    if (file.open(QIODevice::WriteOnly))
        return true;
    setError(errorMessage,
             QStringLiteral("无法创建文件 %1：%2")
                 .arg(file.fileName(), file.errorString()));
    return false;
}

bool commitSaveFile(QSaveFile& file,
                    bool streamOk,
                    QString* errorMessage)
{
    if (!streamOk || file.error() != QFileDevice::NoError)
    {
        const QString detail = file.errorString();
        file.cancelWriting();
        setError(errorMessage,
                 QStringLiteral("写入文件 %1 失败：%2")
                     .arg(file.fileName(), detail));
        return false;
    }
    if (!file.commit())
    {
        setError(errorMessage,
                 QStringLiteral("无法完成文件 %1：%2")
                     .arg(file.fileName(), file.errorString()));
        return false;
    }
    clearError(errorMessage);
    return true;
}

bool writeUtf8(QDataStream& stream, const QString& value)
{
    const QByteArray encoded = value.toUtf8();
    if (encoded.size() > static_cast<qsizetype>(MaximumTextBytes))
        return false;
    stream << static_cast<quint32>(encoded.size());
    return encoded.isEmpty()
        || stream.writeRawData(encoded.constData(), encoded.size())
            == encoded.size();
}

bool readUtf8(QDataStream& stream, QString& value)
{
    quint32 size = 0;
    stream >> size;
    if (stream.status() != QDataStream::Ok || size > MaximumTextBytes
        || static_cast<qint64>(size) > stream.device()->bytesAvailable())
    {
        return false;
    }
    QByteArray encoded(static_cast<int>(size), '\0');
    if (size > 0
        && stream.readRawData(encoded.data(), static_cast<int>(size))
            != static_cast<int>(size))
    {
        return false;
    }
    value = QString::fromUtf8(encoded);
    return value.toUtf8() == encoded;
}

void configureTextStream(QTextStream& stream)
{
    stream.setLocale(QLocale::c());
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    stream.setCodec("UTF-8");
#endif
    stream.setRealNumberNotation(QTextStream::SmartNotation);
    stream.setRealNumberPrecision(std::numeric_limits<double>::max_digits10);
}

bool validContours(
    const QVector<famp::terrain::ContourLine>& contours,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel = {})
{
    quint64 pointCount = 0;
    for (const auto& line : contours)
    {
        if (cancelled(shouldCancel))
        {
            setError(errorMessage, QStringLiteral("等高线导出已取消。"));
            return false;
        }
        if (!std::isfinite(line.elevation) || line.points.size() < 2)
        {
            setError(errorMessage, QStringLiteral("等高线数据无效。"));
            return false;
        }
        pointCount += static_cast<quint64>(line.points.size());
        if (pointCount > MaximumContourPoints)
        {
            setError(errorMessage, QStringLiteral("等高线点数超过安全上限。"));
            return false;
        }
        for (qsizetype index = 0; index < line.points.size(); ++index)
        {
            if ((index % CancellationInterval) == 0
                && cancelled(shouldCancel))
            {
                setError(errorMessage, QStringLiteral("等高线导出已取消。"));
                return false;
            }
            const auto& point = line.points.at(index);
            if (!std::isfinite(point[0]) || !std::isfinite(point[1]))
            {
                setError(errorMessage, QStringLiteral("等高线包含无效坐标。"));
                return false;
            }
        }
    }
    if (contours.isEmpty())
    {
        setError(errorMessage, QStringLiteral("没有可导出的等高线。"));
        return false;
    }
    return true;
}

QString number(double value)
{
    return QString::number(value, 'g',
                           std::numeric_limits<double>::max_digits10);
}
}

namespace famp::terrainio
{
QString pathWithDemSuffix(const QString& path)
{
    return famp::io::pathWithRequiredSuffix(
        path, QStringLiteral("famp-dem"));
}

bool saveGridAtomically(const QString& path,
                        const famp::terrain::Grid& grid,
                        QString* errorMessage,
                        const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!validPath(path, errorMessage))
        return false;
    if (cancelled(shouldCancel))
    {
        setError(errorMessage, QStringLiteral("DEM 保存已取消。"));
        return false;
    }
    if (!validGrid(grid, errorMessage))
        return false;

    QSaveFile file(path);
    if (!openSaveFile(file, errorMessage))
        return false;
    QDataStream stream(&file);
    configureStream(stream);
    if (stream.writeRawData(SidecarMagic, 4) != 4)
    {
        file.cancelWriting();
        setError(errorMessage, QStringLiteral("无法写入 DEM 文件头。"));
        return false;
    }
    stream << SidecarSchemaVersion << static_cast<quint16>(0)
           << static_cast<qint32>(grid.columns)
           << static_cast<qint32>(grid.rows)
           << grid.originX << grid.originY << grid.resolution
           << grid.horizontalUnitToMetre
           << static_cast<qint32>(grid.statistic)
           << static_cast<qint32>(grid.sourcePointCount)
           << static_cast<qint32>(grid.populatedCellCount)
           << static_cast<qint32>(grid.filledCellCount);
    const bool metadataWritten = writeUtf8(stream, grid.sourceLayerId)
        && writeUtf8(stream, grid.sourceLayerName)
        && writeUtf8(stream, grid.sourceCrs)
        && writeUtf8(stream, grid.horizontalUnitName);
    stream << static_cast<quint64>(grid.elevations.size());
    for (qsizetype index = 0; index < grid.elevations.size(); ++index)
    {
        if ((index % CancellationInterval) == 0 && cancelled(shouldCancel))
        {
            return cancelSave(
                file, QStringLiteral("DEM 保存已取消。"), errorMessage);
        }
        stream << grid.elevations.at(index);
    }
    if (cancelled(shouldCancel))
    {
        return cancelSave(
            file, QStringLiteral("DEM 保存已取消。"), errorMessage);
    }
    return commitSaveFile(
        file, metadataWritten && stream.status() == QDataStream::Ok,
        errorMessage);
}

bool loadGrid(const QString& path,
              famp::terrain::Grid& grid,
              QString* errorMessage)
{
    if (!validPath(path, errorMessage))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(errorMessage,
                 QStringLiteral("无法读取 DEM 文件 %1：%2")
                     .arg(path, file.errorString()));
        return false;
    }
    QDataStream stream(&file);
    configureStream(stream);
    char magic[4]{};
    if (stream.readRawData(magic, 4) != 4
        || !std::equal(std::begin(magic), std::end(magic),
                       std::begin(SidecarMagic)))
    {
        setError(errorMessage, QStringLiteral("DEM 文件标识无效。"));
        return false;
    }

    quint16 version = 0;
    quint16 reserved = 0;
    qint32 columns = 0;
    qint32 rows = 0;
    qint32 statistic = -1;
    qint32 sourcePoints = -1;
    qint32 populatedCells = -1;
    qint32 filledCells = -1;
    famp::terrain::Grid candidate;
    stream >> version >> reserved >> columns >> rows
           >> candidate.originX >> candidate.originY >> candidate.resolution
           >> candidate.horizontalUnitToMetre >> statistic >> sourcePoints
           >> populatedCells >> filledCells;
    Q_UNUSED(reserved);
    if (stream.status() != QDataStream::Ok
        || version != SidecarSchemaVersion
        || columns <= 0 || rows <= 0
        || statistic < static_cast<qint32>(
            famp::terrain::CellStatistic::Minimum)
        || statistic > static_cast<qint32>(
            famp::terrain::CellStatistic::Median)
        || sourcePoints < 0 || populatedCells < 0 || filledCells < 0)
    {
        setError(errorMessage, QStringLiteral("DEM 文件版本或元数据无效。"));
        return false;
    }
    candidate.columns = columns;
    candidate.rows = rows;
    candidate.statistic = static_cast<famp::terrain::CellStatistic>(statistic);
    candidate.sourcePointCount = sourcePoints;
    candidate.populatedCellCount = populatedCells;
    candidate.filledCellCount = filledCells;
    if (!readUtf8(stream, candidate.sourceLayerId)
        || !readUtf8(stream, candidate.sourceLayerName)
        || !readUtf8(stream, candidate.sourceCrs)
        || !readUtf8(stream, candidate.horizontalUnitName))
    {
        setError(errorMessage, QStringLiteral("DEM 文件文本元数据无效。"));
        return false;
    }

    quint64 elevationCount = 0;
    stream >> elevationCount;
    const quint64 expectedCount = static_cast<quint64>(columns)
        * static_cast<quint64>(rows);
    if (stream.status() != QDataStream::Ok
        || elevationCount != expectedCount
        || elevationCount > MaximumGridCells
        || elevationCount > static_cast<quint64>(
            std::numeric_limits<qsizetype>::max())
        || elevationCount > static_cast<quint64>(
            file.bytesAvailable() / static_cast<qint64>(sizeof(double))))
    {
        setError(errorMessage, QStringLiteral("DEM 高程数组尺寸无效。"));
        return false;
    }
    candidate.elevations.resize(static_cast<qsizetype>(elevationCount));
    for (double& elevation : candidate.elevations)
        stream >> elevation;
    if (stream.status() != QDataStream::Ok || file.bytesAvailable() != 0
        || !validGrid(candidate, errorMessage))
    {
        if (errorMessage && errorMessage->isEmpty())
            setError(errorMessage, QStringLiteral("DEM 文件内容无效或不完整。"));
        return false;
    }
    grid = std::move(candidate);
    clearError(errorMessage);
    return true;
}

bool exportAsciiGridAtomically(const QString& path,
                               const famp::terrain::Grid& grid,
                               QString* errorMessage,
                               const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!validPath(path, errorMessage))
        return false;
    if (cancelled(shouldCancel))
    {
        setError(errorMessage, QStringLiteral("ASCII Grid 导出已取消。"));
        return false;
    }
    if (!validGrid(grid, errorMessage))
        return false;
    QSaveFile file(path);
    if (!openSaveFile(file, errorMessage))
        return false;
    QTextStream stream(&file);
    configureTextStream(stream);
    stream << "ncols " << grid.columns << '\n'
           << "nrows " << grid.rows << '\n'
           << "xllcorner " << grid.originX << '\n'
           << "yllcorner " << grid.originY << '\n'
           << "cellsize " << grid.resolution << '\n'
           << "NODATA_value " << NoDataValue << '\n';
    for (int row = grid.rows - 1; row >= 0; --row)
    {
        if ((row % 64) == 0 && cancelled(shouldCancel))
        {
            return cancelSave(
                file, QStringLiteral("ASCII Grid 导出已取消。"),
                errorMessage);
        }
        for (int column = 0; column < grid.columns; ++column)
        {
            const quint64 processed = static_cast<quint64>(
                grid.rows - 1 - row) * grid.columns
                + static_cast<quint64>(column);
            if ((processed % static_cast<quint64>(CancellationInterval)) == 0
                && cancelled(shouldCancel))
            {
                return cancelSave(
                    file, QStringLiteral("ASCII Grid 导出已取消。"),
                    errorMessage);
            }
            if (column > 0)
                stream << ' ';
            const double elevation = grid.value(row, column);
            stream << (std::isfinite(elevation) ? elevation : NoDataValue);
        }
        stream << '\n';
    }
    if (cancelled(shouldCancel))
    {
        return cancelSave(
            file, QStringLiteral("ASCII Grid 导出已取消。"), errorMessage);
    }
    stream.flush();
    return commitSaveFile(
        file, stream.status() == QTextStream::Ok, errorMessage);
}

bool exportGridCsvAtomically(const QString& path,
                             const famp::terrain::Grid& grid,
                             QString* errorMessage,
                             const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!validPath(path, errorMessage))
        return false;
    if (cancelled(shouldCancel))
    {
        setError(errorMessage, QStringLiteral("DEM CSV 导出已取消。"));
        return false;
    }
    if (!validGrid(grid, errorMessage))
        return false;
    QSaveFile file(path);
    if (!openSaveFile(file, errorMessage))
        return false;
    QTextStream stream(&file);
    configureTextStream(stream);
    stream << "column,row,x,y,elevation\n";
    for (int row = 0; row < grid.rows; ++row)
    {
        if ((row % 64) == 0 && cancelled(shouldCancel))
        {
            return cancelSave(
                file, QStringLiteral("DEM CSV 导出已取消。"), errorMessage);
        }
        for (int column = 0; column < grid.columns; ++column)
        {
            const quint64 processed = static_cast<quint64>(row)
                * grid.columns + static_cast<quint64>(column);
            if ((processed % static_cast<quint64>(CancellationInterval)) == 0
                && cancelled(shouldCancel))
            {
                return cancelSave(
                    file, QStringLiteral("DEM CSV 导出已取消。"),
                    errorMessage);
            }
            const auto center = grid.cellCenter(row, column);
            stream << column << ',' << row << ',' << center[0] << ','
                   << center[1] << ',';
            const double elevation = grid.value(row, column);
            if (std::isfinite(elevation))
                stream << elevation;
            else
                stream << "NoData";
            stream << '\n';
        }
    }
    if (cancelled(shouldCancel))
    {
        return cancelSave(
            file, QStringLiteral("DEM CSV 导出已取消。"), errorMessage);
    }
    stream.flush();
    return commitSaveFile(
        file, stream.status() == QTextStream::Ok, errorMessage);
}

bool exportContoursCsvAtomically(
    const QString& path,
    const QVector<famp::terrain::ContourLine>& contours,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!validPath(path, errorMessage)
        || !validContours(contours, errorMessage, shouldCancel))
    {
        return false;
    }
    QSaveFile file(path);
    if (!openSaveFile(file, errorMessage))
        return false;
    QTextStream stream(&file);
    configureTextStream(stream);
    stream << "line_id,elevation,point_index,x,y\n";
    for (qsizetype lineIndex = 0; lineIndex < contours.size(); ++lineIndex)
    {
        if (cancelled(shouldCancel))
        {
            return cancelSave(
                file, QStringLiteral("等高线 CSV 导出已取消。"),
                errorMessage);
        }
        const auto& line = contours.at(lineIndex);
        for (qsizetype pointIndex = 0;
             pointIndex < line.points.size(); ++pointIndex)
        {
            if ((pointIndex % CancellationInterval) == 0
                && cancelled(shouldCancel))
            {
                return cancelSave(
                    file, QStringLiteral("等高线 CSV 导出已取消。"),
                    errorMessage);
            }
            const auto& point = line.points.at(pointIndex);
            stream << lineIndex << ',' << line.elevation << ',' << pointIndex
                   << ',' << point[0] << ',' << point[1] << '\n';
        }
    }
    if (cancelled(shouldCancel))
    {
        return cancelSave(
            file, QStringLiteral("等高线 CSV 导出已取消。"), errorMessage);
    }
    stream.flush();
    return commitSaveFile(
        file, stream.status() == QTextStream::Ok, errorMessage);
}

bool exportContoursSvgAtomically(
    const QString& path,
    const QVector<famp::terrain::ContourLine>& contours,
    const QString& sourceCrs,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!validPath(path, errorMessage)
        || !validContours(contours, errorMessage, shouldCancel))
    {
        return false;
    }
    double minimumX = std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
    for (const auto& line : contours)
    {
        if (cancelled(shouldCancel))
        {
            setError(errorMessage, QStringLiteral("等高线 SVG 导出已取消。"));
            return false;
        }
        for (qsizetype pointIndex = 0;
             pointIndex < line.points.size(); ++pointIndex)
        {
            if ((pointIndex % CancellationInterval) == 0
                && cancelled(shouldCancel))
            {
                setError(errorMessage, QStringLiteral("等高线 SVG 导出已取消。"));
                return false;
            }
            const auto& point = line.points.at(pointIndex);
            minimumX = std::min(minimumX, point[0]);
            minimumY = std::min(minimumY, point[1]);
            maximumX = std::max(maximumX, point[0]);
            maximumY = std::max(maximumY, point[1]);
        }
    }
    const double width = std::max(maximumX - minimumX, 1.0e-9);
    const double height = std::max(maximumY - minimumY, 1.0e-9);
    const double strokeWidth = std::max(width, height) / 1000.0;

    QSaveFile file(path);
    if (!openSaveFile(file, errorMessage))
        return false;
    QXmlStreamWriter writer(&file);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("svg"));
    writer.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2000/svg"));
    writer.writeAttribute(QStringLiteral("viewBox"),
                          QStringLiteral("0 0 %1 %2")
                              .arg(number(width), number(height)));
    writer.writeAttribute(QStringLiteral("fill"), QStringLiteral("none"));
    writer.writeAttribute(QStringLiteral("stroke"), QStringLiteral("#8b4513"));
    writer.writeAttribute(QStringLiteral("stroke-width"), number(strokeWidth));
    writer.writeAttribute(QStringLiteral("vector-effect"),
                          QStringLiteral("non-scaling-stroke"));
    writer.writeTextElement(
        QStringLiteral("desc"),
        QStringLiteral("FAMP contours; CRS=%1").arg(sourceCrs));
    for (qsizetype lineIndex = 0; lineIndex < contours.size(); ++lineIndex)
    {
        if (cancelled(shouldCancel))
        {
            return cancelSave(
                file, QStringLiteral("等高线 SVG 导出已取消。"),
                errorMessage);
        }
        const auto& line = contours.at(lineIndex);
        QString pathData;
        pathData.reserve(line.points.size() * 36);
        for (qsizetype pointIndex = 0;
             pointIndex < line.points.size(); ++pointIndex)
        {
            if ((pointIndex % CancellationInterval) == 0
                && cancelled(shouldCancel))
            {
                return cancelSave(
                    file, QStringLiteral("等高线 SVG 导出已取消。"),
                    errorMessage);
            }
            const auto& point = line.points.at(pointIndex);
            if (pointIndex > 0)
                pathData += QStringLiteral(" L ");
            else
                pathData += QStringLiteral("M ");
            pathData += number(point[0] - minimumX);
            pathData += QLatin1Char(' ');
            pathData += number(maximumY - point[1]);
        }
        writer.writeStartElement(QStringLiteral("path"));
        writer.writeAttribute(QStringLiteral("id"),
                              QStringLiteral("contour-%1").arg(lineIndex));
        writer.writeAttribute(QStringLiteral("data-elevation"),
                              number(line.elevation));
        writer.writeAttribute(QStringLiteral("d"), pathData);
        writer.writeEndElement();
    }
    writer.writeEndElement();
    writer.writeEndDocument();
    if (cancelled(shouldCancel))
    {
        return cancelSave(
            file, QStringLiteral("等高线 SVG 导出已取消。"), errorMessage);
    }
    return commitSaveFile(file, !writer.hasError(), errorMessage);
}
}
