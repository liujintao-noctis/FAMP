#pragma once

#include "TaskCancellation.h"
#include "TerrainAnalysis.h"

#include <QString>
#include <QVector>

#include <functional>

namespace famp::cutfill
{
enum class ReferenceMode
{
    ConstantElevation,
    DemGrid
};

enum class Classification
{
    NoData,
    Unchanged,
    Cut,
    Fill
};

struct Options
{
    ReferenceMode referenceMode = ReferenceMode::ConstantElevation;
    double referenceElevation = 0.0;
    double zeroTolerance = 0.0;
    quint64 maximumCellCount = 10'000'000;
};

struct Result
{
    famp::terrain::Grid currentGrid;
    ReferenceMode referenceMode = ReferenceMode::ConstantElevation;
    double constantReferenceElevation = 0.0;
    double zeroTolerance = 0.0;
    QString referencePath;
    QString referenceLayerId;
    QString referenceLayerName;
    QString referenceCrs;
    QVector<double> differences;

    quint64 currentValidCellCount = 0;
    quint64 currentNoDataCellCount = 0;
    quint64 comparedCellCount = 0;
    quint64 missingReferenceCellCount = 0;
    quint64 cutCellCount = 0;
    quint64 fillCellCount = 0;
    quint64 unchangedCellCount = 0;

    double cellAreaSquareMetres = 0.0;
    double cutAreaSquareMetres = 0.0;
    double fillAreaSquareMetres = 0.0;
    double unchangedAreaSquareMetres = 0.0;
    double cutVolumeCubicMetres = 0.0;
    double fillVolumeCubicMetres = 0.0;
    double signedVolumeCubicMetres = 0.0;
    double minimumDifferenceMetres = 0.0;
    double maximumDifferenceMetres = 0.0;

    QString error;
    bool cancelled = false;

    bool isValid(
        const famp::tasks::CancellationCheck& shouldCancel = {}) const;
    bool succeeded() const;
    double referenceElevationAt(qsizetype index) const;
    Classification classificationAt(qsizetype index) const;
};

using Progress = std::function<void(double)>;

QString referenceModeName(ReferenceMode mode);
QString classificationName(Classification classification);
bool validateOptions(const Options& options,
                     QString* errorMessage = nullptr);
bool validateAlignedReference(
    const famp::terrain::Grid& currentGrid,
    const famp::terrain::Grid& referenceGrid,
    qint64* columnOffset = nullptr,
    qint64* rowOffset = nullptr,
    QString* errorMessage = nullptr);

Result compareToConstant(
    famp::terrain::Grid currentGrid,
    const Options& options,
    const famp::tasks::CancellationCheck& shouldCancel = {},
    const Progress& reportProgress = {});

Result compareToGrid(
    famp::terrain::Grid currentGrid,
    const famp::terrain::Grid& referenceGrid,
    const Options& options,
    const famp::tasks::CancellationCheck& shouldCancel = {},
    const Progress& reportProgress = {});
}
