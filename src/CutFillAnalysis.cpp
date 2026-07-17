#include "CutFillAnalysis.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
constexpr qsizetype CancellationInterval = 4096;
constexpr double RelativeTolerance = 1.0e-10;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool cancelled(const famp::tasks::CancellationCheck& shouldCancel)
{
    return famp::tasks::isCancellationRequested(shouldCancel);
}

bool closeEnough(double first, double second,
                 double relativeTolerance = RelativeTolerance)
{
    if (!std::isfinite(first) || !std::isfinite(second))
        return false;
    const double scale = std::max({1.0, std::abs(first), std::abs(second)});
    return std::abs(first - second) <= relativeTolerance * scale;
}

bool alignedOffset(double currentOrigin,
                   double referenceOrigin,
                   double resolution,
                   qint64& offset)
{
    const long double raw =
        (static_cast<long double>(currentOrigin)
         - static_cast<long double>(referenceOrigin))
        / static_cast<long double>(resolution);
    if (!std::isfinite(static_cast<double>(raw)))
        return false;
    const long double rounded = std::round(raw);
    if (rounded < static_cast<long double>(
            std::numeric_limits<qint64>::min())
        || rounded > static_cast<long double>(
            std::numeric_limits<qint64>::max()))
    {
        return false;
    }
    const long double reconstructed =
        static_cast<long double>(referenceOrigin)
        + rounded * static_cast<long double>(resolution);
    const long double magnitude = std::max(
        {1.0L, std::abs(static_cast<long double>(currentOrigin)),
         std::abs(static_cast<long double>(referenceOrigin)),
         std::abs(reconstructed)});
    const long double tolerance = std::max(
        static_cast<long double>(std::abs(resolution)) * 1.0e-8L,
        magnitude * std::numeric_limits<double>::epsilon() * 32.0L);
    if (std::abs(static_cast<long double>(currentOrigin) - reconstructed)
        > tolerance)
    {
        return false;
    }
    offset = static_cast<qint64>(rounded);
    return true;
}

bool finiteNonNegative(double value)
{
    return std::isfinite(value) && value >= 0.0;
}

bool checkedDouble(long double value, double& output)
{
    if (!std::isfinite(value)
        || std::abs(value) > static_cast<long double>(
            std::numeric_limits<double>::max()))
    {
        return false;
    }
    output = static_cast<double>(value);
    return std::isfinite(output);
}

famp::cutfill::Result compareInternal(
    famp::terrain::Grid currentGrid,
    const famp::terrain::Grid* referenceGrid,
    qint64 columnOffset,
    qint64 rowOffset,
    const famp::cutfill::Options& options,
    const famp::tasks::CancellationCheck& shouldCancel,
    const famp::cutfill::Progress& reportProgress)
{
    famp::cutfill::Result result;
    result.referenceMode = options.referenceMode;
    result.constantReferenceElevation = options.referenceElevation;
    result.zeroTolerance = options.zeroTolerance;
    if (referenceGrid)
    {
        result.referenceLayerId = referenceGrid->sourceLayerId;
        result.referenceLayerName = referenceGrid->sourceLayerName;
        result.referenceCrs = referenceGrid->sourceCrs;
    }
    if (!currentGrid.isValid())
    {
        result.error = QStringLiteral("当前 DEM 网格无效。");
        return result;
    }
    const quint64 cellCount = static_cast<quint64>(
        currentGrid.elevations.size());
    if (cellCount > options.maximumCellCount)
    {
        result.error = QStringLiteral(
            "挖填方计算将处理 %1 个网格，超过 %2 个安全上限；请增大分辨率。")
                           .arg(cellCount)
                           .arg(options.maximumCellCount);
        return result;
    }

    result.currentGrid = std::move(currentGrid);
    result.differences.fill(
        std::numeric_limits<double>::quiet_NaN(),
        result.currentGrid.elevations.size());

    const long double cellSizeMetres =
        static_cast<long double>(result.currentGrid.resolution)
        * result.currentGrid.horizontalUnitToMetre;
    const long double cellArea = cellSizeMetres * cellSizeMetres;
    if (!checkedDouble(cellArea, result.cellAreaSquareMetres)
        || result.cellAreaSquareMetres <= 0.0)
    {
        result.error = QStringLiteral("网格实际面积超出可表示范围。");
        return result;
    }

    long double cutVolume = 0.0L;
    long double fillVolume = 0.0L;
    double minimumDifference = std::numeric_limits<double>::infinity();
    double maximumDifference = -std::numeric_limits<double>::infinity();
    const double unitToMetre = result.currentGrid.horizontalUnitToMetre;

    for (qsizetype index = 0;
         index < result.currentGrid.elevations.size(); ++index)
    {
        if ((index % CancellationInterval) == 0)
        {
            if (cancelled(shouldCancel))
            {
                result.cancelled = true;
                result.error = QStringLiteral("挖填方与体积计算已取消。");
                result.differences.clear();
                result.currentGrid = famp::terrain::Grid{};
                return result;
            }
            if (reportProgress)
            {
                reportProgress(static_cast<double>(index)
                               / std::max<qsizetype>(
                                   1, result.currentGrid.elevations.size()));
            }
        }

        const double current = result.currentGrid.elevations.at(index);
        if (!std::isfinite(current))
        {
            ++result.currentNoDataCellCount;
            continue;
        }
        ++result.currentValidCellCount;

        double reference = options.referenceElevation;
        if (referenceGrid)
        {
            const qint64 row = index / result.currentGrid.columns;
            const qint64 column = index % result.currentGrid.columns;
            const qint64 referenceRow = row + rowOffset;
            const qint64 referenceColumn = column + columnOffset;
            if (referenceRow < 0 || referenceRow >= referenceGrid->rows
                || referenceColumn < 0
                || referenceColumn >= referenceGrid->columns)
            {
                ++result.missingReferenceCellCount;
                continue;
            }
            reference = referenceGrid->value(
                static_cast<int>(referenceRow),
                static_cast<int>(referenceColumn));
            if (!std::isfinite(reference))
            {
                ++result.missingReferenceCellCount;
                continue;
            }
        }

        const double difference = current - reference;
        const double differenceMetres = difference * unitToMetre;
        if (!std::isfinite(difference)
            || !std::isfinite(differenceMetres))
        {
            result.error = QStringLiteral("网格高差超出可表示范围。");
            result.differences.clear();
            result.currentGrid = famp::terrain::Grid{};
            return result;
        }
        result.differences[index] = difference;
        ++result.comparedCellCount;
        minimumDifference = std::min(minimumDifference, differenceMetres);
        maximumDifference = std::max(maximumDifference, differenceMetres);

        if (difference > options.zeroTolerance)
        {
            ++result.cutCellCount;
            cutVolume += static_cast<long double>(differenceMetres) * cellArea;
        }
        else if (difference < -options.zeroTolerance)
        {
            ++result.fillCellCount;
            fillVolume += -static_cast<long double>(differenceMetres) * cellArea;
        }
        else
        {
            ++result.unchangedCellCount;
        }
    }

    if (result.comparedCellCount == 0)
    {
        result.error = referenceGrid
            ? QStringLiteral("当前 DEM 与参考 DEM 没有同时包含有效高程的重叠网格。")
            : QStringLiteral("当前 DEM 没有可用于体积计算的有效网格。");
        result.differences.clear();
        result.currentGrid = famp::terrain::Grid{};
        return result;
    }
    if (!checkedDouble(cutVolume, result.cutVolumeCubicMetres)
        || !checkedDouble(fillVolume, result.fillVolumeCubicMetres)
        || !checkedDouble(cutVolume - fillVolume,
                          result.signedVolumeCubicMetres))
    {
        result.error = QStringLiteral("挖填方体积超出可表示范围。");
        result.differences.clear();
        result.currentGrid = famp::terrain::Grid{};
        return result;
    }
    const long double cutArea = static_cast<long double>(
        result.cutCellCount) * cellArea;
    const long double fillArea = static_cast<long double>(
        result.fillCellCount) * cellArea;
    const long double unchangedArea = static_cast<long double>(
        result.unchangedCellCount) * cellArea;
    if (!checkedDouble(cutArea, result.cutAreaSquareMetres)
        || !checkedDouble(fillArea, result.fillAreaSquareMetres)
        || !checkedDouble(unchangedArea,
                          result.unchangedAreaSquareMetres))
    {
        result.error = QStringLiteral("挖填方面积超出可表示范围。");
        result.differences.clear();
        result.currentGrid = famp::terrain::Grid{};
        return result;
    }
    result.minimumDifferenceMetres = minimumDifference;
    result.maximumDifferenceMetres = maximumDifference;
    if (reportProgress)
        reportProgress(1.0);
    if (!result.isValid())
    {
        result.error = QStringLiteral("挖填方计算结果未通过一致性校验。");
        result.differences.clear();
        result.currentGrid = famp::terrain::Grid{};
        return result;
    }
    result.error.clear();
    return result;
}
}

namespace famp::cutfill
{
bool Result::isValid(
    const famp::tasks::CancellationCheck& shouldCancel) const
{
    if (!currentGrid.isValid()
        || differences.size() != currentGrid.elevations.size()
        || !std::isfinite(constantReferenceElevation)
        || !std::isfinite(zeroTolerance) || zeroTolerance < 0.0
        || comparedCellCount == 0
        || !finiteNonNegative(cellAreaSquareMetres)
        || cellAreaSquareMetres <= 0.0
        || !finiteNonNegative(cutAreaSquareMetres)
        || !finiteNonNegative(fillAreaSquareMetres)
        || !finiteNonNegative(unchangedAreaSquareMetres)
        || !finiteNonNegative(cutVolumeCubicMetres)
        || !finiteNonNegative(fillVolumeCubicMetres)
        || !std::isfinite(signedVolumeCubicMetres)
        || !std::isfinite(minimumDifferenceMetres)
        || !std::isfinite(maximumDifferenceMetres)
        || minimumDifferenceMetres > maximumDifferenceMetres)
    {
        return false;
    }
    if (referenceMode != ReferenceMode::ConstantElevation
        && referenceMode != ReferenceMode::DemGrid)
    {
        return false;
    }
    if (referenceMode == ReferenceMode::DemGrid
        && currentGrid.sourceCrs.trimmed().compare(
               referenceCrs.trimmed(), Qt::CaseInsensitive) != 0)
    {
        return false;
    }
    const long double expectedCellSizeMetres =
        static_cast<long double>(currentGrid.resolution)
        * currentGrid.horizontalUnitToMetre;
    double expectedCellArea = 0.0;
    if (!checkedDouble(expectedCellSizeMetres * expectedCellSizeMetres,
                       expectedCellArea)
        || expectedCellArea <= 0.0
        || !closeEnough(expectedCellArea, cellAreaSquareMetres, 1.0e-12))
    {
        return false;
    }

    quint64 derivedCurrentValid = 0;
    quint64 derivedCurrentNoData = 0;
    quint64 derivedCompared = 0;
    quint64 derivedMissingReference = 0;
    quint64 derivedCut = 0;
    quint64 derivedFill = 0;
    quint64 derivedUnchanged = 0;
    long double derivedCutVolume = 0.0L;
    long double derivedFillVolume = 0.0L;
    double derivedMinimum = std::numeric_limits<double>::infinity();
    double derivedMaximum = -std::numeric_limits<double>::infinity();
    const long double cellArea = cellAreaSquareMetres;
    const double unitToMetre = currentGrid.horizontalUnitToMetre;

    for (qsizetype index = 0; index < differences.size(); ++index)
    {
        if ((index % CancellationInterval) == 0
            && famp::tasks::isCancellationRequested(shouldCancel))
        {
            return false;
        }
        const double current = currentGrid.elevations.at(index);
        const double difference = differences.at(index);
        if (!std::isfinite(current))
        {
            ++derivedCurrentNoData;
            if (!std::isnan(difference))
                return false;
            continue;
        }
        ++derivedCurrentValid;
        if (!std::isfinite(difference))
        {
            if (!std::isnan(difference))
                return false;
            if (referenceMode == ReferenceMode::ConstantElevation)
                return false;
            ++derivedMissingReference;
            continue;
        }
        if (referenceMode == ReferenceMode::ConstantElevation
            && !closeEnough(current - difference,
                            constantReferenceElevation))
        {
            return false;
        }
        ++derivedCompared;
        const double differenceMetres = difference * unitToMetre;
        if (!std::isfinite(differenceMetres))
            return false;
        derivedMinimum = std::min(derivedMinimum, differenceMetres);
        derivedMaximum = std::max(derivedMaximum, differenceMetres);
        if (difference > zeroTolerance)
        {
            ++derivedCut;
            derivedCutVolume += static_cast<long double>(differenceMetres)
                * cellArea;
        }
        else if (difference < -zeroTolerance)
        {
            ++derivedFill;
            derivedFillVolume += -static_cast<long double>(differenceMetres)
                * cellArea;
        }
        else
        {
            ++derivedUnchanged;
        }
    }

    double checkedCutVolume = 0.0;
    double checkedFillVolume = 0.0;
    if (!checkedDouble(derivedCutVolume, checkedCutVolume)
        || !checkedDouble(derivedFillVolume, checkedFillVolume))
    {
        return false;
    }
    return derivedCurrentValid == currentValidCellCount
        && derivedCurrentNoData == currentNoDataCellCount
        && derivedCompared == comparedCellCount
        && derivedMissingReference == missingReferenceCellCount
        && derivedCut == cutCellCount
        && derivedFill == fillCellCount
        && derivedUnchanged == unchangedCellCount
        && derivedCurrentValid + derivedCurrentNoData
            == static_cast<quint64>(differences.size())
        && derivedCompared + derivedMissingReference == derivedCurrentValid
        && closeEnough(checkedCutVolume, cutVolumeCubicMetres)
        && closeEnough(checkedFillVolume, fillVolumeCubicMetres)
        && closeEnough(cutVolumeCubicMetres - fillVolumeCubicMetres,
                       signedVolumeCubicMetres)
        && closeEnough(cutCellCount * cellAreaSquareMetres,
                       cutAreaSquareMetres)
        && closeEnough(fillCellCount * cellAreaSquareMetres,
                       fillAreaSquareMetres)
        && closeEnough(unchangedCellCount * cellAreaSquareMetres,
                       unchangedAreaSquareMetres)
        && closeEnough(derivedMinimum, minimumDifferenceMetres)
        && closeEnough(derivedMaximum, maximumDifferenceMetres);
}

bool Result::succeeded() const
{
    return error.isEmpty() && !cancelled && isValid();
}

double Result::referenceElevationAt(qsizetype index) const
{
    if (index < 0 || index >= differences.size())
        return std::numeric_limits<double>::quiet_NaN();
    const double current = currentGrid.elevations.at(index);
    const double difference = differences.at(index);
    if (!std::isfinite(current) || !std::isfinite(difference))
        return std::numeric_limits<double>::quiet_NaN();
    return current - difference;
}

Classification Result::classificationAt(qsizetype index) const
{
    if (index < 0 || index >= differences.size())
        return Classification::NoData;
    const double difference = differences.at(index);
    if (!std::isfinite(currentGrid.elevations.at(index))
        || !std::isfinite(difference))
    {
        return Classification::NoData;
    }
    if (difference > zeroTolerance)
        return Classification::Cut;
    if (difference < -zeroTolerance)
        return Classification::Fill;
    return Classification::Unchanged;
}

QString referenceModeName(ReferenceMode mode)
{
    switch (mode)
    {
    case ReferenceMode::ConstantElevation:
        return QStringLiteral("固定基准高程");
    case ReferenceMode::DemGrid:
        return QStringLiteral("参考 DEM");
    }
    return QStringLiteral("未知");
}

QString classificationName(Classification classification)
{
    switch (classification)
    {
    case Classification::NoData:
        return QStringLiteral("NoData");
    case Classification::Unchanged:
        return QStringLiteral("平衡");
    case Classification::Cut:
        return QStringLiteral("挖方");
    case Classification::Fill:
        return QStringLiteral("填方");
    }
    return QStringLiteral("未知");
}

bool validateOptions(const Options& options, QString* errorMessage)
{
    if (options.referenceMode != ReferenceMode::ConstantElevation
        && options.referenceMode != ReferenceMode::DemGrid)
    {
        setError(errorMessage, QStringLiteral("挖填方参考模式无效。"));
        return false;
    }
    if (!std::isfinite(options.referenceElevation))
    {
        setError(errorMessage, QStringLiteral("基准高程无效。"));
        return false;
    }
    if (!std::isfinite(options.zeroTolerance)
        || options.zeroTolerance < 0.0)
    {
        setError(errorMessage, QStringLiteral("挖填方零值容差不能小于 0。"));
        return false;
    }
    if (options.maximumCellCount == 0
        || options.maximumCellCount > 100'000'000)
    {
        setError(errorMessage, QStringLiteral("挖填方网格数安全上限无效。"));
        return false;
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool validateAlignedReference(
    const famp::terrain::Grid& currentGrid,
    const famp::terrain::Grid& referenceGrid,
    qint64* columnOffset,
    qint64* rowOffset,
    QString* errorMessage)
{
    if (!currentGrid.isValid() || !referenceGrid.isValid())
    {
        setError(errorMessage, QStringLiteral("当前 DEM 或参考 DEM 无效。"));
        return false;
    }
    if (!closeEnough(currentGrid.horizontalUnitToMetre,
                     referenceGrid.horizontalUnitToMetre, 1.0e-12))
    {
        setError(errorMessage, QStringLiteral("当前 DEM 与参考 DEM 的坐标单位不一致。"));
        return false;
    }
    const QString currentCrs = currentGrid.sourceCrs.trimmed();
    const QString referenceCrs = referenceGrid.sourceCrs.trimmed();
    if (currentCrs.compare(referenceCrs, Qt::CaseInsensitive) != 0)
    {
        setError(errorMessage,
                 QStringLiteral("当前 DEM 与参考 DEM 的 CRS 不一致（%1 / %2）。")
                     .arg(currentCrs.isEmpty() ? QStringLiteral("未声明") : currentCrs,
                          referenceCrs.isEmpty() ? QStringLiteral("未声明") : referenceCrs));
        return false;
    }
    if (!closeEnough(currentGrid.resolution,
                     referenceGrid.resolution, 1.0e-9))
    {
        setError(errorMessage,
                 QStringLiteral("当前 DEM 与参考 DEM 的分辨率不一致。"));
        return false;
    }
    qint64 columns = 0;
    qint64 rows = 0;
    if (!alignedOffset(currentGrid.originX, referenceGrid.originX,
                       referenceGrid.resolution, columns)
        || !alignedOffset(currentGrid.originY, referenceGrid.originY,
                          referenceGrid.resolution, rows))
    {
        setError(errorMessage,
                 QStringLiteral("当前 DEM 与参考 DEM 的网格原点未对齐。"));
        return false;
    }
    const qint64 firstColumn = std::max<qint64>(0, -columns);
    const qint64 lastColumn = std::min<qint64>(
        currentGrid.columns,
        static_cast<qint64>(referenceGrid.columns) - columns);
    const qint64 firstRow = std::max<qint64>(0, -rows);
    const qint64 lastRow = std::min<qint64>(
        currentGrid.rows,
        static_cast<qint64>(referenceGrid.rows) - rows);
    if (firstColumn >= lastColumn || firstRow >= lastRow)
    {
        setError(errorMessage,
                 QStringLiteral("当前 DEM 与参考 DEM 的水平范围不重叠。"));
        return false;
    }
    if (columnOffset)
        *columnOffset = columns;
    if (rowOffset)
        *rowOffset = rows;
    if (errorMessage)
        errorMessage->clear();
    return true;
}

Result compareToConstant(
    famp::terrain::Grid currentGrid,
    const Options& options,
    const famp::tasks::CancellationCheck& shouldCancel,
    const Progress& reportProgress)
{
    Result result;
    if (!validateOptions(options, &result.error))
        return result;
    if (options.referenceMode != ReferenceMode::ConstantElevation)
    {
        result.error = QStringLiteral("固定高程计算收到错误的参考模式。");
        return result;
    }
    return compareInternal(
        std::move(currentGrid), nullptr, 0, 0,
        options, shouldCancel, reportProgress);
}

Result compareToGrid(
    famp::terrain::Grid currentGrid,
    const famp::terrain::Grid& referenceGrid,
    const Options& options,
    const famp::tasks::CancellationCheck& shouldCancel,
    const Progress& reportProgress)
{
    Result result;
    if (!validateOptions(options, &result.error))
        return result;
    if (options.referenceMode != ReferenceMode::DemGrid)
    {
        result.error = QStringLiteral("DEM 对比收到错误的参考模式。");
        return result;
    }
    qint64 columnOffset = 0;
    qint64 rowOffset = 0;
    if (!validateAlignedReference(
            currentGrid, referenceGrid, &columnOffset, &rowOffset,
            &result.error))
    {
        return result;
    }
    return compareInternal(
        std::move(currentGrid), &referenceGrid,
        columnOffset, rowOffset, options,
        shouldCancel, reportProgress);
}
}
