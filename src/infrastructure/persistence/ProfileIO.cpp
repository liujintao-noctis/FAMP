#include "ProfileIO.h"

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
constexpr char SidecarMagic[]{'F', 'P', 'R', 'F'};
constexpr quint64 MaximumBins = 2'000'000;
constexpr quint64 MaximumSamples = 10'000'000;
constexpr quint32 MaximumTextBytes = 1'048'576;
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

bool validPath(const QString& path, QString* errorMessage)
{
    if (!path.trimmed().isEmpty())
        return true;
    setError(errorMessage, QStringLiteral("剖面文件路径不能为空。"));
    return false;
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

bool cancelSave(QSaveFile& file,
                const QString& message,
                QString* errorMessage)
{
    file.cancelWriting();
    setError(errorMessage, message);
    return false;
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

bool validResult(
    const famp::profile::Result& result,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel = {})
{
    if (!result.isValid(shouldCancel)
        || static_cast<quint64>(result.bins.size()) > MaximumBins
        || static_cast<quint64>(result.samples.size()) > MaximumSamples)
    {
        setError(errorMessage,
                 cancelled(shouldCancel)
                     ? QStringLiteral("点云剖面校验已取消。")
                     : QStringLiteral("点云剖面数据无效。"));
        return false;
    }
    const QString metadata[]{
        result.sourceLayerId, result.sourceLayerName, result.sourcePath,
        result.sourceCrs, result.horizontalUnitName};
    for (const QString& text : metadata)
    {
        if (static_cast<quint64>(text.toUtf8().size()) > MaximumTextBytes)
        {
            setError(errorMessage, QStringLiteral("点云剖面元数据超过安全上限。"));
            return false;
        }
    }

    quint64 countedSamples = 0;
    for (qsizetype index = 0; index < result.bins.size(); ++index)
    {
        if ((index % CancellationInterval) == 0 && cancelled(shouldCancel))
        {
            setError(errorMessage, QStringLiteral("点云剖面校验已取消。"));
            return false;
        }
        const famp::profile::Bin& bin = result.bins.at(index);
        countedSamples += static_cast<quint64>(bin.pointCount);
        if (bin.pointCount == 0)
            continue;
        const double tolerance = std::max(
            {1.0, std::abs(bin.minimum), std::abs(bin.maximum)}) * 1.0e-12;
        if (!std::isfinite(bin.minimum) || !std::isfinite(bin.maximum)
            || !std::isfinite(bin.mean) || !std::isfinite(bin.median)
            || bin.minimum > bin.maximum
            || bin.mean < bin.minimum - tolerance
            || bin.mean > bin.maximum + tolerance
            || bin.median < bin.minimum - tolerance
            || bin.median > bin.maximum + tolerance
            || (bin.pointCount >= result.minimumPointsPerBin
                && !std::isfinite(bin.selected)))
        {
            setError(errorMessage, QStringLiteral("点云剖面采样段统计无效。"));
            return false;
        }
    }
    if (countedSamples != result.selectedPointCount)
    {
        setError(errorMessage, QStringLiteral("点云剖面点数与采样段统计不一致。"));
        return false;
    }
    const double offsetTolerance = result.corridorWidth * 0.5
        + std::max(1.0, result.corridorWidth) * 1.0e-12;
    QVector<quint64> sampleCounts(result.bins.size(), 0U);
    for (qsizetype index = 0; index < result.samples.size(); ++index)
    {
        if ((index % CancellationInterval) == 0 && cancelled(shouldCancel))
        {
            setError(errorMessage, QStringLiteral("点云剖面校验已取消。"));
            return false;
        }
        const famp::profile::Sample& sample = result.samples.at(index);
        if (sample.sourceIndex >= result.sourcePointCount
            || std::abs(sample.signedOffset) > offsetTolerance)
        {
            setError(errorMessage, QStringLiteral("点云剖面样本索引或偏距无效。"));
            return false;
        }
        ++sampleCounts[sample.binIndex];
    }
    for (qsizetype index = 0; index < result.bins.size(); ++index)
    {
        if (sampleCounts.at(index)
            != static_cast<quint64>(result.bins.at(index).pointCount))
        {
            setError(errorMessage,
                     QStringLiteral("点云剖面样本与采样段索引不一致。"));
            return false;
        }
    }
    return true;
}

QString number(double value)
{
    return QString::number(value, 'g',
                           std::numeric_limits<double>::max_digits10);
}

void writeOptionalNumber(QTextStream& stream, double value)
{
    if (std::isfinite(value))
        stream << value;
}

QString svgPath(const famp::profile::Result& result,
                double left,
                double top,
                double width,
                double height,
                double minimumElevation,
                double elevationRange,
                int valueKind)
{
    QString path;
    path.reserve(result.populatedBinCount * 30);
    bool open = false;
    for (const famp::profile::Bin& bin : result.bins)
    {
        double value = bin.selected;
        if (valueKind < 0)
            value = bin.minimum;
        else if (valueKind > 0)
            value = bin.maximum;
        if (!std::isfinite(value)
            || (valueKind != 0 && !bin.hasSelectedValue()))
        {
            open = false;
            continue;
        }
        const double x = left + width * bin.centerStation / result.length;
        const double y = top + height
            * (1.0 - (value - minimumElevation) / elevationRange);
        path += open ? QStringLiteral(" L ") : QStringLiteral("M ");
        path += number(x);
        path += QLatin1Char(' ');
        path += number(y);
        open = true;
    }
    return path;
}
}

namespace famp::profileio
{
QString pathWithProfileSuffix(const QString& path)
{
    return famp::io::pathWithRequiredSuffix(
        path, QStringLiteral("famp-profile"));
}

bool saveResultAtomically(
    const QString& path,
    const famp::profile::Result& result,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!validPath(path, errorMessage))
        return false;
    if (cancelled(shouldCancel))
    {
        setError(errorMessage, QStringLiteral("点云剖面保存已取消。"));
        return false;
    }
    if (!validResult(result, errorMessage, shouldCancel))
        return false;

    QSaveFile file(path);
    if (!openSaveFile(file, errorMessage))
        return false;
    QDataStream stream(&file);
    configureStream(stream);
    if (stream.writeRawData(SidecarMagic, 4) != 4)
    {
        return cancelSave(
            file, QStringLiteral("无法写入点云剖面文件头。"), errorMessage);
    }
    stream << SidecarSchemaVersion << static_cast<quint16>(0)
           << static_cast<qint32>(result.statistic)
           << static_cast<qint32>(result.minimumPointsPerBin)
           << result.length << result.corridorWidth << result.binSize
           << result.horizontalUnitToMetre
           << result.minimumElevation << result.maximumElevation
           << result.sourcePointCount << result.selectedPointCount
           << static_cast<qint32>(result.populatedBinCount);
    for (double value : result.baseline.start)
        stream << value;
    for (double value : result.baseline.end)
        stream << value;
    const bool metadataWritten = writeUtf8(stream, result.sourceLayerId)
        && writeUtf8(stream, result.sourceLayerName)
        && writeUtf8(stream, result.sourcePath)
        && writeUtf8(stream, result.sourceCrs)
        && writeUtf8(stream, result.horizontalUnitName);
    stream << static_cast<quint64>(result.bins.size())
           << static_cast<quint64>(result.samples.size());
    for (qsizetype index = 0; index < result.bins.size(); ++index)
    {
        if ((index % CancellationInterval) == 0 && cancelled(shouldCancel))
        {
            return cancelSave(
                file, QStringLiteral("点云剖面保存已取消。"), errorMessage);
        }
        const famp::profile::Bin& bin = result.bins.at(index);
        stream << static_cast<qint32>(bin.index)
               << bin.startStation << bin.endStation << bin.centerStation
               << static_cast<qint32>(bin.pointCount)
               << bin.minimum << bin.maximum << bin.mean << bin.median
               << bin.selected;
    }
    for (qsizetype index = 0; index < result.samples.size(); ++index)
    {
        if ((index % CancellationInterval) == 0 && cancelled(shouldCancel))
        {
            return cancelSave(
                file, QStringLiteral("点云剖面保存已取消。"), errorMessage);
        }
        const famp::profile::Sample& sample = result.samples.at(index);
        stream << sample.sourceIndex << static_cast<qint32>(sample.binIndex)
               << sample.station << sample.signedOffset
               << sample.coordinate[0] << sample.coordinate[1]
               << sample.coordinate[2];
    }
    if (cancelled(shouldCancel))
    {
        return cancelSave(
            file, QStringLiteral("点云剖面保存已取消。"), errorMessage);
    }
    return commitSaveFile(
        file, metadataWritten && stream.status() == QDataStream::Ok,
        errorMessage);
}

bool loadResult(const QString& path,
                famp::profile::Result& result,
                QString* errorMessage)
{
    if (!validPath(path, errorMessage))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(errorMessage,
                 QStringLiteral("无法读取点云剖面文件 %1：%2")
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
        setError(errorMessage, QStringLiteral("点云剖面文件标识无效。"));
        return false;
    }

    quint16 version = 0;
    quint16 reserved = 0;
    qint32 statistic = -1;
    qint32 minimumPoints = 0;
    qint32 populatedBins = 0;
    famp::profile::Result candidate;
    stream >> version >> reserved >> statistic >> minimumPoints
           >> candidate.length >> candidate.corridorWidth >> candidate.binSize
           >> candidate.horizontalUnitToMetre
           >> candidate.minimumElevation >> candidate.maximumElevation
           >> candidate.sourcePointCount >> candidate.selectedPointCount
           >> populatedBins;
    Q_UNUSED(reserved);
    for (double& value : candidate.baseline.start)
        stream >> value;
    for (double& value : candidate.baseline.end)
        stream >> value;
    if (stream.status() != QDataStream::Ok
        || version != SidecarSchemaVersion
        || statistic < static_cast<qint32>(famp::profile::Statistic::Minimum)
        || statistic > static_cast<qint32>(famp::profile::Statistic::Median)
        || minimumPoints < 1 || populatedBins < 2)
    {
        setError(errorMessage, QStringLiteral("点云剖面文件版本或元数据无效。"));
        return false;
    }
    candidate.statistic = static_cast<famp::profile::Statistic>(statistic);
    candidate.minimumPointsPerBin = minimumPoints;
    candidate.populatedBinCount = populatedBins;
    if (!readUtf8(stream, candidate.sourceLayerId)
        || !readUtf8(stream, candidate.sourceLayerName)
        || !readUtf8(stream, candidate.sourcePath)
        || !readUtf8(stream, candidate.sourceCrs)
        || !readUtf8(stream, candidate.horizontalUnitName))
    {
        setError(errorMessage, QStringLiteral("点云剖面文本元数据无效。"));
        return false;
    }

    quint64 binCount = 0;
    quint64 sampleCount = 0;
    stream >> binCount >> sampleCount;
    if (stream.status() != QDataStream::Ok
        || binCount < 2 || binCount > MaximumBins
        || sampleCount < 2 || sampleCount > MaximumSamples
        || binCount > static_cast<quint64>(
            std::numeric_limits<qsizetype>::max())
        || sampleCount > static_cast<quint64>(
            std::numeric_limits<qsizetype>::max())
        || sampleCount != candidate.selectedPointCount)
    {
        setError(errorMessage, QStringLiteral("点云剖面数组尺寸无效。"));
        return false;
    }
    constexpr quint64 MinimumBinBytes = 4U + 3U * 8U + 4U + 5U * 8U;
    constexpr quint64 MinimumSampleBytes = 8U + 4U + 5U * 8U;
    const quint64 available = static_cast<quint64>(file.bytesAvailable());
    if (binCount > available / MinimumBinBytes
        || sampleCount
            > (available - binCount * MinimumBinBytes) / MinimumSampleBytes)
    {
        setError(errorMessage, QStringLiteral("点云剖面文件内容不完整。"));
        return false;
    }

    candidate.bins.resize(static_cast<qsizetype>(binCount));
    for (famp::profile::Bin& bin : candidate.bins)
    {
        qint32 index = -1;
        qint32 pointCount = -1;
        stream >> index >> bin.startStation >> bin.endStation
               >> bin.centerStation >> pointCount
               >> bin.minimum >> bin.maximum >> bin.mean >> bin.median
               >> bin.selected;
        bin.index = index;
        bin.pointCount = pointCount;
    }
    candidate.samples.resize(static_cast<qsizetype>(sampleCount));
    for (famp::profile::Sample& sample : candidate.samples)
    {
        qint32 binIndex = -1;
        stream >> sample.sourceIndex >> binIndex
               >> sample.station >> sample.signedOffset
               >> sample.coordinate[0] >> sample.coordinate[1]
               >> sample.coordinate[2];
        sample.binIndex = binIndex;
    }
    if (stream.status() != QDataStream::Ok || file.bytesAvailable() != 0
        || !validResult(candidate, errorMessage))
    {
        if (errorMessage && errorMessage->isEmpty())
        {
            setError(errorMessage,
                     QStringLiteral("点云剖面文件内容无效或不完整。"));
        }
        return false;
    }
    result = std::move(candidate);
    clearError(errorMessage);
    return true;
}

bool exportBinsCsvAtomically(
    const QString& path,
    const famp::profile::Result& result,
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
    stream << "bin_index,station_start,station_end,station_center,"
              "station_center_m,point_count,minimum,maximum,mean,median,"
              "selected\n";
    for (qsizetype index = 0; index < result.bins.size(); ++index)
    {
        if ((index % CancellationInterval) == 0 && cancelled(shouldCancel))
        {
            return cancelSave(
                file, QStringLiteral("剖面采样段 CSV 导出已取消。"),
                errorMessage);
        }
        const famp::profile::Bin& bin = result.bins.at(index);
        stream << bin.index << ',' << bin.startStation << ','
               << bin.endStation << ',' << bin.centerStation << ','
               << bin.centerStation * result.horizontalUnitToMetre << ','
               << bin.pointCount << ',';
        writeOptionalNumber(stream, bin.minimum);
        stream << ',';
        writeOptionalNumber(stream, bin.maximum);
        stream << ',';
        writeOptionalNumber(stream, bin.mean);
        stream << ',';
        writeOptionalNumber(stream, bin.median);
        stream << ',';
        writeOptionalNumber(stream, bin.selected);
        stream << '\n';
    }
    if (cancelled(shouldCancel))
    {
        return cancelSave(
            file, QStringLiteral("剖面采样段 CSV 导出已取消。"),
            errorMessage);
    }
    stream.flush();
    return commitSaveFile(
        file, stream.status() == QTextStream::Ok, errorMessage);
}

bool exportSamplesCsvAtomically(
    const QString& path,
    const famp::profile::Result& result,
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
    stream << "source_index,bin_index,station,station_m,signed_offset,"
              "signed_offset_m,x,y,z\n";
    for (qsizetype index = 0; index < result.samples.size(); ++index)
    {
        if ((index % CancellationInterval) == 0 && cancelled(shouldCancel))
        {
            return cancelSave(
                file, QStringLiteral("剖面原始点 CSV 导出已取消。"),
                errorMessage);
        }
        const famp::profile::Sample& sample = result.samples.at(index);
        stream << sample.sourceIndex << ',' << sample.binIndex << ','
               << sample.station << ','
               << sample.station * result.horizontalUnitToMetre << ','
               << sample.signedOffset << ','
               << sample.signedOffset * result.horizontalUnitToMetre << ','
               << sample.coordinate[0] << ',' << sample.coordinate[1] << ','
               << sample.coordinate[2] << '\n';
    }
    if (cancelled(shouldCancel))
    {
        return cancelSave(
            file, QStringLiteral("剖面原始点 CSV 导出已取消。"), errorMessage);
    }
    stream.flush();
    return commitSaveFile(
        file, stream.status() == QTextStream::Ok, errorMessage);
}

bool exportSvgAtomically(
    const QString& path,
    const famp::profile::Result& result,
    QString* errorMessage,
    const famp::tasks::CancellationCheck& shouldCancel)
{
    if (!validPath(path, errorMessage)
        || !validResult(result, errorMessage, shouldCancel))
    {
        return false;
    }
    if (cancelled(shouldCancel))
    {
        setError(errorMessage, QStringLiteral("剖面 SVG 导出已取消。"));
        return false;
    }

    constexpr double canvasWidth = 1200.0;
    constexpr double canvasHeight = 650.0;
    constexpr double left = 95.0;
    constexpr double right = 35.0;
    constexpr double top = 55.0;
    constexpr double bottom = 80.0;
    constexpr double plotWidth = canvasWidth - left - right;
    constexpr double plotHeight = canvasHeight - top - bottom;
    double minimumElevation = result.minimumElevation;
    double maximumElevation = result.maximumElevation;
    double range = maximumElevation - minimumElevation;
    if (range <= std::max(1.0, std::abs(maximumElevation)) * 1.0e-12)
    {
        const double padding = std::max(0.5, std::abs(maximumElevation) * 0.01);
        minimumElevation -= padding;
        maximumElevation += padding;
        range = maximumElevation - minimumElevation;
    }
    else
    {
        const double padding = range * 0.05;
        minimumElevation -= padding;
        maximumElevation += padding;
        range = maximumElevation - minimumElevation;
    }

    QSaveFile file(path);
    if (!openSaveFile(file, errorMessage))
        return false;
    QXmlStreamWriter writer(&file);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("svg"));
    writer.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2000/svg"));
    writer.writeAttribute(QStringLiteral("viewBox"),
                          QStringLiteral("0 0 1200 650"));
    writer.writeAttribute(QStringLiteral("width"), QStringLiteral("1200"));
    writer.writeAttribute(QStringLiteral("height"), QStringLiteral("650"));
    writer.writeTextElement(
        QStringLiteral("title"),
        QStringLiteral("FAMP 点云剖面 — %1").arg(result.sourceLayerName));
    writer.writeTextElement(
        QStringLiteral("desc"),
        QStringLiteral("CRS=%1; statistic=%2; corridor_width=%3; bin_size=%4")
            .arg(result.sourceCrs,
                 famp::profile::statisticName(result.statistic),
                 number(result.corridorWidth), number(result.binSize)));

    writer.writeStartElement(QStringLiteral("rect"));
    writer.writeAttribute(QStringLiteral("x"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("y"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("width"), QStringLiteral("1200"));
    writer.writeAttribute(QStringLiteral("height"), QStringLiteral("650"));
    writer.writeAttribute(QStringLiteral("fill"), QStringLiteral("white"));
    writer.writeEndElement();

    for (int index = 0; index <= 5; ++index)
    {
        if (cancelled(shouldCancel))
        {
            return cancelSave(
                file, QStringLiteral("剖面 SVG 导出已取消。"), errorMessage);
        }
        const double fraction = static_cast<double>(index) / 5.0;
        const double y = top + plotHeight * fraction;
        writer.writeStartElement(QStringLiteral("line"));
        writer.writeAttribute(QStringLiteral("x1"), number(left));
        writer.writeAttribute(QStringLiteral("x2"), number(left + plotWidth));
        writer.writeAttribute(QStringLiteral("y1"), number(y));
        writer.writeAttribute(QStringLiteral("y2"), number(y));
        writer.writeAttribute(QStringLiteral("stroke"), QStringLiteral("#d9d9d9"));
        writer.writeAttribute(QStringLiteral("stroke-width"), QStringLiteral("1"));
        writer.writeEndElement();
        writer.writeStartElement(QStringLiteral("text"));
        writer.writeAttribute(QStringLiteral("x"), number(left - 10.0));
        writer.writeAttribute(QStringLiteral("y"), number(y + 5.0));
        writer.writeAttribute(QStringLiteral("text-anchor"), QStringLiteral("end"));
        writer.writeAttribute(QStringLiteral("font-size"), QStringLiteral("14"));
        writer.writeCharacters(number(maximumElevation - range * fraction));
        writer.writeEndElement();
    }
    for (int index = 0; index <= 5; ++index)
    {
        const double fraction = static_cast<double>(index) / 5.0;
        const double x = left + plotWidth * fraction;
        writer.writeStartElement(QStringLiteral("line"));
        writer.writeAttribute(QStringLiteral("x1"), number(x));
        writer.writeAttribute(QStringLiteral("x2"), number(x));
        writer.writeAttribute(QStringLiteral("y1"), number(top));
        writer.writeAttribute(QStringLiteral("y2"), number(top + plotHeight));
        writer.writeAttribute(QStringLiteral("stroke"), QStringLiteral("#eeeeee"));
        writer.writeAttribute(QStringLiteral("stroke-width"), QStringLiteral("1"));
        writer.writeEndElement();
        writer.writeStartElement(QStringLiteral("text"));
        writer.writeAttribute(QStringLiteral("x"), number(x));
        writer.writeAttribute(QStringLiteral("y"), number(top + plotHeight + 25.0));
        writer.writeAttribute(QStringLiteral("text-anchor"), QStringLiteral("middle"));
        writer.writeAttribute(QStringLiteral("font-size"), QStringLiteral("14"));
        writer.writeCharacters(number(
            result.length * result.horizontalUnitToMetre * fraction));
        writer.writeEndElement();
    }

    const auto writeProfilePath = [&](int kind,
                                      const QString& stroke,
                                      const QString& width,
                                      const QString& dash = QString()) {
        writer.writeStartElement(QStringLiteral("path"));
        writer.writeAttribute(
            QStringLiteral("d"),
            svgPath(result, left, top, plotWidth, plotHeight,
                    minimumElevation, range, kind));
        writer.writeAttribute(QStringLiteral("fill"), QStringLiteral("none"));
        writer.writeAttribute(QStringLiteral("stroke"), stroke);
        writer.writeAttribute(QStringLiteral("stroke-width"), width);
        writer.writeAttribute(QStringLiteral("vector-effect"),
                              QStringLiteral("non-scaling-stroke"));
        if (!dash.isEmpty())
            writer.writeAttribute(QStringLiteral("stroke-dasharray"), dash);
        writer.writeEndElement();
    };
    writeProfilePath(-1, QStringLiteral("#a9a9a9"), QStringLiteral("1.2"),
                     QStringLiteral("5 4"));
    writeProfilePath(1, QStringLiteral("#a9a9a9"), QStringLiteral("1.2"),
                     QStringLiteral("5 4"));
    writeProfilePath(0, QStringLiteral("#1565c0"), QStringLiteral("2.5"));

    writer.writeStartElement(QStringLiteral("rect"));
    writer.writeAttribute(QStringLiteral("x"), number(left));
    writer.writeAttribute(QStringLiteral("y"), number(top));
    writer.writeAttribute(QStringLiteral("width"), number(plotWidth));
    writer.writeAttribute(QStringLiteral("height"), number(plotHeight));
    writer.writeAttribute(QStringLiteral("fill"), QStringLiteral("none"));
    writer.writeAttribute(QStringLiteral("stroke"), QStringLiteral("#333333"));
    writer.writeAttribute(QStringLiteral("stroke-width"), QStringLiteral("1.5"));
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("text"));
    writer.writeAttribute(QStringLiteral("x"), number(left + plotWidth * 0.5));
    writer.writeAttribute(QStringLiteral("y"), number(canvasHeight - 25.0));
    writer.writeAttribute(QStringLiteral("text-anchor"), QStringLiteral("middle"));
    writer.writeAttribute(QStringLiteral("font-size"), QStringLiteral("17"));
    writer.writeCharacters(QStringLiteral("沿剖面距离（米）"));
    writer.writeEndElement();
    writer.writeStartElement(QStringLiteral("text"));
    writer.writeAttribute(QStringLiteral("x"), QStringLiteral("24"));
    writer.writeAttribute(QStringLiteral("y"), number(top + plotHeight * 0.5));
    writer.writeAttribute(QStringLiteral("text-anchor"), QStringLiteral("middle"));
    writer.writeAttribute(QStringLiteral("font-size"), QStringLiteral("17"));
    writer.writeAttribute(QStringLiteral("transform"),
                          QStringLiteral("rotate(-90 24 %1)")
                              .arg(number(top + plotHeight * 0.5)));
    writer.writeCharacters(QStringLiteral("高程（源坐标单位）"));
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndDocument();

    if (cancelled(shouldCancel))
    {
        return cancelSave(
            file, QStringLiteral("剖面 SVG 导出已取消。"), errorMessage);
    }
    return commitSaveFile(file, !writer.hasError(), errorMessage);
}
}
