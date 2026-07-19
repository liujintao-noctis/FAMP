#pragma once

#include "ProfileAnalysis.h"

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace famp::profileui
{
struct ExportPaths
{
    QString sidecar;
    QString binsCsv;
    QString samplesCsv;
    QString svg;
};

struct Options
{
    famp::profile::Options analysis;
    QString sidecarPath;
    bool exportBinsCsv = true;
    bool exportSamplesCsv = false;
    bool exportSvg = true;
};

ExportPaths derivedExportPaths(const QString& sidecarPath);
bool validateOptions(const Options& options,
                     QString* errorMessage = nullptr);

class ProfileDialog final : public QDialog
{
public:
    ProfileDialog(const QString& layerName,
                  const QString& crsDescription,
                  const QString& horizontalUnitName,
                  double horizontalUnitToMetre,
                  const famp::profile::Baseline& baseline,
                  const QString& initialSidecarPath,
                  QWidget* parent = nullptr);

    Options options() const;

protected:
    void accept() override;

private:
    void updateDerivedPathSummary();

    double horizontalUnitToMetre_ = 1.0;
    QDoubleSpinBox* corridorWidthMetres_ = nullptr;
    QDoubleSpinBox* binSizeMetres_ = nullptr;
    QComboBox* statistic_ = nullptr;
    QSpinBox* minimumPointsPerBin_ = nullptr;
    QCheckBox* saveImmediately_ = nullptr;
    QLineEdit* sidecarPath_ = nullptr;
    QCheckBox* exportBinsCsv_ = nullptr;
    QCheckBox* exportSamplesCsv_ = nullptr;
    QCheckBox* exportSvg_ = nullptr;
    QLabel* derivedPathSummary_ = nullptr;
};

class ProfileResultDialog final : public QDialog
{
public:
    explicit ProfileResultDialog(const famp::profile::Result& result,
                                 const QStringList& savedPaths,
                                 QWidget* parent = nullptr);
};
}
