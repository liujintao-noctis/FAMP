#pragma once

#include "CutFillAnalysis.h"
#include "TerrainAnalysis.h"

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace famp::cutfillui
{
struct ExportPaths
{
    QString sidecar;
    QString summaryCsv;
    QString cellsCsv;
    QString svg;
};

struct Options
{
    famp::terrain::GridOptions grid;
    famp::cutfill::Options analysis;
    QString referenceDemPath;
    QString sidecarPath;
    bool exportSummaryCsv = true;
    bool exportCellsCsv = false;
    bool exportSvg = true;
};

ExportPaths derivedExportPaths(const QString& sidecarPath);
bool validateOptions(const Options& options,
                     QString* errorMessage = nullptr);

// Applies the reference DEM resolution only after its metadata has been
// checked against the selected point cloud. The update is atomic on failure.
bool applyReferenceGrid(const famp::terrain::Grid& referenceGrid,
                        const QString& sourceCrs,
                        double sourceUnitToMetre,
                        Options& options,
                        QString* errorMessage = nullptr);

class CutFillDialog final : public QDialog
{
public:
    CutFillDialog(const QString& layerName,
                  const QString& crsDescription,
                  const QString& horizontalUnitName,
                  double horizontalUnitToMetre,
                  const QString& initialSidecarPath,
                  QWidget* parent = nullptr);

    Options options() const;

protected:
    void accept() override;

private:
    void updateModeControls();
    void updateDerivedPathSummary();

    double horizontalUnitToMetre_ = 1.0;
    QComboBox* referenceMode_ = nullptr;
    QDoubleSpinBox* fixedElevationMetres_ = nullptr;
    QLineEdit* referenceDemPath_ = nullptr;
    QPushButton* referenceBrowse_ = nullptr;
    QCheckBox* automaticResolution_ = nullptr;
    QDoubleSpinBox* manualResolutionMetres_ = nullptr;
    QComboBox* statistic_ = nullptr;
    QCheckBox* fillSmallHoles_ = nullptr;
    QSpinBox* maximumHoleCells_ = nullptr;
    QDoubleSpinBox* zeroToleranceMetres_ = nullptr;
    QLineEdit* sidecarPath_ = nullptr;
    QCheckBox* exportSummaryCsv_ = nullptr;
    QCheckBox* exportCellsCsv_ = nullptr;
    QCheckBox* exportSvg_ = nullptr;
    QLabel* derivedPathSummary_ = nullptr;
};

class CutFillResultDialog final : public QDialog
{
public:
    explicit CutFillResultDialog(const famp::cutfill::Result& result,
                                 const QStringList& savedPaths,
                                 QWidget* parent = nullptr);
};
}
