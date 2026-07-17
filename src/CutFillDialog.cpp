#include "CutFillDialog.h"

#include "CutFillIO.h"

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
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr quint64 MaximumOverviewPixels = 240'000;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool closeEnough(double first, double second)
{
    if (!std::isfinite(first) || !std::isfinite(second))
        return false;
    const double scale = std::max({1.0, std::abs(first), std::abs(second)});
    return std::abs(first - second) <= scale * 1.0e-12;
}

QString formatMetric(double value)
{
    return QString::number(value, 'g', 12);
}

class CutFillOverviewWidget final : public QWidget
{
public:
    explicit CutFillOverviewWidget(const famp::cutfill::Result& result,
                                   QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("cutFillOverview"));
        setAccessibleName(tr("挖填方分类概览图"));
        setMinimumSize(620, 320);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        const auto& grid = result.currentGrid;
        const quint64 sourceCells = static_cast<quint64>(grid.columns)
            * static_cast<quint64>(grid.rows);
        double scale = 1.0;
        if (sourceCells > MaximumOverviewPixels)
        {
            scale = std::sqrt(
                static_cast<double>(MaximumOverviewPixels)
                / static_cast<double>(sourceCells));
        }
        int imageColumns = std::max(
            1, static_cast<int>(std::floor(grid.columns * scale)));
        int imageRows = std::max(
            1, static_cast<int>(std::floor(grid.rows * scale)));
        while (static_cast<quint64>(imageColumns)
                   * static_cast<quint64>(imageRows)
               > MaximumOverviewPixels)
        {
            if (imageColumns >= imageRows && imageColumns > 1)
                --imageColumns;
            else if (imageRows > 1)
                --imageRows;
            else
                break;
        }

        image_ = QImage(imageColumns, imageRows, QImage::Format_ARGB32);
        image_.fill(QColor(245, 245, 245));
        for (int imageRow = 0; imageRow < imageRows; ++imageRow)
        {
            const int sourceFromTop = std::min(
                grid.rows - 1,
                static_cast<int>((static_cast<qint64>(imageRow) * 2 + 1)
                                 * grid.rows
                                 / (static_cast<qint64>(imageRows) * 2)));
            const int sourceRow = grid.rows - 1 - sourceFromTop;
            QRgb* pixels = reinterpret_cast<QRgb*>(
                image_.scanLine(imageRow));
            for (int imageColumn = 0;
                 imageColumn < imageColumns; ++imageColumn)
            {
                const int sourceColumn = std::min(
                    grid.columns - 1,
                    static_cast<int>(
                        (static_cast<qint64>(imageColumn) * 2 + 1)
                        * grid.columns
                        / (static_cast<qint64>(imageColumns) * 2)));
                QColor color(245, 245, 245);
                switch (result.classificationAt(
                    grid.index(sourceRow, sourceColumn)))
                {
                case famp::cutfill::Classification::Cut:
                    color = QColor(220, 100, 100);
                    break;
                case famp::cutfill::Classification::Fill:
                    color = QColor(100, 130, 220);
                    break;
                case famp::cutfill::Classification::Unchanged:
                    color = QColor(156, 163, 175);
                    break;
                case famp::cutfill::Classification::NoData:
                    break;
                }
                pixels[imageColumn] = color.rgba();
            }
        }
        setProperty("sourceCellCount",
                    QVariant::fromValue<qulonglong>(sourceCells));
        setProperty(
            "renderedBlockCount",
            QVariant::fromValue<qulonglong>(
                static_cast<quint64>(imageColumns) * imageRows));
    }

    QSize sizeHint() const override
    {
        return {900, 430};
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.fillRect(rect(), Qt::white);
        QRect target = rect().adjusted(18, 18, -18, -58);
        if (target.width() <= 0 || target.height() <= 0 || image_.isNull())
            return;
        const QSize fitted = image_.size().scaled(
            target.size(), Qt::KeepAspectRatio);
        target = QRect(
            target.center().x() - fitted.width() / 2,
            target.center().y() - fitted.height() / 2,
            fitted.width(), fitted.height());
        painter.drawImage(target, image_);
        painter.setPen(QPen(QColor(90, 90, 90), 1.0));
        painter.drawRect(target.adjusted(0, 0, -1, -1));

        const int legendY = height() - 32;
        struct LegendItem
        {
            QColor color;
            QString text;
        };
        const LegendItem items[]{
            {QColor(220, 100, 100), tr("挖方")},
            {QColor(100, 130, 220), tr("填方")},
            {QColor(156, 163, 175), tr("容差内平衡")},
            {QColor(245, 245, 245), tr("NoData")}};
        int x = 18;
        for (const LegendItem& item : items)
        {
            painter.fillRect(QRect(x, legendY, 16, 16), item.color);
            painter.setPen(QColor(80, 80, 80));
            painter.drawRect(QRect(x, legendY, 16, 16));
            painter.drawText(x + 23, legendY + 14, item.text);
            x += 31 + painter.fontMetrics().horizontalAdvance(item.text);
        }
    }

private:
    QImage image_;
};
}

namespace famp::cutfillui
{
ExportPaths derivedExportPaths(const QString& sidecarPath)
{
    ExportPaths paths;
    paths.sidecar = famp::cutfillio::pathWithVolumeSuffix(sidecarPath);
    if (paths.sidecar.isEmpty())
        return paths;
    const QFileInfo info(paths.sidecar);
    const QString base = info.completeBaseName();
    const QDir directory = info.absoluteDir();
    paths.summaryCsv = directory.filePath(
        base + QStringLiteral("_summary.csv"));
    paths.cellsCsv = directory.filePath(
        base + QStringLiteral("_cells.csv"));
    paths.svg = directory.filePath(base + QStringLiteral(".svg"));
    return paths;
}

bool validateOptions(const Options& options, QString* errorMessage)
{
    if (!famp::terrain::validateGridOptions(options.grid, errorMessage)
        || !famp::cutfill::validateOptions(
            options.analysis, errorMessage))
    {
        return false;
    }
    if (options.grid.maximumCellCount > options.analysis.maximumCellCount)
    {
        setError(errorMessage,
                 QStringLiteral("当前 DEM 网格上限不能大于挖填方计算上限。"));
        return false;
    }
    if (options.analysis.referenceMode
        == famp::cutfill::ReferenceMode::DemGrid)
    {
        const QFileInfo reference(options.referenceDemPath.trimmed());
        if (options.referenceDemPath.trimmed().isEmpty())
        {
            setError(errorMessage, QStringLiteral("请选择参考 DEM 文件。"));
            return false;
        }
        if (!reference.exists() || !reference.isFile())
        {
            setError(errorMessage,
                     QStringLiteral("参考 DEM 文件不存在或不是普通文件。"));
            return false;
        }
        if (reference.suffix().compare(
                QStringLiteral("famp-dem"), Qt::CaseInsensitive) != 0)
        {
            setError(errorMessage,
                     QStringLiteral("参考文件必须是 .famp-dem 成果。"));
            return false;
        }
    }
    const ExportPaths paths = derivedExportPaths(options.sidecarPath);
    if (paths.sidecar.isEmpty())
    {
        setError(errorMessage, QStringLiteral("请选择挖填方成果保存路径。"));
        return false;
    }
    if (!QFileInfo(paths.sidecar).absoluteDir().exists())
    {
        setError(errorMessage, QStringLiteral("挖填方成果保存目录不存在。"));
        return false;
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool applyReferenceGrid(const famp::terrain::Grid& referenceGrid,
                        const QString& sourceCrs,
                        double sourceUnitToMetre,
                        Options& options,
                        QString* errorMessage)
{
    if (options.analysis.referenceMode
        != famp::cutfill::ReferenceMode::DemGrid)
    {
        setError(errorMessage,
                 QStringLiteral("当前选项不是参考 DEM 对比模式。"));
        return false;
    }
    if (!referenceGrid.isValid())
    {
        setError(errorMessage, QStringLiteral("参考 DEM 网格无效。"));
        return false;
    }
    if (!std::isfinite(sourceUnitToMetre) || sourceUnitToMetre <= 0.0)
    {
        setError(errorMessage, QStringLiteral("当前点云的坐标单位无效。"));
        return false;
    }
    const QString currentCrs = sourceCrs.trimmed();
    const QString referenceCrs = referenceGrid.sourceCrs.trimmed();
    if (currentCrs.compare(referenceCrs, Qt::CaseInsensitive) != 0)
    {
        setError(errorMessage,
                 QStringLiteral("当前点云与参考 DEM 的 CRS 不一致（%1 / %2）。")
                     .arg(currentCrs.isEmpty()
                              ? QStringLiteral("未声明") : currentCrs,
                          referenceCrs.isEmpty()
                              ? QStringLiteral("未声明") : referenceCrs));
        return false;
    }
    if (!closeEnough(sourceUnitToMetre,
                     referenceGrid.horizontalUnitToMetre))
    {
        setError(errorMessage,
                 QStringLiteral("当前点云与参考 DEM 的坐标单位不一致。"));
        return false;
    }

    Options candidate = options;
    candidate.grid.automaticResolution = false;
    candidate.grid.resolution = referenceGrid.resolution;
    candidate.grid.horizontalUnitToMetre = sourceUnitToMetre;
    candidate.grid.maximumCellCount = std::min(
        candidate.grid.maximumCellCount,
        candidate.analysis.maximumCellCount);
    if (!famp::terrain::validateGridOptions(
            candidate.grid, errorMessage))
    {
        return false;
    }
    options = candidate;
    if (errorMessage)
        errorMessage->clear();
    return true;
}

CutFillDialog::CutFillDialog(const QString& layerName,
                             const QString& crsDescription,
                             const QString& horizontalUnitName,
                             double horizontalUnitToMetre,
                             const QString& initialSidecarPath,
                             QWidget* parent)
    : QDialog(parent)
    , horizontalUnitToMetre_(horizontalUnitToMetre)
{
    setWindowTitle(tr("挖填方与体积"));
    setMinimumWidth(660);
    auto* root = new QVBoxLayout(this);

    auto* sourceGroup = new QGroupBox(tr("当前地表"), this);
    auto* sourceLayout = new QFormLayout(sourceGroup);
    auto* layerLabel = new QLabel(layerName, sourceGroup);
    layerLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* crsLabel = new QLabel(crsDescription, sourceGroup);
    crsLabel->setWordWrap(true);
    crsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sourceLayout->addRow(tr("点云图层"), layerLabel);
    sourceLayout->addRow(tr("坐标参考"), crsLabel);
    sourceLayout->addRow(tr("X/Y/Z 单位"),
                         new QLabel(horizontalUnitName, sourceGroup));
    root->addWidget(sourceGroup);

    auto* referenceGroup = new QGroupBox(tr("参考地表"), this);
    auto* referenceLayout = new QFormLayout(referenceGroup);
    referenceMode_ = new QComboBox(referenceGroup);
    referenceMode_->setObjectName(QStringLiteral("cutFillReferenceMode"));
    referenceMode_->addItem(
        tr("固定设计高程"),
        static_cast<int>(famp::cutfill::ReferenceMode::ConstantElevation));
    referenceMode_->addItem(
        tr("已有 FAMP DEM"),
        static_cast<int>(famp::cutfill::ReferenceMode::DemGrid));
    fixedElevationMetres_ = new QDoubleSpinBox(referenceGroup);
    fixedElevationMetres_->setObjectName(
        QStringLiteral("cutFillReferenceElevationMetres"));
    fixedElevationMetres_->setDecimals(6);
    fixedElevationMetres_->setRange(-1.0e9, 1.0e9);
    fixedElevationMetres_->setSuffix(tr(" 米"));

    auto* referencePathRow = new QWidget(referenceGroup);
    auto* referencePathLayout = new QHBoxLayout(referencePathRow);
    referencePathLayout->setContentsMargins(0, 0, 0, 0);
    referenceDemPath_ = new QLineEdit(referencePathRow);
    referenceDemPath_->setObjectName(
        QStringLiteral("cutFillReferenceDemPath"));
    referenceBrowse_ = new QPushButton(tr("浏览…"), referencePathRow);
    referencePathLayout->addWidget(referenceDemPath_, 1);
    referencePathLayout->addWidget(referenceBrowse_);

    zeroToleranceMetres_ = new QDoubleSpinBox(referenceGroup);
    zeroToleranceMetres_->setObjectName(
        QStringLiteral("cutFillZeroToleranceMetres"));
    zeroToleranceMetres_->setDecimals(6);
    zeroToleranceMetres_->setRange(0.0, 1.0e6);
    zeroToleranceMetres_->setValue(0.01);
    zeroToleranceMetres_->setSuffix(tr(" 米"));
    zeroToleranceMetres_->setToolTip(
        tr("高差绝对值不大于此值的网格按平衡区计算。"));
    auto* convention = new QLabel(
        tr("高差 = 当前地表 - 参考地表；正值为挖方，负值为填方。"),
        referenceGroup);
    convention->setWordWrap(true);
    referenceLayout->addRow(tr("参考方式"), referenceMode_);
    referenceLayout->addRow(tr("设计高程"), fixedElevationMetres_);
    referenceLayout->addRow(tr("参考 DEM"), referencePathRow);
    referenceLayout->addRow(tr("平衡容差"), zeroToleranceMetres_);
    referenceLayout->addRow(convention);
    root->addWidget(referenceGroup);

    auto* gridGroup = new QGroupBox(tr("当前地表 DEM 网格"), this);
    auto* gridLayout = new QFormLayout(gridGroup);
    automaticResolution_ = new QCheckBox(
        tr("自动：2 × 中位最近邻间距（不小于 0.01 米）"),
        gridGroup);
    automaticResolution_->setObjectName(
        QStringLiteral("cutFillAutomaticResolution"));
    automaticResolution_->setChecked(true);
    manualResolutionMetres_ = new QDoubleSpinBox(gridGroup);
    manualResolutionMetres_->setObjectName(
        QStringLiteral("cutFillResolutionMetres"));
    manualResolutionMetres_->setDecimals(6);
    manualResolutionMetres_->setRange(0.001, 1'000'000.0);
    manualResolutionMetres_->setValue(0.1);
    manualResolutionMetres_->setSuffix(tr(" 米"));
    statistic_ = new QComboBox(gridGroup);
    statistic_->setObjectName(QStringLiteral("cutFillGridStatistic"));
    statistic_->addItem(
        tr("中位数（推荐）"),
        static_cast<int>(famp::terrain::CellStatistic::Median));
    statistic_->addItem(
        tr("最低值"),
        static_cast<int>(famp::terrain::CellStatistic::Minimum));
    statistic_->addItem(
        tr("最高值"),
        static_cast<int>(famp::terrain::CellStatistic::Maximum));
    statistic_->addItem(
        tr("平均值"),
        static_cast<int>(famp::terrain::CellStatistic::Mean));
    fillSmallHoles_ = new QCheckBox(
        tr("填补小型封闭 NoData 空洞"), gridGroup);
    maximumHoleCells_ = new QSpinBox(gridGroup);
    maximumHoleCells_->setRange(1, 3);
    maximumHoleCells_->setValue(3);
    maximumHoleCells_->setSuffix(tr(" 格"));
    gridLayout->addRow(tr("分辨率"), automaticResolution_);
    gridLayout->addRow(tr("手动分辨率"), manualResolutionMetres_);
    gridLayout->addRow(tr("单元高程统计"), statistic_);
    gridLayout->addRow(fillSmallHoles_);
    gridLayout->addRow(tr("最大连通空洞"), maximumHoleCells_);
    auto* referenceResolutionNote = new QLabel(
        tr("选择参考 DEM 时，将在后台读取并强制采用它的网格分辨率。"),
        gridGroup);
    referenceResolutionNote->setWordWrap(true);
    gridLayout->addRow(referenceResolutionNote);
    root->addWidget(gridGroup);

    auto* outputGroup = new QGroupBox(tr("成果"), this);
    auto* outputLayout = new QFormLayout(outputGroup);
    auto* pathRow = new QWidget(outputGroup);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    sidecarPath_ = new QLineEdit(initialSidecarPath, pathRow);
    sidecarPath_->setObjectName(QStringLiteral("cutFillSidecarPath"));
    auto* browse = new QPushButton(tr("浏览…"), pathRow);
    pathLayout->addWidget(sidecarPath_, 1);
    pathLayout->addWidget(browse);
    outputLayout->addRow(tr("挖填方边车（必选）"), pathRow);
    exportSummaryCsv_ = new QCheckBox(
        tr("同时导出汇总 CSV"), outputGroup);
    exportSummaryCsv_->setChecked(true);
    exportCellsCsv_ = new QCheckBox(
        tr("同时导出逐格 CSV（大网格可能较大）"), outputGroup);
    exportSvg_ = new QCheckBox(
        tr("同时导出挖填方概览 SVG"), outputGroup);
    exportSvg_->setChecked(true);
    outputLayout->addRow(exportSummaryCsv_);
    outputLayout->addRow(exportCellsCsv_);
    outputLayout->addRow(exportSvg_);
    derivedPathSummary_ = new QLabel(outputGroup);
    derivedPathSummary_->setWordWrap(true);
    derivedPathSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outputLayout->addRow(tr("派生文件"), derivedPathSummary_);
    root->addWidget(outputGroup);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("开始计算"));
    root->addWidget(buttons);

    connect(referenceMode_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]() { updateModeControls(); });
    connect(automaticResolution_, &QCheckBox::toggled,
            this, [this]() { updateModeControls(); });
    connect(fillSmallHoles_, &QCheckBox::toggled,
            maximumHoleCells_, &QWidget::setEnabled);
    connect(sidecarPath_, &QLineEdit::textChanged,
            this, [this]() { updateDerivedPathSummary(); });
    connect(referenceBrowse_, &QPushButton::clicked, this, [this]() {
        const QString selected = QFileDialog::getOpenFileName(
            this, tr("选择参考 DEM"), referenceDemPath_->text(),
            tr("FAMP DEM (*.famp-dem)"));
        if (!selected.isEmpty())
            referenceDemPath_->setText(selected);
    });
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString selected = QFileDialog::getSaveFileName(
            this, tr("保存挖填方成果"), sidecarPath_->text(),
            tr("FAMP 挖填方成果 (*.famp-volume)"));
        if (!selected.isEmpty())
        {
            sidecarPath_->setText(
                famp::cutfillio::pathWithVolumeSuffix(selected));
        }
    });
    connect(buttons, &QDialogButtonBox::accepted,
            this, &CutFillDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    maximumHoleCells_->setEnabled(false);
    updateModeControls();
    updateDerivedPathSummary();
}

Options CutFillDialog::options() const
{
    Options result;
    result.analysis.referenceMode =
        static_cast<famp::cutfill::ReferenceMode>(
            referenceMode_->currentData().toInt());
    result.analysis.referenceElevation = fixedElevationMetres_->value()
        / horizontalUnitToMetre_;
    result.analysis.zeroTolerance = zeroToleranceMetres_->value()
        / horizontalUnitToMetre_;
    result.grid.automaticResolution =
        result.analysis.referenceMode
            == famp::cutfill::ReferenceMode::ConstantElevation
        && automaticResolution_->isChecked();
    result.grid.resolution = manualResolutionMetres_->value()
        / horizontalUnitToMetre_;
    result.grid.horizontalUnitToMetre = horizontalUnitToMetre_;
    result.grid.statistic = static_cast<famp::terrain::CellStatistic>(
        statistic_->currentData().toInt());
    result.grid.fillSmallHoles = fillSmallHoles_->isChecked();
    result.grid.maximumHoleCells = maximumHoleCells_->value();
    result.grid.maximumCellCount = result.analysis.maximumCellCount;
    result.referenceDemPath = referenceDemPath_->text().trimmed();
    result.sidecarPath = famp::cutfillio::pathWithVolumeSuffix(
        sidecarPath_->text());
    result.exportSummaryCsv = exportSummaryCsv_->isChecked();
    result.exportCellsCsv = exportCellsCsv_->isChecked();
    result.exportSvg = exportSvg_->isChecked();
    return result;
}

void CutFillDialog::accept()
{
    QString error;
    if (!validateOptions(options(), &error))
    {
        QMessageBox::warning(this, tr("挖填方参数无效"), error);
        return;
    }
    QDialog::accept();
}

void CutFillDialog::updateModeControls()
{
    const bool constant = static_cast<famp::cutfill::ReferenceMode>(
        referenceMode_->currentData().toInt())
        == famp::cutfill::ReferenceMode::ConstantElevation;
    fixedElevationMetres_->setEnabled(constant);
    referenceDemPath_->setEnabled(!constant);
    referenceBrowse_->setEnabled(!constant);
    automaticResolution_->setEnabled(constant);
    manualResolutionMetres_->setEnabled(
        constant && !automaticResolution_->isChecked());
}

void CutFillDialog::updateDerivedPathSummary()
{
    const ExportPaths paths = derivedExportPaths(sidecarPath_->text());
    if (paths.sidecar.isEmpty())
    {
        derivedPathSummary_->setText(tr("选择边车路径后显示。"));
        return;
    }
    derivedPathSummary_->setText(
        tr("汇总 CSV: %1\n逐格 CSV: %2\n概览 SVG: %3")
            .arg(paths.summaryCsv, paths.cellsCsv, paths.svg));
}

CutFillResultDialog::CutFillResultDialog(
    const famp::cutfill::Result& result,
    const QStringList& savedPaths,
    QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("挖填方与体积成果"));
    resize(980, 650);
    auto* layout = new QVBoxLayout(this);
    QString referenceDescription;
    if (result.referenceMode
        == famp::cutfill::ReferenceMode::ConstantElevation)
    {
        referenceDescription = tr("固定设计高程 %1 米")
            .arg(formatMetric(result.constantReferenceElevation
                              * result.currentGrid.horizontalUnitToMetre));
    }
    else
    {
        referenceDescription = result.referenceLayerName.trimmed();
        if (referenceDescription.isEmpty())
            referenceDescription = result.referencePath;
        if (referenceDescription.isEmpty())
            referenceDescription = tr("参考 DEM");
    }
    auto* summary = new QLabel(
        tr("当前地表：%1\n参考地表：%2\n"
           "挖方：%3 立方米 / %4 平方米 / %5 格　　"
           "填方：%6 立方米 / %7 平方米 / %8 格\n"
           "净体积（挖方 - 填方）：%9 立方米　　"
           "平衡区：%10 平方米 / %11 格\n"
           "有效对比：%12 格　　缺少参考：%13 格　　"
           "高差范围：%14 至 %15 米\n"
           "网格：%16 × %17，分辨率 %18 米；净体积为正表示挖方多，为负表示填方多。")
            .arg(result.currentGrid.sourceLayerName,
                 referenceDescription)
            .arg(formatMetric(result.cutVolumeCubicMetres))
            .arg(formatMetric(result.cutAreaSquareMetres))
            .arg(result.cutCellCount)
            .arg(formatMetric(result.fillVolumeCubicMetres))
            .arg(formatMetric(result.fillAreaSquareMetres))
            .arg(result.fillCellCount)
            .arg(formatMetric(result.signedVolumeCubicMetres))
            .arg(formatMetric(result.unchangedAreaSquareMetres))
            .arg(result.unchangedCellCount)
            .arg(result.comparedCellCount)
            .arg(result.missingReferenceCellCount)
            .arg(formatMetric(result.minimumDifferenceMetres))
            .arg(formatMetric(result.maximumDifferenceMetres))
            .arg(result.currentGrid.columns)
            .arg(result.currentGrid.rows)
            .arg(formatMetric(result.currentGrid.resolution
                              * result.currentGrid.horizontalUnitToMetre)),
        this);
    summary->setObjectName(QStringLiteral("cutFillSummary"));
    summary->setWordWrap(true);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary);
    layout->addWidget(new CutFillOverviewWidget(result, this), 1);
    auto* paths = new QLabel(
        tr("成果文件：\n%1").arg(savedPaths.join(QLatin1Char('\n'))),
        this);
    paths->setWordWrap(true);
    paths->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(paths);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    layout->addWidget(buttons);
}
}
