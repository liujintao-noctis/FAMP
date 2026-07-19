#include "ProfileDialog.h"

#include "ProfileIO.h"

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
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

QString coordinateText(const famp::cloud::Point3d& point)
{
    return QStringLiteral("%1, %2, %3")
        .arg(point[0], 0, 'g', 15)
        .arg(point[1], 0, 'g', 15)
        .arg(point[2], 0, 'g', 15);
}

class ProfilePlotWidget final : public QWidget
{
public:
    explicit ProfilePlotWidget(const famp::profile::Result& result,
                               QWidget* parent = nullptr)
        : QWidget(parent)
        , bins_(result.bins)
        , lengthMetres_(result.length * result.horizontalUnitToMetre)
        , minimumElevation_(result.minimumElevation)
        , maximumElevation_(result.maximumElevation)
        , statistic_(result.statistic)
    {
        setObjectName(QStringLiteral("profilePlot"));
        setMinimumSize(720, 340);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAccessibleName(tr("点云高程剖面图"));
    }

    QSize sizeHint() const override
    {
        return {900, 430};
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), Qt::white);
        const QRectF plot = rect().adjusted(82, 28, -28, -62);
        if (plot.width() < 20.0 || plot.height() < 20.0
            || bins_.isEmpty() || lengthMetres_ <= 0.0)
        {
            return;
        }

        double minimum = minimumElevation_;
        double maximum = maximumElevation_;
        double range = maximum - minimum;
        if (!std::isfinite(range) || range <= 1.0e-12)
        {
            const double padding = std::max(0.5, std::abs(maximum) * 0.01);
            minimum -= padding;
            maximum += padding;
        }
        else
        {
            minimum -= range * 0.05;
            maximum += range * 0.05;
        }
        range = maximum - minimum;
        const auto yFor = [&](double elevation) {
            return plot.bottom()
                - (elevation - minimum) / range * plot.height();
        };

        painter.setPen(QPen(QColor(225, 225, 225), 1.0));
        for (int index = 0; index <= 5; ++index)
        {
            const double fraction = static_cast<double>(index) / 5.0;
            const double y = plot.top() + fraction * plot.height();
            painter.drawLine(QPointF(plot.left(), y),
                             QPointF(plot.right(), y));
            painter.setPen(QColor(80, 80, 80));
            painter.drawText(
                QRectF(0.0, y - 10.0, plot.left() - 8.0, 20.0),
                Qt::AlignRight | Qt::AlignVCenter,
                QString::number(maximum - fraction * range, 'g', 7));
            painter.setPen(QPen(QColor(225, 225, 225), 1.0));
        }
        for (int index = 0; index <= 5; ++index)
        {
            const double fraction = static_cast<double>(index) / 5.0;
            const double x = plot.left() + fraction * plot.width();
            painter.drawLine(QPointF(x, plot.top()),
                             QPointF(x, plot.bottom()));
            painter.setPen(QColor(80, 80, 80));
            painter.drawText(
                QRectF(x - 45.0, plot.bottom() + 7.0, 90.0, 22.0),
                Qt::AlignHCenter | Qt::AlignTop,
                QString::number(lengthMetres_ * fraction, 'g', 7));
            painter.setPen(QPen(QColor(225, 225, 225), 1.0));
        }

        struct Bucket
        {
            bool valid = false;
            double minimum = std::numeric_limits<double>::infinity();
            double maximum = -std::numeric_limits<double>::infinity();
            double selectedSum = 0.0;
            int selectedCount = 0;
            int firstSourceBin = std::numeric_limits<int>::max();
            int lastSourceBin = -1;
        };
        const int bucketCount = std::max(2, static_cast<int>(plot.width()) + 1);
        QVector<Bucket> buckets(bucketCount);
        const double sourceLength = bins_.back().endStation;
        for (const famp::profile::Bin& bin : bins_)
        {
            if (!bin.hasSelectedValue() || !std::isfinite(bin.minimum)
                || !std::isfinite(bin.maximum))
            {
                continue;
            }
            const double fraction = sourceLength > 0.0
                ? std::clamp(bin.centerStation / sourceLength, 0.0, 1.0)
                : 0.0;
            const int index = std::clamp(
                static_cast<int>(std::lround(
                    fraction * static_cast<double>(bucketCount - 1))),
                0, bucketCount - 1);
            Bucket& bucket = buckets[index];
            bucket.valid = true;
            bucket.minimum = std::min(bucket.minimum, bin.minimum);
            bucket.maximum = std::max(bucket.maximum, bin.maximum);
            bucket.selectedSum += bin.selected;
            ++bucket.selectedCount;
            bucket.firstSourceBin = std::min(
                bucket.firstSourceBin, bin.index);
            bucket.lastSourceBin = std::max(
                bucket.lastSourceBin, bin.index);
        }

        painter.setPen(QPen(QColor(170, 170, 170, 145), 1.0));
        for (int index = 0; index < buckets.size(); ++index)
        {
            const Bucket& bucket = buckets.at(index);
            if (!bucket.valid)
                continue;
            const double x = plot.left()
                + static_cast<double>(index) / (buckets.size() - 1)
                    * plot.width();
            painter.drawLine(QPointF(x, yFor(bucket.minimum)),
                             QPointF(x, yFor(bucket.maximum)));
        }

        QPainterPath path;
        bool open = false;
        int previousSourceBin = -1;
        for (int index = 0; index < buckets.size(); ++index)
        {
            const Bucket& bucket = buckets.at(index);
            if (!bucket.valid || bucket.selectedCount <= 0)
            {
                open = false;
                continue;
            }
            const double x = plot.left()
                + static_cast<double>(index) / (buckets.size() - 1)
                    * plot.width();
            const double y = yFor(
                bucket.selectedSum / static_cast<double>(bucket.selectedCount));
            if (open && bucket.firstSourceBin <= previousSourceBin + 1)
                path.lineTo(x, y);
            else
                path.moveTo(x, y);
            open = true;
            previousSourceBin = bucket.lastSourceBin;
        }
        painter.setPen(QPen(QColor(21, 101, 192), 2.2));
        painter.drawPath(path);
        painter.setPen(QPen(QColor(45, 45, 45), 1.3));
        painter.drawRect(plot);
        painter.drawText(
            QRectF(plot.left(), plot.bottom() + 32.0,
                   plot.width(), 24.0),
            Qt::AlignCenter, tr("沿剖面距离（米）"));
        painter.save();
        painter.translate(18.0, plot.center().y());
        painter.rotate(-90.0);
        painter.drawText(QRectF(-plot.height() * 0.5, -12.0,
                                plot.height(), 24.0),
                         Qt::AlignCenter, tr("高程（源坐标单位）"));
        painter.restore();
        painter.setPen(QColor(21, 101, 192));
        painter.drawText(
            QRectF(plot.left(), 2.0, plot.width(), 22.0),
            Qt::AlignCenter,
            tr("%1剖面线（灰色范围为段内最低/最高高程）")
                .arg(famp::profile::statisticName(statistic_)));
    }

private:
    QVector<famp::profile::Bin> bins_;
    double lengthMetres_ = 0.0;
    double minimumElevation_ = 0.0;
    double maximumElevation_ = 0.0;
    famp::profile::Statistic statistic_ = famp::profile::Statistic::Median;
};
}

namespace famp::profileui
{
ExportPaths derivedExportPaths(const QString& sidecarPath)
{
    ExportPaths paths;
    paths.sidecar = famp::profileio::pathWithProfileSuffix(sidecarPath);
    if (paths.sidecar.isEmpty())
        return paths;
    const QFileInfo info(paths.sidecar);
    const QString base = info.completeBaseName();
    const QDir directory = info.absoluteDir();
    paths.binsCsv = directory.filePath(
        base + QStringLiteral("_bins.csv"));
    paths.samplesCsv = directory.filePath(
        base + QStringLiteral("_points.csv"));
    paths.svg = directory.filePath(base + QStringLiteral(".svg"));
    return paths;
}

bool validateOptions(const Options& options, QString* errorMessage)
{
    if (!famp::profile::validateOptions(options.analysis, errorMessage))
        return false;
    if (options.sidecarPath.trimmed().isEmpty())
    {
        if (errorMessage)
            errorMessage->clear();
        return true;
    }
    const ExportPaths paths = derivedExportPaths(options.sidecarPath);
    if (paths.sidecar.isEmpty())
    {
        setError(errorMessage, QStringLiteral("点云剖面保存路径无效。"));
        return false;
    }
    if (!QFileInfo(paths.sidecar).absoluteDir().exists())
    {
        setError(errorMessage, QStringLiteral("点云剖面保存目录不存在。"));
        return false;
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

ProfileDialog::ProfileDialog(
    const QString& layerName,
    const QString& crsDescription,
    const QString& horizontalUnitName,
    double horizontalUnitToMetre,
    const famp::profile::Baseline& baseline,
    const QString& initialSidecarPath,
    QWidget* parent)
    : QDialog(parent)
    , horizontalUnitToMetre_(horizontalUnitToMetre)
{
    setWindowTitle(tr("点云高程剖面"));
    setMinimumWidth(640);
    auto* root = new QVBoxLayout(this);

    const double length = std::hypot(
        baseline.end[0] - baseline.start[0],
        baseline.end[1] - baseline.start[1]);
    const double lengthMetres = length * horizontalUnitToMetre_;
    auto* sourceGroup = new QGroupBox(tr("剖面线"), this);
    auto* sourceLayout = new QFormLayout(sourceGroup);
    auto* layerLabel = new QLabel(layerName, sourceGroup);
    layerLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* crsLabel = new QLabel(crsDescription, sourceGroup);
    crsLabel->setWordWrap(true);
    crsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* startLabel = new QLabel(coordinateText(baseline.start), sourceGroup);
    auto* endLabel = new QLabel(coordinateText(baseline.end), sourceGroup);
    startLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    endLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sourceLayout->addRow(tr("点云图层"), layerLabel);
    sourceLayout->addRow(tr("坐标参考"), crsLabel);
    sourceLayout->addRow(tr("水平单位"),
                         new QLabel(horizontalUnitName, sourceGroup));
    sourceLayout->addRow(tr("起点真实坐标"), startLabel);
    sourceLayout->addRow(tr("终点真实坐标"), endLabel);
    sourceLayout->addRow(
        tr("水平长度"),
        new QLabel(tr("%1 米").arg(lengthMetres, 0, 'g', 10), sourceGroup));
    root->addWidget(sourceGroup);

    auto* analysisGroup = new QGroupBox(tr("采样与统计"), this);
    auto* analysisLayout = new QFormLayout(analysisGroup);
    const double defaultBinSize = std::clamp(
        lengthMetres / 500.0, 0.02, 1.0);
    const double defaultWidth = std::clamp(
        std::max(1.0, defaultBinSize * 4.0), 0.05, 100.0);
    corridorWidthMetres_ = new QDoubleSpinBox(analysisGroup);
    corridorWidthMetres_->setDecimals(4);
    corridorWidthMetres_->setRange(0.001, 1'000'000.0);
    corridorWidthMetres_->setValue(defaultWidth);
    corridorWidthMetres_->setSuffix(tr(" 米"));
    corridorWidthMetres_->setToolTip(
        tr("以剖面线为中心的完整走廊宽度；只统计线段范围内的点。"));
    binSizeMetres_ = new QDoubleSpinBox(analysisGroup);
    binSizeMetres_->setDecimals(4);
    binSizeMetres_->setRange(0.001, 1'000'000.0);
    binSizeMetres_->setValue(defaultBinSize);
    binSizeMetres_->setSuffix(tr(" 米"));
    statistic_ = new QComboBox(analysisGroup);
    statistic_->addItem(
        tr("中位数（推荐）"),
        static_cast<int>(famp::profile::Statistic::Median));
    statistic_->addItem(
        tr("最低值"), static_cast<int>(famp::profile::Statistic::Minimum));
    statistic_->addItem(
        tr("最高值"), static_cast<int>(famp::profile::Statistic::Maximum));
    statistic_->addItem(
        tr("平均值"), static_cast<int>(famp::profile::Statistic::Mean));
    minimumPointsPerBin_ = new QSpinBox(analysisGroup);
    minimumPointsPerBin_->setRange(1, 1'000'000);
    minimumPointsPerBin_->setValue(1);
    minimumPointsPerBin_->setToolTip(
        tr("点数不足的采样段会保留为空白，不参与剖面折线。"));
    analysisLayout->addRow(tr("走廊宽度"), corridorWidthMetres_);
    analysisLayout->addRow(tr("沿线采样间隔"), binSizeMetres_);
    analysisLayout->addRow(tr("代表高程"), statistic_);
    analysisLayout->addRow(tr("每段最少点数"), minimumPointsPerBin_);
    root->addWidget(analysisGroup);

    auto* outputGroup = new QGroupBox(tr("成果"), this);
    auto* outputLayout = new QFormLayout(outputGroup);
    auto* pathRow = new QWidget(outputGroup);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    sidecarPath_ = new QLineEdit(initialSidecarPath, pathRow);
    auto* browse = new QPushButton(tr("浏览…"), pathRow);
    pathLayout->addWidget(sidecarPath_, 1);
    pathLayout->addWidget(browse);
    saveImmediately_ = new QCheckBox(
        tr("生成后立即另存文件（默认仅保留在项目内容树中）"), outputGroup);
    saveImmediately_->setObjectName(QStringLiteral("profileSaveImmediately"));
    outputLayout->addRow(saveImmediately_);
    outputLayout->addRow(tr("剖面项目边车"), pathRow);
    exportBinsCsv_ = new QCheckBox(tr("同时导出采样段统计 CSV"), outputGroup);
    exportBinsCsv_->setChecked(true);
    exportSamplesCsv_ = new QCheckBox(tr("同时导出走廊内原始点 CSV"), outputGroup);
    exportSvg_ = new QCheckBox(tr("同时导出剖面图 SVG"), outputGroup);
    exportSvg_->setChecked(true);
    outputLayout->addRow(exportBinsCsv_);
    outputLayout->addRow(exportSamplesCsv_);
    outputLayout->addRow(exportSvg_);
    derivedPathSummary_ = new QLabel(outputGroup);
    derivedPathSummary_->setWordWrap(true);
    derivedPathSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outputLayout->addRow(tr("派生文件"), derivedPathSummary_);
    root->addWidget(outputGroup);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("生成内存成果"));
    root->addWidget(buttons);
    connect(sidecarPath_, &QLineEdit::textChanged,
            this, [this]() { updateDerivedPathSummary(); });
    connect(saveImmediately_, &QCheckBox::toggled, this,
            [this, pathRow, browse](bool enabled) {
                pathRow->setEnabled(enabled);
                browse->setEnabled(enabled);
                exportBinsCsv_->setEnabled(enabled);
                exportSamplesCsv_->setEnabled(enabled);
                exportSvg_->setEnabled(enabled);
                updateDerivedPathSummary();
            });
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString selected = QFileDialog::getSaveFileName(
            this, tr("保存点云剖面项目边车"), sidecarPath_->text(),
            tr("FAMP 点云剖面 (*.famp-profile)"));
        if (!selected.isEmpty())
        {
            sidecarPath_->setText(
                famp::profileio::pathWithProfileSuffix(selected));
        }
    });
    connect(buttons, &QDialogButtonBox::accepted,
            this, &ProfileDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    pathRow->setEnabled(false);
    exportBinsCsv_->setEnabled(false);
    exportSamplesCsv_->setEnabled(false);
    exportSvg_->setEnabled(false);
    updateDerivedPathSummary();
}

Options ProfileDialog::options() const
{
    Options result;
    result.analysis.corridorWidth = corridorWidthMetres_->value()
        / horizontalUnitToMetre_;
    result.analysis.binSize = binSizeMetres_->value()
        / horizontalUnitToMetre_;
    result.analysis.horizontalUnitToMetre = horizontalUnitToMetre_;
    result.analysis.statistic = static_cast<famp::profile::Statistic>(
        statistic_->currentData().toInt());
    result.analysis.minimumPointsPerBin = minimumPointsPerBin_->value();
    result.sidecarPath = saveImmediately_->isChecked()
        ? famp::profileio::pathWithProfileSuffix(sidecarPath_->text())
        : QString();
    result.exportBinsCsv = exportBinsCsv_->isChecked();
    result.exportSamplesCsv = exportSamplesCsv_->isChecked();
    result.exportSvg = exportSvg_->isChecked();
    return result;
}

void ProfileDialog::accept()
{
    QString error;
    if (!validateOptions(options(), &error))
    {
        QMessageBox::warning(this, tr("点云剖面参数无效"), error);
        return;
    }
    QDialog::accept();
}

void ProfileDialog::updateDerivedPathSummary()
{
    if (!saveImmediately_->isChecked())
    {
        derivedPathSummary_->setText(
            tr("不会自动写入磁盘；可稍后在内容树中保存所选实体。"));
        return;
    }
    const ExportPaths paths = derivedExportPaths(sidecarPath_->text());
    if (paths.sidecar.isEmpty())
    {
        derivedPathSummary_->setText(tr("选择边车路径后显示。"));
        return;
    }
    derivedPathSummary_->setText(
        tr("采样段 CSV: %1\n原始点 CSV: %2\n剖面 SVG: %3")
            .arg(paths.binsCsv, paths.samplesCsv, paths.svg));
}

ProfileResultDialog::ProfileResultDialog(
    const famp::profile::Result& result,
    const QStringList& savedPaths,
    QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("点云高程剖面成果"));
    resize(980, 600);
    auto* layout = new QVBoxLayout(this);
    auto* summary = new QLabel(
        tr("图层：%1\n长度：%2 米　走廊宽度：%3 米　采样间隔：%4 米\n"
           "走廊点：%5 / %6　有效采样段：%7 / %8　代表高程：%9")
            .arg(result.sourceLayerName)
            .arg(result.length * result.horizontalUnitToMetre, 0, 'g', 10)
            .arg(result.corridorWidth * result.horizontalUnitToMetre,
                 0, 'g', 10)
            .arg(result.binSize * result.horizontalUnitToMetre, 0, 'g', 10)
            .arg(result.selectedPointCount)
            .arg(result.sourcePointCount)
            .arg(result.populatedBinCount)
            .arg(result.bins.size())
            .arg(famp::profile::statisticName(result.statistic)),
        this);
    summary->setObjectName(QStringLiteral("profileSummary"));
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary);
    layout->addWidget(new ProfilePlotWidget(result, this), 1);
    auto* paths = new QLabel(
        tr("成果文件：\n%1").arg(savedPaths.join(QLatin1Char('\n'))), this);
    paths->setWordWrap(true);
    paths->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(paths);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    layout->addWidget(buttons);
}
}
