#pragma once

#include "TerrainAnalysis.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QSpinBox;

namespace famp::terrainui
{
struct ExportPaths
{
    QString sidecar;
    QString asciiGrid;
    QString gridCsv;
    QString contourCsv;
    QString contourSvg;
};

struct Options
{
    famp::terrain::GridOptions grid;
    famp::terrain::ContourOptions contours;
    QString sidecarPath;
    bool exportAsciiGrid = false;
    bool exportGridCsv = false;
    bool exportContourCsv = false;
    bool exportContourSvg = false;
    bool addToCanvas = true;
};

ExportPaths derivedExportPaths(const QString& sidecarPath);
bool validateOptions(const Options& options,
                     QString* errorMessage = nullptr);

class TerrainDialog final : public QDialog
{
public:
    TerrainDialog(const QString& layerName,
                  const QString& crsDescription,
                  const QString& horizontalUnitName,
                  double horizontalUnitToMetre,
                  const QString& initialSidecarPath,
                  QWidget* parent = nullptr);

    Options options() const;

protected:
    void accept() override;

private:
    void updateDerivedPathSummary();

    double horizontalUnitToMetre_ = 1.0;
    QCheckBox* automaticResolution_ = nullptr;
    QDoubleSpinBox* manualResolutionMetres_ = nullptr;
    QComboBox* statistic_ = nullptr;
    QCheckBox* fillSmallHoles_ = nullptr;
    QSpinBox* maximumHoleCells_ = nullptr;
    QCheckBox* automaticInterval_ = nullptr;
    QDoubleSpinBox* manualInterval_ = nullptr;
    QCheckBox* automaticBase_ = nullptr;
    QDoubleSpinBox* manualBase_ = nullptr;
    QSpinBox* smoothingIterations_ = nullptr;
    QCheckBox* saveImmediately_ = nullptr;
    QLineEdit* sidecarPath_ = nullptr;
    QCheckBox* exportAsciiGrid_ = nullptr;
    QCheckBox* exportGridCsv_ = nullptr;
    QCheckBox* exportContourCsv_ = nullptr;
    QCheckBox* exportContourSvg_ = nullptr;
    QCheckBox* addToCanvas_ = nullptr;
    QLabel* derivedPathSummary_ = nullptr;
};
}
