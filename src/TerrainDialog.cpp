#include "TerrainDialog.h"

#include "TerrainIO.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace
{
void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}
}

namespace famp::terrainui
{
ExportPaths derivedExportPaths(const QString& sidecarPath)
{
    ExportPaths paths;
    paths.sidecar = famp::terrainio::pathWithDemSuffix(sidecarPath);
    if (paths.sidecar.isEmpty())
        return paths;
    const QFileInfo info(paths.sidecar);
    const QString base = info.completeBaseName();
    const QDir directory = info.absoluteDir();
    paths.asciiGrid = directory.filePath(base + QStringLiteral(".asc"));
    paths.gridCsv = directory.filePath(base + QStringLiteral("_dem.csv"));
    paths.contourCsv = directory.filePath(
        base + QStringLiteral("_contours.csv"));
    paths.contourSvg = directory.filePath(
        base + QStringLiteral("_contours.svg"));
    return paths;
}

bool validateOptions(const Options& options, QString* errorMessage)
{
    if (!famp::terrain::validateGridOptions(options.grid, errorMessage)
        || !famp::terrain::validateContourOptions(
            options.contours, errorMessage))
    {
        return false;
    }
    const ExportPaths paths = derivedExportPaths(options.sidecarPath);
    if (paths.sidecar.isEmpty())
    {
        setError(errorMessage, QStringLiteral("请选择 DEM 成果保存路径。"));
        return false;
    }
    if (!QFileInfo(paths.sidecar).absoluteDir().exists())
    {
        setError(errorMessage, QStringLiteral("DEM 成果保存目录不存在。"));
        return false;
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

TerrainDialog::TerrainDialog(const QString& layerName,
                             const QString& crsDescription,
                             const QString& horizontalUnitName,
                             double horizontalUnitToMetre,
                             const QString& initialSidecarPath,
                             QWidget* parent)
    : QDialog(parent)
    , horizontalUnitToMetre_(horizontalUnitToMetre)
{
    setWindowTitle(tr("DEM 与等高线"));
    setMinimumWidth(610);
    auto* root = new QVBoxLayout(this);

    auto* sourceGroup = new QGroupBox(tr("数据源"), this);
    auto* sourceLayout = new QFormLayout(sourceGroup);
    auto* layerLabel = new QLabel(layerName, sourceGroup);
    layerLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* crsLabel = new QLabel(crsDescription, sourceGroup);
    crsLabel->setWordWrap(true);
    crsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sourceLayout->addRow(tr("点云图层"), layerLabel);
    sourceLayout->addRow(tr("坐标参考"), crsLabel);
    sourceLayout->addRow(tr("水平单位"),
                         new QLabel(horizontalUnitName, sourceGroup));
    root->addWidget(sourceGroup);

    auto* gridGroup = new QGroupBox(tr("DEM 网格"), this);
    auto* gridLayout = new QFormLayout(gridGroup);
    automaticResolution_ = new QCheckBox(
        tr("自动：2 × 中位最近邻间距（不小于 0.01 米）"), gridGroup);
    automaticResolution_->setChecked(true);
    manualResolutionMetres_ = new QDoubleSpinBox(gridGroup);
    manualResolutionMetres_->setDecimals(6);
    manualResolutionMetres_->setRange(0.001, 1'000'000.0);
    manualResolutionMetres_->setValue(0.1);
    manualResolutionMetres_->setSuffix(tr(" 米"));
    manualResolutionMetres_->setEnabled(false);
    statistic_ = new QComboBox(gridGroup);
    statistic_->addItem(tr("中位数（推荐）"),
                        static_cast<int>(
                            famp::terrain::CellStatistic::Median));
    statistic_->addItem(tr("最低值"),
                        static_cast<int>(
                            famp::terrain::CellStatistic::Minimum));
    statistic_->addItem(tr("最高值"),
                        static_cast<int>(
                            famp::terrain::CellStatistic::Maximum));
    statistic_->addItem(tr("平均值"),
                        static_cast<int>(
                            famp::terrain::CellStatistic::Mean));
    fillSmallHoles_ = new QCheckBox(tr("填补小型封闭 NoData 空洞"), gridGroup);
    maximumHoleCells_ = new QSpinBox(gridGroup);
    maximumHoleCells_->setRange(1, 3);
    maximumHoleCells_->setValue(3);
    maximumHoleCells_->setSuffix(tr(" 格"));
    maximumHoleCells_->setEnabled(false);
    gridLayout->addRow(tr("分辨率"), automaticResolution_);
    gridLayout->addRow(tr("手动分辨率"), manualResolutionMetres_);
    gridLayout->addRow(tr("单元高程统计"), statistic_);
    gridLayout->addRow(fillSmallHoles_);
    gridLayout->addRow(tr("最大连通空洞"), maximumHoleCells_);
    root->addWidget(gridGroup);

    auto* contourGroup = new QGroupBox(tr("等高线"), this);
    auto* contourLayout = new QFormLayout(contourGroup);
    automaticInterval_ = new QCheckBox(
        tr("自动选择约 20 个高程层级的整洁等高距"), contourGroup);
    automaticInterval_->setChecked(true);
    manualInterval_ = new QDoubleSpinBox(contourGroup);
    manualInterval_->setDecimals(6);
    manualInterval_->setRange(0.000001, 1.0e9);
    manualInterval_->setValue(1.0);
    manualInterval_->setEnabled(false);
    automaticBase_ = new QCheckBox(tr("自动对齐最低高程"), contourGroup);
    automaticBase_->setChecked(true);
    manualBase_ = new QDoubleSpinBox(contourGroup);
    manualBase_->setDecimals(6);
    manualBase_->setRange(-1.0e12, 1.0e12);
    manualBase_->setValue(0.0);
    manualBase_->setEnabled(false);
    smoothingIterations_ = new QSpinBox(contourGroup);
    smoothingIterations_->setRange(0, 3);
    smoothingIterations_->setValue(1);
    smoothingIterations_->setToolTip(
        tr("0 保留原始折线，1–3 使用 Chaikin 平滑。"));
    contourLayout->addRow(tr("等高距"), automaticInterval_);
    contourLayout->addRow(tr("手动等高距（高程单位）"), manualInterval_);
    contourLayout->addRow(tr("基准高程"), automaticBase_);
    contourLayout->addRow(tr("手动基准高程"), manualBase_);
    contourLayout->addRow(tr("平滑次数"), smoothingIterations_);
    root->addWidget(contourGroup);

    auto* outputGroup = new QGroupBox(tr("成果"), this);
    auto* outputLayout = new QFormLayout(outputGroup);
    auto* pathRow = new QWidget(outputGroup);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    sidecarPath_ = new QLineEdit(initialSidecarPath, pathRow);
    auto* browse = new QPushButton(tr("浏览…"), pathRow);
    pathLayout->addWidget(sidecarPath_, 1);
    pathLayout->addWidget(browse);
    outputLayout->addRow(tr("DEM 项目边车（必选）"), pathRow);
    exportAsciiGrid_ = new QCheckBox(tr("同时导出 ESRI ASCII Grid (.asc)"), outputGroup);
    exportGridCsv_ = new QCheckBox(tr("同时导出 DEM CSV"), outputGroup);
    exportContourCsv_ = new QCheckBox(tr("同时导出等高线 CSV"), outputGroup);
    exportContourSvg_ = new QCheckBox(tr("同时导出等高线 SVG"), outputGroup);
    addToCanvas_ = new QCheckBox(tr("把等高线加入二维制图画布"), outputGroup);
    addToCanvas_->setChecked(true);
    outputLayout->addRow(exportAsciiGrid_);
    outputLayout->addRow(exportGridCsv_);
    outputLayout->addRow(exportContourCsv_);
    outputLayout->addRow(exportContourSvg_);
    outputLayout->addRow(addToCanvas_);
    derivedPathSummary_ = new QLabel(outputGroup);
    derivedPathSummary_->setWordWrap(true);
    derivedPathSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outputLayout->addRow(tr("派生文件"), derivedPathSummary_);
    root->addWidget(outputGroup);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("开始生成"));
    root->addWidget(buttons);

    connect(automaticResolution_, &QCheckBox::toggled,
            manualResolutionMetres_, &QWidget::setDisabled);
    connect(fillSmallHoles_, &QCheckBox::toggled,
            maximumHoleCells_, &QWidget::setEnabled);
    connect(automaticInterval_, &QCheckBox::toggled,
            manualInterval_, &QWidget::setDisabled);
    connect(automaticBase_, &QCheckBox::toggled,
            manualBase_, &QWidget::setDisabled);
    connect(sidecarPath_, &QLineEdit::textChanged,
            this, [this]() { updateDerivedPathSummary(); });
    connect(browse, &QPushButton::clicked, this, [this]() {
        QString selected = QFileDialog::getSaveFileName(
            this, tr("保存 DEM 项目边车"), sidecarPath_->text(),
            tr("FAMP DEM (*.famp-dem)"));
        if (!selected.isEmpty())
            sidecarPath_->setText(famp::terrainio::pathWithDemSuffix(selected));
    });
    connect(buttons, &QDialogButtonBox::accepted,
            this, &TerrainDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    updateDerivedPathSummary();
}

Options TerrainDialog::options() const
{
    Options result;
    result.grid.automaticResolution = automaticResolution_->isChecked();
    result.grid.resolution = manualResolutionMetres_->value()
        / horizontalUnitToMetre_;
    result.grid.horizontalUnitToMetre = horizontalUnitToMetre_;
    result.grid.statistic = static_cast<famp::terrain::CellStatistic>(
        statistic_->currentData().toInt());
    result.grid.fillSmallHoles = fillSmallHoles_->isChecked();
    result.grid.maximumHoleCells = maximumHoleCells_->value();
    result.contours.automaticInterval = automaticInterval_->isChecked();
    result.contours.interval = manualInterval_->value();
    result.contours.automaticBase = automaticBase_->isChecked();
    result.contours.baseElevation = manualBase_->value();
    result.contours.smoothingIterations = smoothingIterations_->value();
    result.sidecarPath = famp::terrainio::pathWithDemSuffix(
        sidecarPath_->text());
    result.exportAsciiGrid = exportAsciiGrid_->isChecked();
    result.exportGridCsv = exportGridCsv_->isChecked();
    result.exportContourCsv = exportContourCsv_->isChecked();
    result.exportContourSvg = exportContourSvg_->isChecked();
    result.addToCanvas = addToCanvas_->isChecked();
    return result;
}

void TerrainDialog::accept()
{
    QString error;
    if (!validateOptions(options(), &error))
    {
        QMessageBox::warning(this, tr("地形参数无效"), error);
        return;
    }
    QDialog::accept();
}

void TerrainDialog::updateDerivedPathSummary()
{
    const ExportPaths paths = derivedExportPaths(sidecarPath_->text());
    if (paths.sidecar.isEmpty())
    {
        derivedPathSummary_->setText(tr("选择边车路径后显示。"));
        return;
    }
    derivedPathSummary_->setText(
        tr("ASC: %1\nDEM CSV: %2\n等高线 CSV: %3\n等高线 SVG: %4")
            .arg(paths.asciiGrid, paths.gridCsv,
                 paths.contourCsv, paths.contourSvg));
}
}
