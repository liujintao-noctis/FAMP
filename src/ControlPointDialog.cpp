#include "ControlPointDialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
enum Column
{
    EnabledColumn = 0,
    NameColumn,
    LocalXColumn,
    LocalYColumn,
    LocalZColumn,
    TargetXColumn,
    TargetYColumn,
    TargetZColumn,
    ResidualColumn,
    ColumnCount
};

constexpr int MaxDialogPointCount = 10000;

QString coordinateText(double value)
{
    return QString::number(value, 'g', 17);
}

QTableWidgetItem* coordinateItem(double value)
{
    auto* item = new QTableWidgetItem(coordinateText(value));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

bool parseCoordinate(const QTableWidget& table,
                     int row,
                     int column,
                     double& value)
{
    const QTableWidgetItem* item = table.item(row, column);
    bool valid = false;
    const double candidate = item
        ? QLocale::c().toDouble(item->text().trimmed(), &valid) : 0.0;
    if (!valid || !std::isfinite(candidate))
        return false;
    value = candidate;
    return true;
}

QString pointId(const QTableWidget& table, int row)
{
    const QTableWidgetItem* item = table.item(row, NameColumn);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void appendRow(QTableWidget& table, const famp::control::Point& point)
{
    const int row = table.rowCount();
    table.insertRow(row);
    auto* enabled = new QTableWidgetItem;
    enabled->setFlags(
        Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    enabled->setCheckState(point.enabled ? Qt::Checked : Qt::Unchecked);
    enabled->setTextAlignment(Qt::AlignCenter);
    table.setItem(row, EnabledColumn, enabled);

    auto* name = new QTableWidgetItem(point.name);
    name->setData(Qt::UserRole, point.id);
    table.setItem(row, NameColumn, name);
    table.setItem(row, LocalXColumn, coordinateItem(point.local[0]));
    table.setItem(row, LocalYColumn, coordinateItem(point.local[1]));
    table.setItem(row, LocalZColumn, coordinateItem(point.local[2]));
    table.setItem(row, TargetXColumn, coordinateItem(point.target[0]));
    table.setItem(row, TargetYColumn, coordinateItem(point.target[1]));
    table.setItem(row, TargetZColumn, coordinateItem(point.target[2]));
    auto* residual = new QTableWidgetItem(QStringLiteral("—"));
    residual->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    residual->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table.setItem(row, ResidualColumn, residual);
}

bool collectPoints(const QTableWidget& table,
                   QVector<famp::control::Point>& points,
                   QString& error)
{
    QVector<famp::control::Point> candidate;
    candidate.reserve(table.rowCount());
    for (int row = 0; row < table.rowCount(); ++row)
    {
        const QTableWidgetItem* enabled = table.item(row, EnabledColumn);
        const QTableWidgetItem* name = table.item(row, NameColumn);
        famp::control::Point point;
        point.id = pointId(table, row);
        point.name = name ? name->text().trimmed() : QString();
        point.enabled = enabled && enabled->checkState() == Qt::Checked;
        bool coordinatesValid = true;
        for (int axis = 0; axis < 3; ++axis)
        {
            coordinatesValid = coordinatesValid
                && parseCoordinate(
                    table, row, LocalXColumn + axis,
                    point.local[static_cast<std::size_t>(axis)])
                && parseCoordinate(
                    table, row, TargetXColumn + axis,
                    point.target[static_cast<std::size_t>(axis)]);
        }
        if (!coordinatesValid)
        {
            error = QStringLiteral(
                "第 %1 行包含无效坐标；请使用有限数字和英文小数点。")
                        .arg(row + 1);
            return false;
        }
        candidate.append(point);
    }
    if (!famp::control::validatePoints(candidate, &error))
        return false;
    points = candidate;
    error.clear();
    return true;
}

QString qualityText(const famp::control::Quality& quality,
                    const QString& prefix)
{
    return QStringLiteral(
        "%1：启用 %2 点，RMSE %3，平均 %4，最大 %5（图层坐标单位）")
        .arg(prefix)
        .arg(quality.enabledPointCount)
        .arg(quality.rootMeanSquare, 0, 'g', 8)
        .arg(quality.mean, 0, 'g', 8)
        .arg(quality.maximum, 0, 'g', 8);
}
}

namespace famp::control
{
bool editControlPoints(
    QWidget* parent,
    const QString& layerName,
    const QString& sourcePath,
    const famp::cloud::SpatialReference& currentSpatial,
    const QVector<Point>& currentPoints,
    EditResult& result)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("控制点与空间配准"));
    dialog.resize(1180, 680);
    QVBoxLayout layout(&dialog);

    QLabel sourceLabel(
        QStringLiteral("<b>%1</b><br><span style='color:#666'>%2</span>")
            .arg(layerName.toHtmlEscaped(), sourcePath.toHtmlEscaped()),
        &dialog);
    sourceLabel.setTextFormat(Qt::RichText);
    sourceLabel.setWordWrap(true);
    layout.addWidget(&sourceLabel);

    QLabel note(
        QStringLiteral(
            "填写点云局部坐标与同名实测目标坐标；目标坐标应使用图层 CRS，残差单位与图层坐标单位一致。"
            "至少启用 3 个不共线点后可进行保距的 3D 刚体解算；解算更新局部↔真实坐标关系，"
            "不改写点云文件。停用点会保留但不参与解算和残差统计。"),
        &dialog);
    note.setWordWrap(true);
    layout.addWidget(&note);

    QTableWidget table(&dialog);
    table.setColumnCount(ColumnCount);
    table.setHorizontalHeaderLabels({
        QStringLiteral("启用"), QStringLiteral("名称"),
        QStringLiteral("局部 X"), QStringLiteral("局部 Y"),
        QStringLiteral("局部 Z"), QStringLiteral("目标 X"),
        QStringLiteral("目标 Y"), QStringLiteral("目标 Z"),
        QStringLiteral("残差（坐标单位）")});
    table.horizontalHeader()->setSectionResizeMode(
        EnabledColumn, QHeaderView::ResizeToContents);
    table.horizontalHeader()->setSectionResizeMode(
        NameColumn, QHeaderView::ResizeToContents);
    for (int column = LocalXColumn; column <= ResidualColumn; ++column)
    {
        table.horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::Stretch);
    }
    table.setSelectionBehavior(QAbstractItemView::SelectRows);
    table.setSelectionMode(QAbstractItemView::ExtendedSelection);
    table.setAlternatingRowColors(true);
    table.setSortingEnabled(false);
    for (const Point& point : currentPoints)
        appendRow(table, point);
    layout.addWidget(&table, 1);

    QHBoxLayout rowButtons;
    QPushButton addButton(QStringLiteral("添加控制点"), &dialog);
    QPushButton removeButton(QStringLiteral("删除所选"), &dialog);
    QPushButton solveButton(QStringLiteral("解算并计算残差"), &dialog);
    QCheckBox applySolution(
        QStringLiteral("保存时应用本次解算变换"), &dialog);
    applySolution.setEnabled(false);
    rowButtons.addWidget(&addButton);
    rowButtons.addWidget(&removeButton);
    rowButtons.addSpacing(20);
    rowButtons.addWidget(&solveButton);
    rowButtons.addWidget(&applySolution);
    rowButtons.addStretch();
    layout.addLayout(&rowButtons);

    QLabel qualityLabel(QStringLiteral("尚未解算。"), &dialog);
    qualityLabel.setWordWrap(true);
    layout.addWidget(&qualityLabel);

    QDialogButtonBox buttons(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons.button(QDialogButtonBox::Save)->setText(
        QStringLiteral("保存控制点"));
    layout.addWidget(&buttons);

    Solution solved;
    QVector<Point> solvedPoints;
    bool solutionCurrent = false;
    auto invalidateSolution = [&]() {
        solutionCurrent = false;
        applySolution.setChecked(false);
        applySolution.setEnabled(false);
        qualityLabel.setText(QStringLiteral("控制点已修改，请重新解算。"));
        const QSignalBlocker blocker(&table);
        for (int row = 0; row < table.rowCount(); ++row)
        {
            if (QTableWidgetItem* item = table.item(row, ResidualColumn))
                item->setText(QStringLiteral("—"));
        }
    };

    QObject::connect(&table, &QTableWidget::itemChanged,
                     &dialog, [&](QTableWidgetItem*) {
        invalidateSolution();
    });
    QObject::connect(&addButton, &QPushButton::clicked, &dialog, [&]() {
        if (table.rowCount() >= MaxDialogPointCount)
        {
            QMessageBox::warning(
                &dialog, QStringLiteral("控制点与空间配准"),
                QStringLiteral("控制点数量不能超过 10000 个。"));
            return;
        }
        QSet<QString> existingNames;
        for (int row = 0; row < table.rowCount(); ++row)
        {
            const QTableWidgetItem* item = table.item(row, NameColumn);
            if (item)
                existingNames.insert(item->text().trimmed().toCaseFolded());
        }
        int number = table.rowCount() + 1;
        QString name;
        do
        {
            name = QStringLiteral("CP-%1").arg(number++);
        }
        while (existingNames.contains(name.toCaseFolded()));
        Point point;
        point.id = createPointId();
        point.name = name;
        {
            const QSignalBlocker blocker(&table);
            appendRow(table, point);
        }
        table.setCurrentCell(table.rowCount() - 1, NameColumn);
        table.editItem(table.item(table.rowCount() - 1, NameColumn));
        invalidateSolution();
    });
    QObject::connect(&removeButton, &QPushButton::clicked, &dialog, [&]() {
        QSet<int> selectedRows;
        for (const QModelIndex& index : table.selectionModel()->selectedRows())
            selectedRows.insert(index.row());
        QList<int> rows = selectedRows.values();
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : rows)
            table.removeRow(row);
        if (!rows.isEmpty())
            invalidateSolution();
    });
    QObject::connect(&solveButton, &QPushButton::clicked, &dialog, [&]() {
        QVector<Point> candidate;
        QString error;
        if (!collectPoints(table, candidate, error))
        {
            QMessageBox::warning(
                &dialog, QStringLiteral("控制点与空间配准"), error);
            return;
        }
        Solution candidateSolution;
        if (!solveRigid(candidate, candidateSolution, &error))
        {
            QMessageBox::warning(
                &dialog, QStringLiteral("控制点解算失败"), error);
            return;
        }
        {
            const QSignalBlocker blocker(&table);
            QMap<QString, double> residualById;
            for (const Residual& residual : candidateSolution.quality.residuals)
                residualById.insert(residual.pointId, residual.distance);
            for (int row = 0; row < table.rowCount(); ++row)
            {
                const QString id = pointId(table, row);
                table.item(row, ResidualColumn)->setText(
                    residualById.contains(id)
                        ? QString::number(residualById.value(id), 'g', 8)
                        : QStringLiteral("停用"));
            }
        }
        solved = candidateSolution;
        solvedPoints = candidate;
        solutionCurrent = true;
        applySolution.setEnabled(true);
        applySolution.setChecked(true);
        qualityLabel.setText(qualityText(
            solved.quality, QStringLiteral("本次刚体解算")));
    });

    QObject::connect(&buttons, &QDialogButtonBox::rejected,
                     &dialog, &QDialog::reject);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        QVector<Point> candidate;
        QString error;
        if (!collectPoints(table, candidate, error))
        {
            QMessageBox::warning(
                &dialog, QStringLiteral("控制点与空间配准"), error);
            return;
        }
        if (applySolution.isChecked()
            && (!solutionCurrent || !pointsEqual(candidate, solvedPoints)))
        {
            QMessageBox::warning(
                &dialog, QStringLiteral("控制点与空间配准"),
                QStringLiteral("控制点在解算后发生变化，请重新解算。"));
            return;
        }
        result.points = candidate;
        result.applySolution = applySolution.isChecked();
        if (result.applySolution)
            result.solution = solved;
        dialog.accept();
    });

    if (!currentPoints.isEmpty())
    {
        Quality currentQuality;
        QString ignoredError;
        if (evaluate(
                currentPoints, currentSpatial,
                currentQuality, &ignoredError))
        {
            qualityLabel.setText(qualityText(
                currentQuality, QStringLiteral("当前空间参考")));
        }
    }

    return dialog.exec() == QDialog::Accepted;
}
}
