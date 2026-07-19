#include "CutFillIO.h"

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
constexpr char SidecarMagic[]{'F', 'V', 'O', 'L'};
constexpr quint64 MaximumGridCells = 25'000'000;
constexpr quint32 MaximumTextBytes = 1'048'576;
constexpr qsizetype CancellationInterval = 4096;
constexpr quint64 MaximumSvgBlocks = 250'000;

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

bool validPath(const QString& path, QString* errorMessage)
{
    if (!path.trimmed().isEmpty())
        return true;
    setError(errorMessage, QStringLiteral("挖填方成果文件路径不能为空。"));
    return false;
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

bool cancelSave(QSaveFile& file,
                const QString& message,
                QString* errorMessage)
{
    file.cancelWriting();
    setError(errorMessage, message);
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

void configureStream(QDataStream& stream)
{
    stream.setVersion(QDataStream::Qt_5_15);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
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

bool validMetadata(const famp::cutfill::Result& result)
{
    const QString values[]{
        result.currentGrid.sourceLayerId,
        result.currentGrid.sourceLayerName,
        result.currentGrid.sourceCrs,
        result.currentGrid.horizontalUnitName,
        result.referencePath,
        result.referenceLayerId,
        result.referenceLayerName,
        result.referenceCrs};
    return std::all_of(std::begin(values), std::end(values),
                       [](const QString& value) {
        return static_cast<quint64>(value.toUtf8().size())
            <= MaximumTextBytes;
    });
}

bool validResult(
    const famp::cutfill::Result& result,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel = {})
{
    if (cancelled(shouldCancel))
    {
        setError(errorMessage, QStringLiteral("挖填方成果写入已取消。"));
        return false;
    }
    if (result.cancelled || !result.error.isEmpty()
        || static_cast<quint64>(result.differences.size()) > MaximumGridCells
        || !validMetadata(result)
        || !result.isValid(shouldCancel))
    {
        if (cancelled(shouldCancel))
            setError(errorMessage, QStringLiteral("挖填方成果写入已取消。"));
        else
            setError(errorMessage, QStringLiteral("挖填方成果数据无效。"));
        return false;
    }
    return true;
}

QString csvField(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + escaped + QLatin1Char('"');
}

QString number(double value)
{
    return QString::number(value, 'g',
                           std::numeric_limits<double>::max_digits10);
}

QString blockColor(double differenceMetres,
                   double maximumAbsoluteDifference,
                   double toleranceMetres)
{
    if (std::abs(differenceMetres) <= toleranceMetres)
        return QStringLiteral("#9ca3af");
    const double normalized = maximumAbsoluteDifference > 0.0
        ? std::clamp(std::abs(differenceMetres)
                         / maximumAbsoluteDifference,
                     0.0, 1.0)
        : 0.0;
    const int light = static_cast<int>(
        std::round(225.0 - 125.0 * std::sqrt(normalized)));
    if (differenceMetres > 0.0)
        return QStringLiteral("#%1%2%2")
            .arg(220, 2, 16, QLatin1Char('0'))
            .arg(light, 2, 16, QLatin1Char('0'));
    return QStringLiteral("#%1%2%3")
        .arg(light, 2, 16, QLatin1Char('0'))
        .arg(light + 15, 2, 16, QLatin1Char('0'))
        .arg(220, 2, 16, QLatin1Char('0'));
}
}

namespace famp::cutfillio
{
QString pathWithVolumeSuffix(const QString& path)
{
    return famp::io::pathWithRequiredSuffix(
        path, QStringLiteral("famp-volume"));
}

bool saveResultAtomically(
    const QString& path,
    const famp::cutfill::Result& result,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!validPath(path, errorMessage)
        || !validResult(result, errorMessage, shouldCancel))
    {
        return false;
    }
    QSaveFile file(path);
    if (!openSaveFile(file, errorMessage))
        return false;
    QDataStream stream(&file);
    configureStream(stream);
    if (stream.writeRawData(SidecarMagic, 4) != 4)
        return cancelSave(file, QStringLiteral("无法写入挖填方文件头。"),
                          errorMessage);

    const auto& grid = result.currentGrid;
    stream << SidecarSchemaVersion << static_cast<quint16>(0)
           << static_cast<qint32>(result.referenceMode)
           << static_cast<qint32>(grid.columns)
           << static_cast<qint32>(grid.rows)
           << grid.originX << grid.originY << grid.resolution
           << grid.horizontalUnitToMetre
           << static_cast<qint32>(grid.statistic)
           << static_cast<qint32>(grid.sourcePointCount)
           << static_cast<qint32>(grid.populatedCellCount)
           << static_cast<qint32>(grid.filledCellCount)
           << result.constantReferenceElevation
           << result.zeroTolerance
           << result.currentValidCellCount
           << result.currentNoDataCellCount
           << result.comparedCellCount
           << result.missingReferenceCellCount
           << result.cutCellCount
           << result.fillCellCount
           << result.unchangedCellCount
           << result.cellAreaSquareMetres
           << result.cutAreaSquareMetres
           << result.fillAreaSquareMetres
           << result.unchangedAreaSquareMetres
           << result.cutVolumeCubicMetres
           << result.fillVolumeCubicMetres
           << result.signedVolumeCubicMetres
           << result.minimumDifferenceMetres
           << result.maximumDifferenceMetres;
    const bool metadataWritten =
        writeUtf8(stream, grid.sourceLayerId)
        && writeUtf8(stream, grid.sourceLayerName)
        && writeUtf8(stream, grid.sourceCrs)
        && writeUtf8(stream, grid.horizontalUnitName)
        && writeUtf8(stream, result.referencePath)
        && writeUtf8(stream, result.referenceLayerId)
        && writeUtf8(stream, result.referenceLayerName)
        && writeUtf8(stream, result.referenceCrs);

    stream << static_cast<quint64>(grid.elevations.size());
    for (qsizetype index = 0; index < grid.elevations.size(); ++index)
    {
        if ((index % CancellationInterval) == 0
            && cancelled(shouldCancel))
        {
            return cancelSave(file, QStringLiteral("挖填方成果保存已取消。"),
                              errorMessage);
        }
        stream << grid.elevations.at(index);
    }
    stream << static_cast<quint64>(result.differences.size());
    for (qsizetype index = 0; index < result.differences.size(); ++index)
    {
        if ((index % CancellationInterval) == 0
            && cancelled(shouldCancel))
        {
            return cancelSave(file, QStringLiteral("挖填方成果保存已取消。"),
                              errorMessage);
        }
        stream << result.differences.at(index);
    }
    if (cancelled(shouldCancel))
    {
        return cancelSave(file, QStringLiteral("挖填方成果保存已取消。"),
                          errorMessage);
    }
    return commitSaveFile(
        file, metadataWritten && stream.status() == QDataStream::Ok,
        errorMessage);
}

bool loadResult(const QString& path,
                famp::cutfill::Result& result,
                QString* errorMessage)
{
    if (!validPath(path, errorMessage))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(errorMessage,
                 QStringLiteral("无法读取挖填方成果 %1：%2")
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
        setError(errorMessage, QStringLiteral("挖填方成果文件标识无效。"));
        return false;
    }

    quint16 version = 0;
    quint16 reserved = 0;
    qint32 referenceMode = -1;
    qint32 columns = 0;
    qint32 rows = 0;
    qint32 statistic = -1;
    qint32 sourcePointCount = -1;
    qint32 populatedCellCount = -1;
    qint32 filledCellCount = -1;
    famp::cutfill::Result candidate;
    stream >> version >> reserved >> referenceMode >> columns >> rows
           >> candidate.currentGrid.originX
           >> candidate.currentGrid.originY
           >> candidate.currentGrid.resolution
           >> candidate.currentGrid.horizontalUnitToMetre
           >> statistic >> sourcePointCount
           >> populatedCellCount >> filledCellCount
           >> candidate.constantReferenceElevation
           >> candidate.zeroTolerance
           >> candidate.currentValidCellCount
           >> candidate.currentNoDataCellCount
           >> candidate.comparedCellCount
           >> candidate.missingReferenceCellCount
           >> candidate.cutCellCount
           >> candidate.fillCellCount
           >> candidate.unchangedCellCount
           >> candidate.cellAreaSquareMetres
           >> candidate.cutAreaSquareMetres
           >> candidate.fillAreaSquareMetres
           >> candidate.unchangedAreaSquareMetres
           >> candidate.cutVolumeCubicMetres
           >> candidate.fillVolumeCubicMetres
           >> candidate.signedVolumeCubicMetres
           >> candidate.minimumDifferenceMetres
           >> candidate.maximumDifferenceMetres;
    Q_UNUSED(reserved);
    if (stream.status() != QDataStream::Ok
        || version != SidecarSchemaVersion
        || referenceMode < static_cast<qint32>(
            famp::cutfill::ReferenceMode::ConstantElevation)
        || referenceMode > static_cast<qint32>(
            famp::cutfill::ReferenceMode::DemGrid)
        || columns <= 0 || rows <= 0
        || statistic < static_cast<qint32>(
            famp::terrain::CellStatistic::Minimum)
        || statistic > static_cast<qint32>(
            famp::terrain::CellStatistic::Median)
        || sourcePointCount < 0
        || populatedCellCount < 0 || filledCellCount < 0)
    {
        setError(errorMessage,
                 QStringLiteral("挖填方成果文件版本或元数据无效。"));
        return false;
    }
    candidate.referenceMode = static_cast<famp::cutfill::ReferenceMode>(
        referenceMode);
    candidate.currentGrid.columns = columns;
    candidate.currentGrid.rows = rows;
    candidate.currentGrid.statistic = static_cast<
        famp::terrain::CellStatistic>(statistic);
    candidate.currentGrid.sourcePointCount = sourcePointCount;
    candidate.currentGrid.populatedCellCount = populatedCellCount;
    candidate.currentGrid.filledCellCount = filledCellCount;
    if (!readUtf8(stream, candidate.currentGrid.sourceLayerId)
        || !readUtf8(stream, candidate.currentGrid.sourceLayerName)
        || !readUtf8(stream, candidate.currentGrid.sourceCrs)
        || !readUtf8(stream, candidate.currentGrid.horizontalUnitName)
        || !readUtf8(stream, candidate.referencePath)
        || !readUtf8(stream, candidate.referenceLayerId)
        || !readUtf8(stream, candidate.referenceLayerName)
        || !readUtf8(stream, candidate.referenceCrs))
    {
        setError(errorMessage,
                 QStringLiteral("挖填方成果文本元数据无效。"));
        return false;
    }

    const quint64 expectedCount = static_cast<quint64>(columns)
        * static_cast<quint64>(rows);
    quint64 elevationCount = 0;
    stream >> elevationCount;
    if (stream.status() != QDataStream::Ok
        || elevationCount != expectedCount
        || elevationCount > MaximumGridCells
        || elevationCount > static_cast<quint64>(
            std::numeric_limits<qsizetype>::max())
        || elevationCount > static_cast<quint64>(
            file.bytesAvailable() / static_cast<qint64>(sizeof(double))))
    {
        setError(errorMessage,
                 QStringLiteral("挖填方当前高程数组尺寸无效。"));
        return false;
    }
    candidate.currentGrid.elevations.resize(
        static_cast<qsizetype>(elevationCount));
    for (double& elevation : candidate.currentGrid.elevations)
        stream >> elevation;

    quint64 differenceCount = 0;
    stream >> differenceCount;
    if (stream.status() != QDataStream::Ok
        || differenceCount != expectedCount
        || differenceCount > static_cast<quint64>(
            file.bytesAvailable() / static_cast<qint64>(sizeof(double))))
    {
        setError(errorMessage,
                 QStringLiteral("挖填方高差数组尺寸无效。"));
        return false;
    }
    candidate.differences.resize(static_cast<qsizetype>(differenceCount));
    for (double& difference : candidate.differences)
        stream >> difference;
    if (stream.status() != QDataStream::Ok || file.bytesAvailable() != 0
        || !validResult(candidate, errorMessage))
    {
        if (errorMessage && errorMessage->isEmpty())
        {
            setError(errorMessage,
                     QStringLiteral("挖填方成果文件内容无效或不完整。"));
        }
        return false;
    }
    result = std::move(candidate);
    clearError(errorMessage);
    return true;
}

bool exportSummaryCsvAtomically(
    const QString& path,
    const famp::cutfill::Result& result,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!validPath(path, errorMessage)
        || !validResult(result, errorMessage, shouldCancel))
    {
        return false;
    }
    QSaveFile file(path);
    if (!openSaveFile(file, errorMessage))
        return false;
    QTextStream stream(&file);
    configureTextStream(stream);
    stream << "metric,value,unit\n"
           << "reference_mode,"
           << csvField(famp::cutfill::referenceModeName(result.referenceMode))
           << ",\n"
           << "source_layer," << csvField(result.currentGrid.sourceLayerName)
           << ",\n"
           << "source_layer_id," << csvField(result.currentGrid.sourceLayerId)
           << ",\n"
           << "source_crs," << csvField(result.currentGrid.sourceCrs)
           << ",\n";
    if (result.referenceMode
        == famp::cutfill::ReferenceMode::ConstantElevation)
    {
        stream << "reference_elevation,"
               << result.constantReferenceElevation
                    * result.currentGrid.horizontalUnitToMetre
               << ",m\n";
    }
    else
    {
        stream << "reference_dem," << csvField(result.referencePath)
               << ",\n"
               << "reference_layer,"
               << csvField(result.referenceLayerName) << ",\n";
    }
    stream << "grid_columns," << result.currentGrid.columns << ",cell\n"
           << "grid_rows," << result.currentGrid.rows << ",cell\n"
           << "grid_resolution,"
           << result.currentGrid.resolution
               * result.currentGrid.horizontalUnitToMetre
           << ",m\n"
           << "zero_tolerance,"
           << result.zeroTolerance
                * result.currentGrid.horizontalUnitToMetre
           << ",m\n"
           << "current_valid_cells," << result.currentValidCellCount
           << ",cell\n"
           << "current_nodata_cells," << result.currentNoDataCellCount
           << ",cell\n"
           << "compared_cells," << result.comparedCellCount << ",cell\n"
           << "missing_reference_cells," << result.missingReferenceCellCount
           << ",cell\n"
           << "cut_cells," << result.cutCellCount << ",cell\n"
           << "fill_cells," << result.fillCellCount << ",cell\n"
           << "unchanged_cells," << result.unchangedCellCount << ",cell\n"
           << "cut_area," << result.cutAreaSquareMetres << ",m2\n"
           << "fill_area," << result.fillAreaSquareMetres << ",m2\n"
           << "unchanged_area," << result.unchangedAreaSquareMetres
           << ",m2\n"
           << "cut_volume," << result.cutVolumeCubicMetres << ",m3\n"
           << "fill_volume," << result.fillVolumeCubicMetres << ",m3\n"
           << "signed_volume_cut_minus_fill,"
           << result.signedVolumeCubicMetres << ",m3\n"
           << "minimum_difference," << result.minimumDifferenceMetres
           << ",m\n"
           << "maximum_difference," << result.maximumDifferenceMetres
           << ",m\n";
    if (cancelled(shouldCancel))
        return cancelSave(file, QStringLiteral("挖填方汇总 CSV 导出已取消。"),
                          errorMessage);
    return commitSaveFile(file, stream.status() == QTextStream::Ok,
                          errorMessage);
}

bool exportCellsCsvAtomically(
    const QString& path,
    const famp::cutfill::Result& result,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!validPath(path, errorMessage)
        || !validResult(result, errorMessage, shouldCancel))
    {
        return false;
    }
    QSaveFile file(path);
    if (!openSaveFile(file, errorMessage))
        return false;
    QTextStream stream(&file);
    configureTextStream(stream);
    stream << "column,row,x,y,current_elevation,reference_elevation,"
              "difference_m,classification,cell_area_m2,signed_volume_m3\n";
    const auto& grid = result.currentGrid;
    for (qsizetype index = 0; index < grid.elevations.size(); ++index)
    {
        if ((index % CancellationInterval) == 0
            && cancelled(shouldCancel))
        {
            return cancelSave(file,
                              QStringLiteral("挖填方网格 CSV 导出已取消。"),
                              errorMessage);
        }
        const double current = grid.elevations.at(index);
        if (!std::isfinite(current))
            continue;
        const int row = static_cast<int>(index / grid.columns);
        const int column = static_cast<int>(index % grid.columns);
        const auto center = grid.cellCenter(row, column);
        const double difference = result.differences.at(index);
        const double reference = result.referenceElevationAt(index);
        const auto classification = result.classificationAt(index);
        stream << column << ',' << row << ','
               << center[0] << ',' << center[1] << ',' << current << ',';
        if (std::isfinite(reference))
            stream << reference;
        stream << ',';
        if (std::isfinite(difference))
            stream << difference * grid.horizontalUnitToMetre;
        stream << ',' << csvField(
                      famp::cutfill::classificationName(classification))
               << ',' << result.cellAreaSquareMetres << ',';
        if (std::isfinite(difference))
        {
            stream << difference * grid.horizontalUnitToMetre
                * result.cellAreaSquareMetres;
        }
        stream << '\n';
    }
    if (cancelled(shouldCancel))
    {
        return cancelSave(file, QStringLiteral("挖填方网格 CSV 导出已取消。"),
                          errorMessage);
    }
    return commitSaveFile(file, stream.status() == QTextStream::Ok,
                          errorMessage);
}

bool exportSvgAtomically(
    const QString& path,
    const famp::cutfill::Result& result,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!validPath(path, errorMessage)
        || !validResult(result, errorMessage, shouldCancel))
    {
        return false;
    }
    const auto& grid = result.currentGrid;
    const quint64 cellCount = static_cast<quint64>(grid.columns)
        * static_cast<quint64>(grid.rows);
    int blockSize = std::max(1, static_cast<int>(std::ceil(
        std::sqrt(static_cast<double>(cellCount)
                  / static_cast<double>(MaximumSvgBlocks)))));
    const auto blockCount = [&]() {
        return (static_cast<quint64>(grid.columns) + blockSize - 1)
                / blockSize
            * ((static_cast<quint64>(grid.rows) + blockSize - 1)
               / blockSize);
    };
    while (blockCount() > MaximumSvgBlocks)
        ++blockSize;
    const double maximumAbsoluteDifference = std::max(
        std::abs(result.minimumDifferenceMetres),
        std::abs(result.maximumDifferenceMetres));
    const double toleranceMetres = result.zeroTolerance
        * grid.horizontalUnitToMetre;

    QSaveFile file(path);
    if (!openSaveFile(file, errorMessage))
        return false;
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("svg"));
    xml.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2000/svg"));
    xml.writeAttribute(QStringLiteral("viewBox"),
                       QStringLiteral("0 0 %1 %2")
                           .arg(grid.columns).arg(grid.rows));
    xml.writeAttribute(QStringLiteral("preserveAspectRatio"),
                       QStringLiteral("xMidYMid meet"));
    xml.writeAttribute(QStringLiteral("data-crs"), grid.sourceCrs);
    xml.writeAttribute(QStringLiteral("data-cut-volume-m3"),
                       number(result.cutVolumeCubicMetres));
    xml.writeAttribute(QStringLiteral("data-fill-volume-m3"),
                       number(result.fillVolumeCubicMetres));
    xml.writeTextElement(
        QStringLiteral("title"),
        QStringLiteral("FAMP 挖填方图：红色为挖方，蓝色为填方，灰色为容差内平衡"));
    xml.writeStartElement(QStringLiteral("g"));
    xml.writeAttribute(QStringLiteral("shape-rendering"),
                       QStringLiteral("crispEdges"));

    quint64 processed = 0;
    for (int blockRow = 0; blockRow < grid.rows;
         blockRow += blockSize)
    {
        for (int blockColumn = 0; blockColumn < grid.columns;
             blockColumn += blockSize)
        {
            if ((processed++ % 256) == 0 && cancelled(shouldCancel))
            {
                return cancelSave(file,
                                  QStringLiteral("挖填方 SVG 导出已取消。"),
                                  errorMessage);
            }
            const int endRow = std::min(grid.rows, blockRow + blockSize);
            const int endColumn = std::min(
                grid.columns, blockColumn + blockSize);
            long double sum = 0.0L;
            quint64 count = 0;
            for (int row = blockRow; row < endRow; ++row)
            {
                for (int column = blockColumn;
                     column < endColumn; ++column)
                {
                    const double difference = result.differences.at(
                        grid.index(row, column));
                    if (!std::isfinite(difference))
                        continue;
                    sum += static_cast<long double>(difference)
                        * grid.horizontalUnitToMetre;
                    ++count;
                }
            }
            if (count == 0)
                continue;
            const double meanDifference = static_cast<double>(sum / count);
            xml.writeStartElement(QStringLiteral("rect"));
            xml.writeAttribute(QStringLiteral("x"),
                               QString::number(blockColumn));
            xml.writeAttribute(
                QStringLiteral("y"),
                QString::number(grid.rows - endRow));
            xml.writeAttribute(QStringLiteral("width"),
                               QString::number(endColumn - blockColumn));
            xml.writeAttribute(QStringLiteral("height"),
                               QString::number(endRow - blockRow));
            xml.writeAttribute(
                QStringLiteral("fill"),
                blockColor(meanDifference, maximumAbsoluteDifference,
                           toleranceMetres));
            xml.writeAttribute(QStringLiteral("data-mean-difference-m"),
                               number(meanDifference));
            xml.writeEndElement();
        }
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    if (cancelled(shouldCancel))
    {
        return cancelSave(file, QStringLiteral("挖填方 SVG 导出已取消。"),
                          errorMessage);
    }
    return commitSaveFile(file, !xml.hasError(), errorMessage);
}
}
