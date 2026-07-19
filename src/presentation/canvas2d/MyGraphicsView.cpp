/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 2D 米格纸视图 — DP 简化、B 样条拟合、KNN Alpha Shape
 *****************************************************************/

#include "MyGraphicsView.h"
#include "FileIO.h"
#include "GraphicsExport.h"
#include "GraphicsItemTransform.h"
#include "GraphicsSceneDocument.h"
#include "MetricGrid.h"
#include "MetricScale.h"
#include "Version.h"

#include <QComboBox>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsItemGroup>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsTextItem>
#include <QPushButton>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineF>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSettings>
#include <QScreen>
#include <QShowEvent>
#include <QStringList>
#include <QUndoStack>
#include <QUuid>
#include <QVBoxLayout>
#include <QWindow>

#include <chrono>

namespace
{
QString workspaceGraphicsName(QGraphicsItem* item)
{
    if (const auto* measurement = dynamic_cast<MeasurementItem*>(item))
    {
        return QStringLiteral("二维%1测量")
            .arg(famp::measurement::kindName(measurement->kind()));
    }
    if (dynamic_cast<ContourItem*>(item))
        return QStringLiteral("二维等高线");
    if (const auto* projection = dynamic_cast<MyItem*>(item))
    {
        const QString label = projection->data(1).toString().trimmed();
        return label.isEmpty() ? QStringLiteral("点云投影轮廓") : label;
    }
    if (dynamic_cast<CompassItem*>(item))
        return QStringLiteral("指北针");
    if (dynamic_cast<FormTabulationItem*>(item))
        return QStringLiteral("制图信息栏");
    if (const auto* text = dynamic_cast<QGraphicsTextItem*>(item))
    {
        QString name = text->toPlainText().simplified();
        if (name.size() > 32)
            name = name.left(32) + QStringLiteral("…");
        return name.isEmpty() ? QStringLiteral("文字") : name;
    }
    if (dynamic_cast<QGraphicsItemGroup*>(item))
        return QStringLiteral("二维图元组");
    return QStringLiteral("二维图元");
}
}
#include <algorithm>
#include <cmath>
#include <stack>
Q_DECLARE_METATYPE(MyOrderCloudType)

namespace
{
const QRectF DefaultDraftingSceneRect(-1500.0, -1500.0, 3000.0, 3000.0);
constexpr qreal OrthographicProjectionGapMillimeters = 10.0;
constexpr qreal OrthographicCanvasMarginMillimeters = 10.0;

class RotationPreviewGraphicsView final : public QGraphicsView
{
public:
    explicit RotationPreviewGraphicsView(QWidget* parent = nullptr)
        : QGraphicsView(parent)
    {
    }

    void fitPreviewBounds(const QRectF& bounds)
    {
        previewBounds_ = bounds.normalized();
        if (scene())
            scene()->setSceneRect(previewBounds_);
        refitPreview();
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QGraphicsView::showEvent(event);
        refitPreview();
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QGraphicsView::resizeEvent(event);
        refitPreview();
    }

private:
    void refitPreview()
    {
        if (!previewBounds_.isValid() || previewBounds_.isEmpty()
            || !viewport() || viewport()->width() <= 1
            || viewport()->height() <= 1)
        {
            return;
        }
        resetTransform();
        fitInView(previewBounds_, Qt::KeepAspectRatio);
        centerOn(previewBounds_.center());
    }

    QRectF previewBounds_;
};

ProjectType projectTypeForPlane(famp::projection::Plane plane)
{
    switch (plane)
    {
    case famp::projection::Plane::YOZ:
        return YOZ;
    case famp::projection::Plane::XOZ:
        return XOZ;
    case famp::projection::Plane::XOY:
        return XOY;
    case famp::projection::Plane::Overlook:
        return OLXOY;
    }
    return NONE;
}

bool fuzzyPointCompare(const QPointF &left, const QPointF &right)
{
    return qFuzzyCompare(left.x() + 1.0, right.x() + 1.0)
        && qFuzzyCompare(left.y() + 1.0, right.y() + 1.0);
}

QVector<QPointF> extendedSectionPoints(const QVector<QPointF>& endpoints)
{
    if (endpoints.size() < 2)
        return {};
    const QPointF first = endpoints.front();
    const QPointF last = endpoints.back();
    QVector2D direction(last - first);
    if (direction.lengthSquared() <= 1.0e-12F)
        return {};
    direction.normalize();
    constexpr qreal ExtensionSceneUnits = 20.0;
    const QPointF extension(
        direction.x() * ExtensionSceneUnits,
        direction.y() * ExtensionSceneUnits);
    return {first - extension, first, last, last + extension};
}

QString metricCalibrationGroup(QScreen* screen)
{
    if (!screen)
        return QStringLiteral("displayMetricCalibration/unknown");

    const QSizeF physicalSize = screen->physicalSize();
    const QString descriptor = QStringList{
        screen->name(),
        screen->manufacturer(),
        screen->model(),
        screen->serialNumber(),
        QString::number(physicalSize.width(), 'f', 3),
        QString::number(physicalSize.height(), 'f', 3)}
        .join(QLatin1Char('|'));
    const QByteArray digest = QCryptographicHash::hash(
        descriptor.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("displayMetricCalibration/%1")
        .arg(QString::fromLatin1(digest));
}

QPointF storedMetricCalibration(QScreen* screen)
{
    if (!screen)
        return QPointF(1.0, 1.0);

    QSettings settings;
    settings.beginGroup(metricCalibrationGroup(screen));
    bool horizontalOk = false;
    bool verticalOk = false;
    const double horizontal = settings.value(
        QStringLiteral("horizontalFactor"), 1.0).toDouble(&horizontalOk);
    const double vertical = settings.value(
        QStringLiteral("verticalFactor"), 1.0).toDouble(&verticalOk);
    settings.endGroup();
    return QPointF(
        horizontalOk && famp::metric::isValidCalibrationFactor(horizontal)
            ? horizontal : 1.0,
        verticalOk && famp::metric::isValidCalibrationFactor(vertical)
            ? vertical : 1.0);
}

void saveMetricCalibration(QScreen* screen, const QPointF& factors)
{
    if (!screen
        || !famp::metric::isValidCalibrationFactor(factors.x())
        || !famp::metric::isValidCalibrationFactor(factors.y()))
    {
        return;
    }

    QSettings settings;
    settings.beginGroup(metricCalibrationGroup(screen));
    settings.setValue(QStringLiteral("horizontalFactor"), factors.x());
    settings.setValue(QStringLiteral("verticalFactor"), factors.y());
    settings.setValue(QStringLiteral("screenName"), screen->name());
    settings.setValue(QStringLiteral("manufacturer"), screen->manufacturer());
    settings.setValue(QStringLiteral("model"), screen->model());
    settings.setValue(QStringLiteral("serialNumber"), screen->serialNumber());
    settings.endGroup();
    settings.sync();
}

void clearMetricCalibration(QScreen* screen)
{
    if (!screen)
        return;
    QSettings settings;
    settings.remove(metricCalibrationGroup(screen));
    settings.sync();
}

class MetricCalibrationTarget final : public QWidget
{
public:
    explicit MetricCalibrationTarget(const QPointF& pixelsPerMillimeter,
                                     QWidget* parent = nullptr)
        : QWidget(parent)
        , pixelsPerMillimeter_(pixelsPerMillimeter)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setPixelsPerMillimeter(const QPointF& value)
    {
        pixelsPerMillimeter_ = value;
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        constexpr int padding = 84;
        const double reference =
            famp::metric::CalibrationReferenceMillimeters;
        return QSize(
            qCeil(pixelsPerMillimeter_.x() * reference) + padding,
            qCeil(pixelsPerMillimeter_.y() * reference) + padding);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);
        painter.setRenderHint(QPainter::Antialiasing, false);

        constexpr qreal origin = 38.0;
        const qreal horizontalLength = pixelsPerMillimeter_.x()
            * famp::metric::CalibrationReferenceMillimeters;
        const qreal verticalLength = pixelsPerMillimeter_.y()
            * famp::metric::CalibrationReferenceMillimeters;
        const QPointF start(origin, origin);
        const QPointF horizontalEnd(origin + horizontalLength, origin);
        const QPointF verticalEnd(origin, origin + verticalLength);

        QPen pen(Qt::black);
        pen.setCosmetic(true);
        pen.setWidthF(1.0);
        painter.setPen(pen);
        painter.drawLine(start, horizontalEnd);
        painter.drawLine(start, verticalEnd);

        for (int millimeters = 0; millimeters <= 100; millimeters += 10)
        {
            const qreal x = origin
                + millimeters * pixelsPerMillimeter_.x();
            const qreal y = origin
                + millimeters * pixelsPerMillimeter_.y();
            painter.drawLine(QPointF(x, origin - 5.0),
                             QPointF(x, origin + 5.0));
            painter.drawLine(QPointF(origin - 5.0, y),
                             QPointF(origin + 5.0, y));
        }

        painter.drawText(
            QRectF(origin, origin + 8.0, horizontalLength, 24.0),
            Qt::AlignHCenter | Qt::AlignTop,
            QObject::tr("横向 100 mm"));
        painter.drawText(
            QRectF(origin + 10.0,
                   origin + verticalLength / 2.0 - 12.0,
                   120.0,
                   24.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            QObject::tr("纵向 100 mm"));

        painter.setPen(QColor(180, 180, 180));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

private:
    QPointF pixelsPerMillimeter_;
};

}

MyGraphicsView::MyGraphicsView(QWidget *parent)
    : QGraphicsView(parent)
{
    //初始化指针
    project_cloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
    currentItemCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);

    dlgPlotTab = NULL;

    labelScene = new QLabel("Scene坐标");
    labelScene->setMinimumWidth(200);
    labelScene->adjustSize();

    this->setCursor(Qt::CrossCursor);   //设置鼠标样式
    this->setMouseTracking(true);       //设置鼠标追踪
    this->setDragMode(QGraphicsView::RubberBandDrag);   //选中鼠标框选内容
    // Projection outlines, selection overlays and the physical grid are all
    // antialiased in separate paint layers. A partial viewport update can
    // leave pixels from the previous transformed bounds behind. Repaint the
    // complete drafting viewport for every interactive frame; the 2D scene is
    // deliberately lightweight and correctness is more important than dirty-
    // region micro-optimisation here.
    this->setCacheMode(QGraphicsView::CacheNone);
    this->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    this->setOptimizationFlag(
        QGraphicsView::DontAdjustForAntialiasing, false);

    scaleType = OneToFifty;         //比例尺默认1:50
    metricPixelsPerMillimeter = QPointF(
        famp::metric::deviceIndependentPixelsPerMillimeter(
            famp::metric::DefaultDotsPerInch),
        famp::metric::deviceIndependentPixelsPerMillimeter(
            famp::metric::DefaultDotsPerInch));
    deltaOffset = scaleOffsetFor(scaleType);

    scene = new QGraphicsScene(DefaultDraftingSceneRect, this);
    history = new QUndoStack(this);
    history->setUndoLimit(100);
    this->setScene(scene);
    connect(scene, &QGraphicsScene::selectionChanged, this, [this]() {
        emit selectionAvailabilityChanged(!scene->selectedItems().isEmpty());
    });
    connect(scene, &QGraphicsScene::changed, this,
            [this](const QList<QRectF>&) { emit workspaceItemsChanged(); });

    setMetricScreen(QGuiApplication::primaryScreen());

}

MyGraphicsView::~MyGraphicsView()
{
    mousePressItemStates.clear();
    invalidateItemHandles();
    history->clear();
    scene->clear();
}

void MyGraphicsView::invalidateItemHandles()
{
    for (auto iterator = itemHandles.begin();
         iterator != itemHandles.end(); ++iterator)
    {
        if (auto handle = iterator.value().lock())
        {
            // QGraphicsScene owns attached items. External workspace payloads
            // may outlive this view, so they must not retain dangling item
            // pointers after the scene deletes its contents.
            handle->deleteWhenDetached = false;
            handle->item = nullptr;
        }
    }
    itemHandles.clear();
}

QUndoStack* MyGraphicsView::commandStack() const
{
    return history;
}

QJsonObject MyGraphicsView::saveProjectState(QString* errorMessage) const
{
    QJsonObject result = famp::graphicsdoc::saveScene(scene, errorMessage);
    if (!result.isEmpty())
    {
        result.insert(QStringLiteral("metricGridVisible"), metricGridVisible);
        result.insert(QStringLiteral("orthographicRotationDegrees"),
                      orthographicRotationDegrees_);
        result.insert(
            QStringLiteral("projectionRotationDegrees"),
            QJsonObject{
                {QStringLiteral("overlook"),
                 orthographicRotationDegrees_},
                {QStringLiteral("xoz"), xozRotationDegrees_},
                {QStringLiteral("yoz"), yozRotationDegrees_}});
    }
    return result;
}

bool MyGraphicsView::validateProjectState(const QJsonObject& state,
                                          QString* errorMessage) const
{
    return famp::graphicsdoc::validateSceneDocument(state, errorMessage);
}

bool MyGraphicsView::restoreProjectState(const QJsonObject& state,
                                         QString* errorMessage)
{
    resetMeasurementInteraction(false);
    if (!validateProjectState(state, errorMessage))
        return false;
    invalidateItemHandles();
    QList<QGraphicsItem*> restoredItems;
    if (!famp::graphicsdoc::restoreScene(
            scene, state, &restoredItems, errorMessage))
    {
        return false;
    }
    history->clear();
    history->setClean();
    const auto validRotation = [](int degrees, int fallback) {
        return degrees >= 0 && degrees < 360 && degrees % 90 == 0
            ? degrees : fallback;
    };
    const int legacyRotation = validRotation(
        state.value(QStringLiteral("orthographicRotationDegrees")).toInt(0),
        0);
    const QJsonObject storedRotations = state.value(
        QStringLiteral("projectionRotationDegrees")).toObject();
    orthographicRotationDegrees_ = validRotation(
        storedRotations.value(QStringLiteral("overlook"))
            .toInt(legacyRotation),
        legacyRotation);
    xozRotationDegrees_ = validRotation(
        storedRotations.value(QStringLiteral("xoz"))
            .toInt(legacyRotation),
        legacyRotation);
    yozRotationDegrees_ = validRotation(
        storedRotations.value(QStringLiteral("yoz"))
            .toInt(legacyRotation),
        legacyRotation);
    metricGridVisible = state.value(QStringLiteral("metricGridVisible"))
                            .toBool(false);
    emit metricGridVisibilityChanged(metricGridVisible);
    rescaleMeasurementItems();
    rescaleTerrainItems();
    viewport()->update();
    emit selectionAvailabilityChanged(false);
    emit workspaceItemsChanged();
    return true;
}

QVector<WorkspaceGraphicsItem> MyGraphicsView::workspaceGraphicsItems()
{
    QVector<WorkspaceGraphicsItem> result;
    for (QGraphicsItem* item : scene->items(Qt::AscendingOrder))
    {
        if (!item || item->parentItem()
            || item->data(famp::graphicsdoc::TransientItemDataKey).toBool())
        {
            continue;
        }
        WorkspaceGraphicsItem entry;
        entry.id = famp::graphicsdoc::ensureItemId(item);
        entry.name = workspaceGraphicsName(item);
        entry.measurement = dynamic_cast<MeasurementItem*>(item) != nullptr;
        entry.visible = item->isVisible();
        entry.handle = handleForItem(item);
        result.append(std::move(entry));
    }
    return result;
}

int MyGraphicsView::projectionDrawingCount() const
{
    int count = 0;
    for (QGraphicsItem* item : scene->items())
    {
        const auto* drawing = item && !item->parentItem()
            ? dynamic_cast<const MyItem*>(item) : nullptr;
        if (drawing && drawing->projectionType() != XOYLine
            && drawing->projectionType() != NONE)
        {
            ++count;
        }
    }
    return count;
}

bool MyGraphicsView::hasProjectionDrawing(
    famp::projection::Plane plane) const
{
    const ProjectType expectedType = projectTypeForPlane(plane);

    for (QGraphicsItem* item : scene->items())
    {
        const auto* drawing = dynamic_cast<const MyItem*>(item);
        if (drawing && drawing->projectionType() == expectedType)
            return true;
    }
    return false;
}

QRectF MyGraphicsView::projectionDrawingSceneBounds(
    famp::projection::Plane plane) const
{
    MyItem* item = primaryProjectionItem(projectTypeForPlane(plane));
    return item ? item->sceneBoundingRect() : QRectF();
}

QPointF MyGraphicsView::projectionDrawingSceneOrigin(
    famp::projection::Plane plane) const
{
    MyItem* item = primaryProjectionItem(projectTypeForPlane(plane));
    return item ? item->mapToScene(QPointF()) : QPointF();
}

bool MyGraphicsView::hasSectionCutLine(
    famp::projection::Plane profilePlane) const
{
    return sectionCutLineItem(projectTypeForPlane(profilePlane)) != nullptr;
}

QRectF MyGraphicsView::sectionCutLineSceneBounds(
    famp::projection::Plane profilePlane) const
{
    MyItem* item = sectionCutLineItem(projectTypeForPlane(profilePlane));
    return item ? item->sceneBoundingRect() : QRectF();
}

QRectF MyGraphicsView::projectionLayoutSceneBounds() const
{
    QRectF bounds;
    bool hasBounds = false;
    const auto uniteItem = [&bounds, &hasBounds](MyItem* item) {
        if (!item)
            return;
        if (hasBounds)
            bounds = bounds.united(item->sceneBoundingRect());
        else
        {
            bounds = item->sceneBoundingRect();
            hasBounds = true;
        }
    };
    for (ProjectType type : {OLXOY, XOZ, YOZ})
    {
        uniteItem(primaryProjectionItem(type));
    }
    uniteItem(sectionCutLineItem(XOZ));
    uniteItem(sectionCutLineItem(YOZ));
    return bounds;
}

QRectF MyGraphicsView::drawingSceneRect() const
{
    return scene ? scene->sceneRect() : QRectF();
}

int MyGraphicsView::drawingScaleDenominator() const
{
    return scaleDenominator(scaleType);
}

bool MyGraphicsView::hasSelectedItems() const
{
    return scene && !scene->selectedItems().isEmpty();
}

bool MyGraphicsView::setProjectionInput(
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud,
    famp::projection::Plane plane,
    QString* errorMessage)
{
    if (!cloud || cloud->empty())
    {
        if (errorMessage)
            *errorMessage = tr("自动绘图的投影预览为空。");
        clearProjectionInput();
        return false;
    }

    project_cloud.reset(
        new pcl::PointCloud<pcl::PointXYZRGB>(*cloud));
    projectionPlane_ = plane;
    project_type = projectTypeForPlane(plane);
    if (errorMessage)
        errorMessage->clear();
    return true;
}

void MyGraphicsView::clearProjectionInput()
{
    project_cloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
    project_type = NONE;
}

bool MyGraphicsView::hasProjectionInput() const
{
    return project_cloud && !project_cloud->empty();
}

famp::projection::Plane MyGraphicsView::projectionPlane() const
{
    return projectionPlane_;
}

bool MyGraphicsView::confirmProjectionRotation(
    QWidget* dialogParent)
{
    if (!hasProjectionInput() || project_type == NONE)
        return false;

    const bool isOverlook =
        projectionPlane_ == famp::projection::Plane::Overlook;
    const bool isProfile = projectionPlane_ == famp::projection::Plane::XOZ
        || projectionPlane_ == famp::projection::Plane::YOZ;
    if (!isOverlook && !isProfile)
        return false;

    const QString drawingName = isOverlook
        ? tr("俯视二维制图")
        : tr("%1 剖面图")
              .arg(famp::projection::axisName(projectionPlane_));

    QVector<QPointF> previewPoints;
    PCLCloud2QTPoints(project_cloud, project_type, previewPoints,
                     QPointF(1.0, 1.0));
    if (previewPoints.isEmpty())
        return false;

    QDialog dialog(dialogParent ? dialogParent : this);
    dialog.setObjectName(QStringLiteral("initialDrawingRotationDialog"));
    dialog.setWindowTitle(tr("确认%1方向").arg(drawingName));
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.setMinimumWidth(540);

    auto* layout = new QVBoxLayout(&dialog);
    auto* instruction = new QLabel(&dialog);
    instruction->setObjectName(
        QStringLiteral("projectionDrawingRotationInstruction"));
    instruction->setText(
        isOverlook
            ? tr("俯视二维制图是三图排版和剖切线的固定基准。请选择旋转角度，"
                 "在下方预览确认后再生成。")
            : tr("请选择当前 %1 的旋转角度。确认后才会生成正式成果，"
                 "并自动对齐俯视图中的对应剖面切割线。")
                  .arg(drawingName));
    instruction->setWordWrap(true);
    layout->addWidget(instruction);

    auto* preview = new RotationPreviewGraphicsView(&dialog);
    preview->setObjectName(QStringLiteral("initialDrawingRotationPreview"));
    preview->setMinimumSize(500, 300);
    preview->setRenderHint(QPainter::Antialiasing, true);
    preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview->setBackgroundBrush(QColor(31, 37, 43));
    auto* previewScene = new QGraphicsScene(preview);
    preview->setScene(previewScene);

    qreal minimumX = previewPoints.first().x();
    qreal maximumX = minimumX;
    qreal minimumY = previewPoints.first().y();
    qreal maximumY = minimumY;
    for (const QPointF& point : previewPoints)
    {
        minimumX = qMin(minimumX, point.x());
        maximumX = qMax(maximumX, point.x());
        minimumY = qMin(minimumY, point.y());
        maximumY = qMax(maximumY, point.y());
    }
    const QRectF pointBounds(
        QPointF(minimumX, minimumY), QPointF(maximumX, maximumY));
    const qreal extent = qMax<qreal>(
        1.0, qMax(pointBounds.width(), pointBounds.height()));
    const qreal pointRadius = extent / 220.0;
    const qsizetype sampleStep = qMax<qsizetype>(
        1, previewPoints.size() / 1500);
    QPainterPath pointPath;
    for (qsizetype index = 0; index < previewPoints.size(); index += sampleStep)
    {
        pointPath.addEllipse(previewPoints.at(index),
                             pointRadius, pointRadius);
    }

    auto* previewGroup = new QGraphicsItemGroup;
    previewScene->addItem(previewGroup);
    auto* cloudItem = new QGraphicsPathItem(pointPath);
    cloudItem->setPen(Qt::NoPen);
    cloudItem->setBrush(QColor(0, 210, 255));
    previewGroup->addToGroup(cloudItem);

    const QPointF axisOrigin(pointBounds.left(), pointBounds.bottom());
    const qreal axisLength = extent * 0.22;
    auto* horizontalAxis = new QGraphicsLineItem(
        QLineF(axisOrigin, axisOrigin + QPointF(axisLength, 0.0)));
    horizontalAxis->setPen(QPen(QColor(255, 96, 96),
                                qMax<qreal>(pointRadius * 1.4, 0.01)));
    previewGroup->addToGroup(horizontalAxis);
    auto* verticalAxis = new QGraphicsLineItem(
        QLineF(axisOrigin, axisOrigin - QPointF(0.0, axisLength)));
    verticalAxis->setPen(QPen(QColor(104, 238, 144),
                              qMax<qreal>(pointRadius * 1.4, 0.01)));
    previewGroup->addToGroup(verticalAxis);

    QString horizontalLabel = QStringLiteral("X");
    QString verticalLabel = QStringLiteral("Y");
    if (project_type == YOZ)
        horizontalLabel = QStringLiteral("Z");
    if (project_type == XOZ)
        verticalLabel = QStringLiteral("Z");
    auto* horizontalText = new QGraphicsSimpleTextItem(horizontalLabel);
    horizontalText->setBrush(QColor(255, 145, 145));
    horizontalText->setScale(extent / 500.0);
    horizontalText->setPos(axisOrigin + QPointF(axisLength, 0.0));
    previewGroup->addToGroup(horizontalText);
    auto* verticalText = new QGraphicsSimpleTextItem(verticalLabel);
    verticalText->setBrush(QColor(150, 255, 178));
    verticalText->setScale(extent / 500.0);
    verticalText->setPos(axisOrigin - QPointF(0.0, axisLength));
    previewGroup->addToGroup(verticalText);

    previewGroup->setTransformOriginPoint(
        previewGroup->boundingRect().center());
    layout->addWidget(preview, 1);

    auto* form = new QFormLayout;
    auto* rotationCombo = new QComboBox(&dialog);
    rotationCombo->setObjectName(
        QStringLiteral("initialDrawingRotationCombo"));
    rotationCombo->addItem(tr("不旋转（0°）"), 0);
    rotationCombo->addItem(tr("顺时针 90°"), 90);
    rotationCombo->addItem(tr("顺时针 180°"), 180);
    rotationCombo->addItem(tr("顺时针 270°"), 270);
    const int currentIndex = rotationCombo->findData(
        projectionRotationDegrees(projectionPlane_));
    rotationCombo->setCurrentIndex(currentIndex >= 0 ? currentIndex : 0);
    form->addRow(tr("成果方向"), rotationCombo);
    layout->addLayout(form);

    auto* hint = new QLabel(&dialog);
    hint->setObjectName(
        QStringLiteral("projectionDrawingRotationHint"));
    hint->setText(
        isOverlook
            ? tr("该角度应用于俯视图及其两条剖面切割线；XOZ、YOZ 生成时"
                 "仍会分别询问方向。不会修改源点云坐标或制图比例尺。")
            : tr("该角度只应用于当前 %1；俯视图保持不动。确认后系统仍固定"
                 "YOZ 在俯视图上方、XOZ 在右侧，并按对应切割线中心轴对齐。")
                  .arg(drawingName));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName(QStringLiteral("initialDrawingRotationButtons"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(
        QStringLiteral("initialDrawingRotationConfirmButton"));
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确认并生成"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted,
            &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    QRectF allRotationBounds;
    bool hasRotationBounds = false;
    for (const int degrees : {0, 90, 180, 270})
    {
        previewGroup->setRotation(degrees);
        const QRectF rotatedBounds = previewScene->itemsBoundingRect();
        if (rotatedBounds.isValid() && !rotatedBounds.isEmpty())
        {
            allRotationBounds = hasRotationBounds
                ? allRotationBounds.united(rotatedBounds)
                : rotatedBounds;
            hasRotationBounds = true;
        }
    }
    previewGroup->setRotation(0.0);
    const qreal previewMargin = qMax<qreal>(extent * 0.08, 0.1);
    const QRectF fixedPreviewBounds = allRotationBounds.adjusted(
        -previewMargin, -previewMargin,
        previewMargin, previewMargin);

    const auto updatePreview = [preview, previewScene, previewGroup,
                                rotationCombo, fixedPreviewBounds]() {
        previewGroup->setRotation(rotationCombo->currentData().toInt());
        previewScene->update();
        preview->fitPreviewBounds(fixedPreviewBounds);
    };
    connect(rotationCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog, [updatePreview](int) { updatePreview(); });
    dialog.ensurePolished();
    dialog.adjustSize();
    updatePreview();

    if (dialog.exec() != QDialog::Accepted)
        return false;
    return setProjectionRotationDegrees(
        projectionPlane_, rotationCombo->currentData().toInt());
}

bool MyGraphicsView::confirmInitialOrthographicRotation(
    QWidget* dialogParent)
{
    return confirmProjectionRotation(dialogParent);
}

bool MyGraphicsView::setProjectionRotationDegrees(
    famp::projection::Plane plane,
    int degrees)
{
    int normalized = degrees % 360;
    if (normalized < 0)
        normalized += 360;
    if (normalized % 90 != 0)
        return false;

    switch (plane)
    {
    case famp::projection::Plane::Overlook:
        orthographicRotationDegrees_ = normalized;
        // A profile that has not been drawn yet starts from the confirmed
        // plan direction, while remaining independently adjustable in its
        // own confirmation dialog.
        if (!hasProjectionDrawing(famp::projection::Plane::XOZ))
            xozRotationDegrees_ = normalized;
        if (!hasProjectionDrawing(famp::projection::Plane::YOZ))
            yozRotationDegrees_ = normalized;
        break;
    case famp::projection::Plane::XOZ:
        xozRotationDegrees_ = normalized;
        break;
    case famp::projection::Plane::YOZ:
        yozRotationDegrees_ = normalized;
        break;
    case famp::projection::Plane::XOY:
        return false;
    }

    layoutOrthographicProjectionDrawings();
    viewport()->update();
    return true;
}

int MyGraphicsView::projectionRotationDegrees(
    famp::projection::Plane plane) const
{
    switch (plane)
    {
    case famp::projection::Plane::Overlook:
        return orthographicRotationDegrees_;
    case famp::projection::Plane::XOZ:
        return xozRotationDegrees_;
    case famp::projection::Plane::YOZ:
        return yozRotationDegrees_;
    case famp::projection::Plane::XOY:
        return 0;
    }
    return 0;
}

bool MyGraphicsView::setOrthographicRotationDegrees(int degrees)
{
    int normalized = degrees % 360;
    if (normalized < 0)
        normalized += 360;
    if (normalized % 90 != 0)
        return false;
    orthographicRotationDegrees_ = normalized;
    xozRotationDegrees_ = normalized;
    yozRotationDegrees_ = normalized;
    layoutOrthographicProjectionDrawings();
    viewport()->update();
    return true;
}

int MyGraphicsView::orthographicRotationDegrees() const
{
    return orthographicRotationDegrees_;
}

bool MyGraphicsView::setWorkspaceItemVisible(const QString& id, bool visible)
{
    const QUuid parsed(id);
    if (parsed.isNull())
        return false;
    const QString normalized = parsed.toString(
        QUuid::WithoutBraces).toLower();
    for (QGraphicsItem* item : scene->items())
    {
        if (famp::graphicsdoc::itemId(item) == normalized)
        {
            item->setVisible(visible);
            viewport()->update();
            emit workspaceItemsChanged();
            return true;
        }
    }
    return false;
}

bool MyGraphicsView::removeWorkspaceItemPermanently(const QString& id)
{
    const QUuid parsed(id);
    if (parsed.isNull())
        return false;
    const QString normalized = parsed.toString(
        QUuid::WithoutBraces).toLower();
    QGraphicsItem* target = nullptr;
    for (QGraphicsItem* item : scene->items())
    {
        if (famp::graphicsdoc::itemId(item) == normalized)
        {
            target = item;
            break;
        }
    }
    if (!target)
        return false;

    history->clear();
    for (auto iterator = itemHandles.begin(); iterator != itemHandles.end(); ++iterator)
    {
        if (auto handle = iterator.value().lock())
        {
            handle->deleteWhenDetached = false;
            handle->item = nullptr;
        }
    }
    itemHandles.clear();
    scene->removeItem(target);
    delete target;
    layoutOrthographicProjectionDrawings();
    emit workspaceItemsChanged();
    return true;
}

void MyGraphicsView::selectWorkspaceItem(const QString& id, bool center)
{
    const QUuid parsed(id);
    if (parsed.isNull())
        return;
    const QString normalized = parsed.toString(
        QUuid::WithoutBraces).toLower();
    for (QGraphicsItem* item : scene->items())
    {
        if (famp::graphicsdoc::itemId(item) != normalized)
            continue;
        scene->clearSelection();
        if (item->flags().testFlag(QGraphicsItem::ItemIsSelectable))
            item->setSelected(true);
        if (center)
            centerOn(item);
        return;
    }
}

void MyGraphicsView::clearWorkspaceItemSelection()
{
    if (!scene)
        return;
    scene->clearSelection();
    viewport()->update();
}

famp::graphics::ItemHandle MyGraphicsView::handleForItem(QGraphicsItem* item)
{
    if (!item)
        return {};

    auto existing = itemHandles.value(item).lock();
    if (existing)
        return existing;

    auto handle = std::make_shared<famp::graphics::ItemLifetime>(item);
    itemHandles.insert(item, handle);
    return handle;
}

QVector<famp::graphics::ItemHandle> MyGraphicsView::handlesForItems(
    const QList<QGraphicsItem*>& items)
{
    QVector<famp::graphics::ItemHandle> handles;
    handles.reserve(items.size());
    for (QGraphicsItem* item : items)
    {
        if (item && std::none_of(handles.cbegin(), handles.cend(),
                                [item](const auto& handle) {
                                    return handle && handle->item == item;
                                }))
        {
            handles.push_back(handleForItem(item));
        }
    }
    return handles;
}

QVector<famp::graphics::ItemState> MyGraphicsView::selectedItemStates()
{
    return famp::graphics::captureItemStates(
        handlesForItems(scene->selectedItems()));
}

void MyGraphicsView::pushTransformChange(
    const QVector<famp::graphics::ItemState>& before,
    const QString& text)
{
    QVector<famp::graphics::ItemHandle> handles;
    handles.reserve(before.size());
    for (const auto& state : before)
        handles.push_back(state.handle);

    const auto after = famp::graphics::captureItemStates(handles);
    if (!famp::graphics::itemStatesEqual(before, after))
    {
        history->push(famp::graphics::makeTransformCommand(
            before, after, text));
    }
}

void MyGraphicsView::addItemWithHistory(QGraphicsItem* item,
                                        const QString& text)
{
    if (!item)
        return;
    history->push(famp::graphics::makeAddItemCommand(
        scene, handleForItem(item), text));
}

bool MyGraphicsView::addTerrainContours(
    const QVector<famp::terrain::ContourLine>& lines,
    double horizontalUnitToMetre,
    const QString& sourceCrs,
    const QString& sourceLayerId,
    const QString& sourceLayerName,
    const QString& demPath,
    double interval,
    double baseElevation,
    QString* errorMessage)
{
    ContourItemData data;
    if (!ContourItem::createDataFromAbsolute(
            lines, horizontalUnitToMetre, sourceCrs, sourceLayerId,
            sourceLayerName, demPath, interval, baseElevation,
            data, errorMessage))
    {
        return false;
    }
    auto* item = new ContourItem(std::move(data), deltaOffset);
    item->setPos(scene->sceneRect().center() - item->boundingRect().center());
    addItemWithHistory(item, tr("添加 DEM 等高线"));
    scene->clearSelection();
    item->setSelected(true);
    if (errorMessage)
        errorMessage->clear();
    return true;
}

int MyGraphicsView::terrainContourCount() const
{
    int count = 0;
    for (QGraphicsItem* item : scene->items())
    {
        if (item && item->type() == ContourItem::Type)
            ++count;
    }
    return count;
}

void MyGraphicsView::invalidateHistory(const QString& reason)
{
    mousePressItemStates.clear();
    if (history->count() == 0)
        return;
    history->clear();
    emit sendStrFromGraphicView2Console(
        QStringLiteral("因%1已重置撤销历史。").arg(reason));
}

void MyGraphicsView::clearSceneAndHistory()
{
    resetMeasurementInteraction(true);
    mousePressItemStates.clear();
    invalidateItemHandles();
    history->clear();
    scene->clear();
    scene->setSceneRect(DefaultDraftingSceneRect);
    orthographicRotationDegrees_ = 0;
    xozRotationDegrees_ = 0;
    yozRotationDegrees_ = 0;
}

void MyGraphicsView::moveSelectedItemsBy(const QPointF& delta,
                                         const QString& text)
{
    const auto before = selectedItemStates();
    for (const auto& state : before)
    {
        if (state.handle && state.handle->item)
            state.handle->item->moveBy(delta.x(), delta.y());
    }
    pushTransformChange(before, text);
}

void MyGraphicsView::getDBItemCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud)
{
    this->currentItemCloud = Cloud;
    clearSectionPlaneReference();
}

void MyGraphicsView::setSectionPlaneReference(
    const QVector3D& origin,
    const QVector3D& normal)
{
    if (!std::isfinite(origin.x()) || !std::isfinite(origin.y())
        || !std::isfinite(origin.z()) || !std::isfinite(normal.x())
        || !std::isfinite(normal.y()) || !std::isfinite(normal.z())
        || normal.lengthSquared() <= 1.0e-12F)
    {
        clearSectionPlaneReference();
        return;
    }
    sectionPlaneOrigin_ = origin;
    sectionPlaneNormal_ = normal.normalized();
    hasSectionPlaneReference_ = true;
}

void MyGraphicsView::clearSectionPlaneReference()
{
    hasSectionPlaneReference_ = false;
    sectionPlaneOrigin_ = {};
    sectionPlaneNormal_ = {};
}

//是否绘制切割线对话框
int MyGraphicsView::dlgDrawXOYLine()
{
    QString dlgTitle = "XOY面投影线绘制";
    QString strInfo = "是否生成剖面切割线？";

    emit sendDlgClipVisible(false);     //将VTK的平面裁剪对话框隐藏
    QMessageBox msg;
    msg.setText(strInfo);
    msg.setWindowTitle(dlgTitle);
    msg.setStandardButtons(QMessageBox::Yes | QMessageBox::No| QMessageBox::Cancel);
    msg.setDefaultButton(QMessageBox::Cancel);
    int result = msg.exec();

    switch (result)
    {
    case(QMessageBox::Yes):
    {
        return 1;       //绘制剖面线
    }
    break;

    case(QMessageBox::No):
    {
        return 0;   //不绘制剖面线
    }
    break;

    case(QMessageBox::Cancel):
    {
        return -1;          //取消
    }
    break;

    default:
        break;
    }
    return -1;      // 默认取消
}

//计算点云的平均密度（点与点之间的平均距离）
double MyGraphicsView::computeCloudMeanDis(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud)
{
    double meanDis = 0.0;   //平均距离
    int numberOfPoints = 0;     //有效的点云数
    std::vector<int> indices;
    std::vector<float> squareDistances;

    pcl::search::KdTree< pcl::PointXYZRGB> kdtree;
    kdtree.setInputCloud(cloud);

    for (size_t i = 0; i < cloud->size(); i++)
    {
        if (!pcl::isFinite(cloud->points[i]))       continue;       //检查该值是否为正常值（有限）

        if (kdtree.nearestKSearch(i, 2, indices, squareDistances) >= 2
            && squareDistances.size() >= 2)
        {
            meanDis += sqrt(squareDistances[1]);
            ++numberOfPoints;
        }
    }

    if (numberOfPoints != 0)
    {
        meanDis /= numberOfPoints;
    }
    return meanDis;
}

//画出XOZ面的投影
//统一的投影绘制方法，消除4个draw方法的代码重复
void MyGraphicsView::drawProjection(const DrawConfig& config, QPointF offset)
{
    //若需要体素滤波降采样（俯视XOY投影专用）
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudToOrder(
        new pcl::PointCloud<pcl::PointXYZRGB>(*project_cloud));
    if (config.useVoxelDownsample)
    {
        float leaf = 0.01;
        while (cloudToOrder->size() > 10000)
        {
            pcl::VoxelGrid<pcl::PointXYZRGB> vog;
            vog.setInputCloud(cloudToOrder);
            vog.setLeafSize(leaf, leaf, leaf);
            vog.filter(*cloudToOrder);
            //不再在循环内写磁盘 — 见Task B
            leaf += 0.01;
        }

        //计算投影点云的平均密度
        double densityOL = computeCloudMeanDis(cloudToOrder);
        if (!std::isfinite(densityOL) || densityOL <= 0.0)
        {
            QMessageBox::warning(
                this, tr("俯视自动绘图"),
                tr("投影点云的有效间距为 0，无法提取边界。"));
            return;
        }

        //KNNAlphaShape提取边界
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr boundarypoints(new pcl::PointCloud<pcl::PointXYZRGB>);
        KNNAlphaShape(cloudToOrder, densityOL*2.5, densityOL*2.5, boundarypoints);  //以平均密度的2.0-3.0倍作为Alpha半径和KNN搜索半径

        if (boundarypoints->size() < 3)
        {
            QMessageBox::warning(
                this, tr("俯视自动绘图"),
                tr("提取到的边界点少于 3 个，无法形成轮廓。"));
            return;
        }

        cloudToOrder = boundarypoints;
    }

    //计算投影点云的平均密度
    double density = computeCloudMeanDis(cloudToOrder);

    //将投影/边界的点云转为有序点云
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr orderCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    orderSoetCloud(cloudToOrder, std::min(static_cast<int>(cloudToOrder->size() - 1), 30), orderCloud);
    if (orderCloud->size() < 2)
    {
        QMessageBox::warning(
            this, tr("自动绘图"),
            tr("投影点云无法形成连续轮廓。"));
        return;
    }
#ifdef FAMP_DEBUG
    // 调试：保存有序点云
    // pcl::io::savePCDFileASCII(config.debugOrderFile, *orderCloud);
#endif

    //将有序化的点云用DP进行简化
    std::vector<DPPoint> points_DP;
    for (size_t i = 0; i < orderCloud->size(); i++)
    {
        DPPoint point;
        point.x = orderCloud->points[i].x;
        point.y = orderCloud->points[i].y;
        point.z = orderCloud->points[i].z;
        point.ID = i;

        points_DP.push_back(point);
    }

    computeDP(points_DP, 0, points_DP.size() - 1, density);     //DP简化

    //将DP点转为PCL点云
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_DP(new pcl::PointCloud<pcl::PointXYZRGB>);
    for (size_t i = 0; i < points_DP.size(); i++)
    {
        if (points_DP[i].isRemoved == true) continue;

        pcl::PointXYZRGB point;
        point.x = points_DP.at(i).x;
        point.y = points_DP.at(i).y;
        point.z = points_DP.at(i).z;

        cloud_DP->push_back(point);
    }

#ifdef FAMP_DEBUG
    if (!cloud_DP->empty())
        // 调试：保存 DP 简化结果
        // pcl::io::savePCDFileASCII(config.debugDPFile, *cloud_DP);
#endif
    qDebug() << "原始点数:" << orderCloud->size() << "\t" << "DP简化后点云数：" << "\t" << cloud_DP->size();

    //首次绘制和后续比例尺重绘必须使用同一份 DP 简化点云。
    QVector<QPointF> qtPoints;
    PCLCloud2QTPoints(cloud_DP, config.projType, qtPoints,
                     QPointF(offset.x(), offset.y()));

    //将点云传入到重写的QGraphicsItem中绘制
    MyItem *item = new MyItem(qtPoints, config.projType);
    MyOrderCloudType myordercloud;
    myordercloud.orderCloud = cloud_DP;
    myordercloud.project_type = config.projType;
    item->setData(ItemName, config.labelText);
    item->setData(ItemCloud, QVariant::fromValue(myordercloud));
    addItemWithHistory(item, tr("添加投影轮廓"));
    scene->clearSelection();
    item->setSelected(true);
    layoutOrthographicProjectionDrawings();
    emit sendStrFromGraphicView2Console(QString(config.consoleMsg));
}

void MyGraphicsView::drawXOZ(QPointF offset)
{
    MyItem* previous = primaryProjectionItem(XOZ);
    DrawConfig cfg = { XOZ, "XOZ面投影", false, false, "ordercloud_xoz.pcd", "cloud_DP_XOZ.pcd", "已生成XOZ面投影连线！" };
    drawProjection(cfg, offset);
    MyItem* profile = primaryProjectionItem(XOZ);
    if (profile && profile != previous)
    {
        drawSectionCutLine(XOZ, offset);
        scene->clearSelection();
        profile->setSelected(true);
    }
}

//画出YOZ面的投影
void MyGraphicsView::drawYOZ(QPointF offset)
{
    MyItem* previous = primaryProjectionItem(YOZ);
    DrawConfig cfg = { YOZ, "YOZ面投影", false, false, "ordercloud_yoz.pcd", "cloud_DP_YOZ.pcd", "已生成YOZ面投影连线！" };
    drawProjection(cfg, offset);
    MyItem* profile = primaryProjectionItem(YOZ);
    if (profile && profile != previous)
    {
        drawSectionCutLine(YOZ, offset);
        scene->clearSelection();
        profile->setSelected(true);
    }
}

//画出XOY面的投影
void MyGraphicsView::drawXOY(QPointF offset)
{
    DrawConfig cfg = { XOY, "XOY面投影", false, false, "ordercloud_xoy.pcd", "cloud_DP_XOY.pcd", "已生成XOY面投影连线！" };
    drawProjection(cfg, offset);
}

//画出俯视XOY面的投影
void MyGraphicsView::drawOverLookXOY(QPointF offset)
{
    DrawConfig cfg = { OLXOY, "俯视投影", true, true, "ordercloud_olxoy.pcd", "cloud_DP_OLXOY.pcd", "已生成俯视投影面投影连线！" };
    drawProjection(cfg, offset);
}

//画出XOY面的剖线
void MyGraphicsView::drawXOYLine(QPointF offset)
{
    //计算投影点云的平均密度
    double density = computeCloudMeanDis(project_cloud);
    //qDebug() << "density" << density;

    //将提取的无序点云有序化
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr orderCloudPointsXOYLine(new pcl::PointCloud<pcl::PointXYZRGB>);
    orderSoetCloud(project_cloud, std::min(static_cast<int>(project_cloud->size() - 1), 30), orderCloudPointsXOYLine);
    // 调试：保存 XOY 剖线有序点云
    // pcl::io::savePCDFileASCII("ordercloud_xoyLine.pcd", *orderCloudPointsXOYLine);

    //提取第一个点和最后一个点
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr XOYLineCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    XOYLineCloud->push_back(orderCloudPointsXOYLine->front());
    XOYLineCloud->push_back(orderCloudPointsXOYLine->back());

    //将有序点云转为QT点集
    QVector<QPointF> points_xoy;
    PCLCloud2QTPoints(XOYLineCloud, XOY, points_xoy, QPointF(offset.x(), offset.y()));

    //计算两个点的直线向量
    QVector2D line_dir = QVector2D(points_xoy.back().x() - points_xoy.front().x(), points_xoy.back().y() - points_xoy.front().y());
    line_dir.normalize();

    QPointF P1, P2, P3, P4;
    P2 = QPointF(points_xoy.front().x(), points_xoy.front().y());
    P3 = QPointF(points_xoy.back().x(), points_xoy.back().y());
    P1 = QPointF(P2.x() - line_dir.x() * 20, P2.y() - line_dir.y() * 20);
    P4 = QPointF(P3.x() + line_dir.x() * 20, P3.y() + line_dir.y() * 20);

    points_xoy.clear();
    points_xoy.push_back(P1);
    points_xoy.push_back(P2);
    points_xoy.push_back(P3);
    points_xoy.push_back(P4);

    //将点云传入到重写的QGraphicsItem中绘制
    MyItem *itemXOYLine = new MyItem(points_xoy, XOY);
    MyOrderCloudType xoyline_myordercloud;
    xoyline_myordercloud.orderCloud = XOYLineCloud;
    xoyline_myordercloud.project_type = XOY;
    itemXOYLine->setData(ItemName, "剖面线");
    itemXOYLine->setData(ItemCloud, QVariant::fromValue(xoyline_myordercloud));
    addItemWithHistory(itemXOYLine, tr("添加剖面线"));
    scene->clearSelection();
    itemXOYLine->setSelected(true);
    emit sendStrFromGraphicView2Console(QString::asprintf("已生成剖面连线！"));

}

void MyGraphicsView::drawSectionCutLine(ProjectType profileType,
                                        QPointF offset)
{
    if ((profileType != XOZ && profileType != YOZ)
        || !currentItemCloud || currentItemCloud->size() < 2)
    {
        return;
    }

    pcl::PointXYZRGB minimum;
    pcl::PointXYZRGB maximum;
    pcl::getMinMax3D(*currentItemCloud, minimum, maximum);
    if (!pcl::isFinite(minimum) || !pcl::isFinite(maximum))
        return;

    auto endpoints = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    pcl::PointXYZRGB first;
    pcl::PointXYZRGB last;
    if (profileType == XOZ)
    {
        if (maximum.x - minimum.x <= 1.0e-9F)
            return;
        float sectionY = (minimum.y + maximum.y) * 0.5F;
        // A confirmed plane-clip entity retains the exact clipping plane.
        // Prefer it when its normal matches the requested axis-aligned
        // profile; the clipped point bounds remain the line extent.
        if (hasSectionPlaneReference_
            && std::abs(sectionPlaneNormal_.y()) >= 0.98F)
        {
            sectionY = sectionPlaneOrigin_.y();
        }
        first.x = minimum.x;
        first.y = sectionY;
        last.x = maximum.x;
        last.y = sectionY;
    }
    else
    {
        if (maximum.y - minimum.y <= 1.0e-9F)
            return;
        float sectionX = (minimum.x + maximum.x) * 0.5F;
        if (hasSectionPlaneReference_
            && std::abs(sectionPlaneNormal_.x()) >= 0.98F)
        {
            sectionX = sectionPlaneOrigin_.x();
        }
        first.x = sectionX;
        first.y = minimum.y;
        last.x = sectionX;
        last.y = maximum.y;
    }
    first.z = 0.0F;
    last.z = 0.0F;
    endpoints->push_back(first);
    endpoints->push_back(last);

    QVector<QPointF> endpointScenePoints;
    PCLCloud2QTPoints(
        endpoints, XOY, endpointScenePoints,
        QPointF(offset.x(), offset.y()));
    QVector<QPointF> displayedPoints =
        extendedSectionPoints(endpointScenePoints);
    if (displayedPoints.size() != 4)
        return;

    MyOrderCloudType sectionSource;
    sectionSource.orderCloud = endpoints;
    sectionSource.project_type = XOY;
    const QString label = profileType == XOZ
        ? tr("XOZ 剖面切割线") : tr("YOZ 剖面切割线");
    MyItem* line = sectionCutLineItem(profileType);
    if (line)
    {
        line->setPoints(displayedPoints);
        line->setData(ItemName, label);
        line->setData(ItemCloud, QVariant::fromValue(sectionSource));
    }
    else
    {
        line = new MyItem(displayedPoints, XOYLine);
        line->setData(ItemName, label);
        line->setData(ItemCloud, QVariant::fromValue(sectionSource));
        line->setData(SectionPlaneDataKey, static_cast<int>(profileType));
        addItemWithHistory(line, tr("添加%1").arg(label));
    }
    layoutOrthographicProjectionDrawings();
    emit sendStrFromGraphicView2Console(
        tr("已在俯视图上自动生成并对齐 %1。").arg(label));
}

/*
KNNAlphaShape提取边界
\* 输入点云
\*输入滚球法Alpha半径
\*输入KNN搜索半径
\*输出提取的点云
*/
void MyGraphicsView::KNNAlphaShape(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud, float Alpha, float neborRadius, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &outcloud)
{

    std::vector<QVector2D> qt_points;       //将PCL转为qt点集
    std::vector<int> KNNBoundaryPointIndex;     //KNNAlphaShape提取边界点的索引
    QVector<QVector2D> knn_boundaryPoints;      //提取的边界轮廓二维点

    //kd快速临近点搜索
    pcl::KdTreeFLANN<pcl::PointXYZRGB>  kdtree;
    kdtree.setInputCloud(cloud);

    //容器储存了每个点邻近点的索引
    std::vector<std::vector<int>> each_point_nebot;

    std::vector<int> nebor_index;
    std::vector<float> nebor_dis;

    each_point_nebot.resize(cloud->points.size(), nebor_index);

    //容器each_point_nebot获得每个点邻近点索引的容器
    for (size_t i = 0; i < cloud->points.size(); i++)
    {
        if (kdtree.radiusSearch(cloud->points[i], neborRadius, nebor_index, nebor_dis) > 0)
        {
            each_point_nebot[i].swap(nebor_index);      //该容器第一个索引为自身
        }
    }

    //将点云的点集转为Qvector2D
    for (size_t i = 0; i < cloud->size(); i++)
    {
        QVector2D point;
        point.setX(cloud->points[i].x);
        point.setY(cloud->points[i].y);

        qt_points.push_back(point);
    }

    //判断该点是否处理过函数
    std::vector<bool> process;
    process.resize(cloud->size(), false);

    for (size_t i = 0; i < qt_points.size(); i++)
    {

        //从该点的邻近点开始
        for (size_t k = 1; k < each_point_nebot[i].size(); k++)
        {

            //判断该点是否计算过
            if (process[each_point_nebot[i][k]] == true)    continue;
            //process[each_point_nebot[i][k]] = true;

            // 跳过距离大于直径的情况
            if (qt_points[i].distanceToPoint(qt_points[each_point_nebot[i][k]]) > 2 * Alpha)        continue;

            // 两个圆心
            QVector2D c1, c2;

            // 线段中点
            QVector2D center = 0.5*(qt_points[i] + qt_points[each_point_nebot[i][k]]);

            // 方向向量 P1P2 = (x,y)
            QVector2D dir = qt_points[i] - qt_points[each_point_nebot[i][k]];

            // 垂直向量 n = (a,b)  a*dir.x+b*dir.y = 0; a = -(b*dir.y/dir.x)
            QVector2D normal;
            normal.setY(5);         // 因为未知数有两个，随便给y附一个值5。

            if (dir.x() != 0)
            {
                normal.setX(-(normal.y()*dir.y()) / dir.x());
            }
            else
            {
                // 如果方向平行于y轴
                normal.setX(1);
                normal.setY(0);
            }

            normal.normalize();   // 法向量单位化

            //获得圆心到P1P2两点连线的距离
            float len = sqrt(Alpha*Alpha - (0.25*dir.length()*dir.length()));

            //两个圆心的坐标
            c1 = center + len * normal;
            c2 = center - len * normal;

            // b1、b2记录是否在圆C1、C2中找到其他点。
            bool b1 = false, b2 = false;
            for (size_t m = 0; m < qt_points.size(); m++)
            {
                if (m == i || m == each_point_nebot[i][k])  continue;

                if (b1 != true && qt_points[m].distanceToPoint(c1) < Alpha) b1 = true;
                if (b2 != true && qt_points[m].distanceToPoint(c2) < Alpha) b2 = true;

                // 如果都有内部点，不必再继续检查了
                if (b1 == true && b2 == true)   break;
            }

            if (b1 != true || b2 != true)
            {
                //将边界点的索引存入容器
                KNNBoundaryPointIndex.push_back(i);
                KNNBoundaryPointIndex.push_back(each_point_nebot[i][k]);
            }
        }

        process[i] = true;
    }

    //将重复的点索引删除
    std::sort(KNNBoundaryPointIndex.begin(), KNNBoundaryPointIndex.end());
    KNNBoundaryPointIndex.erase(std::unique(KNNBoundaryPointIndex.begin(), KNNBoundaryPointIndex.end()), KNNBoundaryPointIndex.end());

    pcl::PointIndices indices;
    indices.indices.swap(KNNBoundaryPointIndex);
    pcl::copyPointCloud(*cloud, indices, *outcloud);
    //qDebug() << alphaShapeCloud->size();

    if (outcloud->size() != 0)
    {
        for (size_t i = 0; i < outcloud->size(); i++)
        {
            QVector2D point;
            point.setX(outcloud->points[i].x);
            point.setY(outcloud->points[i].y);

            knn_boundaryPoints.push_back(point);
        }
    }

    // 调试：保存 KNN Alpha Shape 提取边界
    // if(outcloud->size() !=0)      pcl::io::savePCDFileASCII("KNNAlphaShape.pcd", *outcloud);
}

//寻找点云中最远的两点的索引
// 2D point with original cloud index for convex hull computation
struct HullPoint {
    double x, y;
    int originalIndex;
};

// Cross product of vectors OA and OB (O is origin). Returns positive if O->A->B is counter-clockwise.
static inline double cross2D(const HullPoint &a, const HullPoint &b, const HullPoint &c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Squared 2D Euclidean distance (avoids sqrt for comparisons)
static inline double distSq2D(const HullPoint &a, const HullPoint &b)
{
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

/**
 * Find the two farthest-apart points in a 2D point cloud.
 *
 * Algorithm: Andrew's monotone chain convex hull (O(n log n)) followed by
 * rotating calipers (O(h)) on the hull. Total: O(n log n), where h ≤ n is
 * the hull size. For clouds with < 3 points, falls back to trivial brute-force.
 *
 * @param incloud           Input PCL point cloud (x,y,z,rgb)
 * @param headendPointIndex Output: indices of the two farthest points,
 *                          with headendPointIndex[0] ≤ headendPointIndex[1]
 */
void MyGraphicsView::findMaxDistancePointsofCloud(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & incloud, int headendPointIndex[2])
{
    size_t n = incloud->size();

    // ---------- Edge cases: 0, 1, or 2 points ----------
    if (n == 0) {
        headendPointIndex[0] = 0;
        headendPointIndex[1] = 0;
        return;
    }
    if (n == 1) {
        headendPointIndex[0] = 0;
        headendPointIndex[1] = 0;
        return;
    }
    if (n == 2) {
        headendPointIndex[0] = 0;
        headendPointIndex[1] = 1;
        return;
    }

    // ---------- Step 1: Build 2D point array with original indices ----------
    std::vector<HullPoint> pts(n);
    for (size_t i = 0; i < n; i++) {
        pts[i].x = incloud->points[i].x;
        pts[i].y = incloud->points[i].y;
        pts[i].originalIndex = static_cast<int>(i);
    }

    // ---------- Step 2: Andrew's monotone chain convex hull ----------
    // Sort by x, then y
    std::sort(pts.begin(), pts.end(), [](const HullPoint &a, const HullPoint &b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });

    // Build lower hull
    std::vector<HullPoint> hull;
    for (size_t i = 0; i < n; i++) {
        while (hull.size() >= 2 && cross2D(hull[hull.size() - 2], hull[hull.size() - 1], pts[i]) <= 0)
            hull.pop_back();
        hull.push_back(pts[i]);
    }

    // Build upper hull
    size_t lowerSize = hull.size();
    for (size_t i = n; i-- > 0; ) {
        while (hull.size() > lowerSize && cross2D(hull[hull.size() - 2], hull[hull.size() - 1], pts[i]) <= 0)
            hull.pop_back();
        hull.push_back(pts[i]);
    }

    // Remove duplicate last point (same as first)
    hull.pop_back();

    // If hull degenerated to a line segment (all points collinear), just pick the two extremes
    if (hull.size() < 2) {
        headendPointIndex[0] = pts[0].originalIndex;
        headendPointIndex[1] = pts[n - 1].originalIndex;
        if (headendPointIndex[0] > headendPointIndex[1])
            std::swap(headendPointIndex[0], headendPointIndex[1]);
        return;
    }

    // ---------- Step 3: Rotating calipers ----------
    int h = static_cast<int>(hull.size());
    int bestI = 0, bestJ = 0;
    double bestDistSq = 0.0;

    // Find initial antipodal point (farthest from hull[0])
    int j = 1;
    while (cross2D(hull[0], hull[1], hull[(j + 1) % h]) > cross2D(hull[0], hull[1], hull[j]))
        j = (j + 1) % h;

    // Rotate through all antipodal pairs
    for (int i = 0; i < h; i++) {
        int nextI = (i + 1) % h;
        // Advance j while the next hull edge would produce a larger cross product (area)
        while (cross2D(hull[i], hull[nextI], hull[(j + 1) % h]) > cross2D(hull[i], hull[nextI], hull[j]))
            j = (j + 1) % h;

        // Check distance from hull[i] to hull[j]
        double dSq = distSq2D(hull[i], hull[j]);
        if (dSq > bestDistSq) {
            bestDistSq = dSq;
            bestI = i;
            bestJ = j;
        }

        // Also check distance from hull[nextI] to hull[j]
        dSq = distSq2D(hull[nextI], hull[j]);
        if (dSq > bestDistSq) {
            bestDistSq = dSq;
            bestI = nextI;
            bestJ = j;
        }
    }

    // ---------- Step 4: Map hull indices back to original cloud indices ----------
    headendPointIndex[0] = hull[bestI].originalIndex;
    headendPointIndex[1] = hull[bestJ].originalIndex;
    if (headendPointIndex[0] > headendPointIndex[1])
        std::swap(headendPointIndex[0], headendPointIndex[1]);
}

/*
//将无序点云进行有序排列
*\输入无序点云
*\输入最近邻搜索K
*\输出有序点云
*/
void MyGraphicsView::orderSoetCloud(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & incloud, int neborNumbers, pcl::PointCloud<pcl::PointXYZRGB>::Ptr & outcloud)
{
    //每个点是否遍历过容器
    std::vector<bool> process;
    process.resize(incloud->size(), false);

    //kd快速临近点搜索
    pcl::KdTreeFLANN<pcl::PointXYZRGB>  kdtree;
    kdtree.setInputCloud(incloud);

    //容器储存了每个点邻近点的索引
    std::vector<std::vector<int>> each_point_nebot;

    std::vector<int> nebor_index;
    std::vector<float> nebor_dis;

    each_point_nebot.resize(incloud->points.size(), nebor_index);

    //容器each_point_nebot获得每个点邻近点索引的容器
    for (size_t i = 0; i < incloud->size(); i++)
    {
        if (kdtree.nearestKSearch(incloud->points[i], neborNumbers, nebor_index, nebor_dis) > 0)
        {
            each_point_nebot[i].swap(nebor_index);
        }
    }

    pcl::PointIndices pointIndex;
    std::queue<int> seed;        //种子点，以该点开始进行生长

    //寻找到端点的索引
    int headendPointIndex[2];
    findMaxDistancePointsofCloud(incloud, headendPointIndex);
    qDebug() << "headendPointIndex" << headendPointIndex[0] << headendPointIndex[1];

    seed.push(headendPointIndex[0]);
    pointIndex.indices.push_back(headendPointIndex[0]);
    process[headendPointIndex[0]] = true;

    while (!seed.empty())
    {
        int current_seed_index = seed.front();
        for (size_t i = 1; i < each_point_nebot[current_seed_index].size(); i++)
        {
            if (process[each_point_nebot[current_seed_index][i]] == true)   continue;
            seed.push(each_point_nebot[current_seed_index][i]);
            pointIndex.indices.push_back(each_point_nebot[current_seed_index][i]);
            process[each_point_nebot[current_seed_index][i]] = true;
            break;
        }
        seed.pop();
    }
    qDebug() << pointIndex.indices.size();
    for (size_t i = 0; i < pointIndex.indices.size(); i++)
    {
        outcloud->push_back(incloud->points[pointIndex.indices[i]]);
    }

    // 调试：保存有序排列点云
    // pcl::io::savePCDFileASCII("ordercloud.pcd", *outcloud);
}

//根据点云和投影类型将PCL按照不同比例尺转为QT点集
void MyGraphicsView::PCLCloud2QTPoints(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& incloud, ProjectType protype, QVector<QPointF> &points, QPointF offset)
{
    //根据传入的点云和投影类型转为QT点
    //比例尺 默认 1:50
    if (!incloud)
    {
        return;
    }
    points.reserve(points.size() + static_cast<int>(incloud->size()));

    switch (protype)
    {
    case(XOY):
    {
        for (size_t i = 0; i < incloud->size(); i++)
        {
            QPointF point;
            point.setX(incloud->at(i).x * offset.x());
            point.setY(-incloud->at(i).y * offset.y());
            points.push_back(point);
        }
    }
    break;

    case(XOZ):
    {
        for (size_t i = 0; i < incloud->size(); i++)
        {
            QPointF point;
            point.setX(incloud->at(i).x * offset.x());
            point.setY(-incloud->at(i).z * offset.y());
            points.push_back(point);
        }
    }
    break;

    case(YOZ):
    {
        for (size_t i = 0; i < incloud->size(); i++)
        {
            QPointF point;
            point.setX(incloud->at(i).z * offset.x());
            point.setY(-incloud->at(i).y * offset.y());
            points.push_back(point);
        }
    }
    break;

    case(OLXOY):
    {
        for (size_t i = 0; i < incloud->size(); i++)
        {
            QPointF point;
            point.setX(incloud->at(i).x * offset.x());
            point.setY(-incloud->at(i).y * offset.y());
            points.push_back(point);
        }
    }
    break;

    default:
        break;
    }

}

MyItem* MyGraphicsView::primaryProjectionItem(ProjectType type) const
{
    if (!scene)
        return nullptr;
    for (QGraphicsItem* graphicsItem : scene->items(Qt::DescendingOrder))
    {
        auto* item = dynamic_cast<MyItem*>(graphicsItem);
        if (item && !item->parentItem() && item->projectionType() == type)
            return item;
    }
    return nullptr;
}

MyItem* MyGraphicsView::sectionCutLineItem(ProjectType profileType) const
{
    if (!scene || (profileType != XOZ && profileType != YOZ))
        return nullptr;
    for (QGraphicsItem* graphicsItem : scene->items(Qt::DescendingOrder))
    {
        auto* item = dynamic_cast<MyItem*>(graphicsItem);
        const QVariant sectionValue = item
            ? item->data(SectionPlaneDataKey) : QVariant();
        if (item && !item->parentItem()
            && item->projectionType() == XOYLine
            && sectionValue.isValid()
            && sectionValue.toInt() == static_cast<int>(profileType))
        {
            return item;
        }
    }
    return nullptr;
}

void MyGraphicsView::layoutOrthographicProjectionDrawings()
{
    if (!scene)
        return;

    MyItem* overlook = primaryProjectionItem(OLXOY);
    MyItem* xoz = primaryProjectionItem(XOZ);
    MyItem* yoz = primaryProjectionItem(YOZ);
    MyItem* xozSection = sectionCutLineItem(XOZ);
    MyItem* yozSection = sectionCutLineItem(YOZ);
    QVector<MyItem*> drawings;
    const auto prepareDrawing = [&drawings](MyItem* item, int degrees) {
        if (!item)
            return;
        item->setTransformOriginPoint(QPointF());
        item->setRotation(degrees);
        item->setPos(QPointF());
        drawings.append(item);
    };
    prepareDrawing(overlook, orthographicRotationDegrees_);
    prepareDrawing(xoz, xozRotationDegrees_);
    prepareDrawing(yoz, yozRotationDegrees_);
    for (MyItem* line : {xozSection, yozSection})
    {
        if (!line)
            continue;
        line->setTransformOriginPoint(QPointF());
        // Section lines belong to the plan, not to either independently
        // rotated profile, so they must always follow the plan direction.
        line->setRotation(orthographicRotationDegrees_);
        line->setPos(QPointF());
    }

    if (drawings.isEmpty() && !xozSection && !yozSection)
    {
        scene->setSceneRect(DefaultDraftingSceneRect);
        return;
    }

    const qreal horizontalGap = qMax<qreal>(
        1.0, metricPixelsPerMillimeter.x()
            * OrthographicProjectionGapMillimeters);
    const qreal verticalGap = qMax<qreal>(
        1.0, metricPixelsPerMillimeter.y()
            * OrthographicProjectionGapMillimeters);
    const QPointF layoutAnchor = DefaultDraftingSceneRect.center();

    if (overlook)
    {
        // 俯视图始终是稳定锚点。切割线与俯视图使用完全相同的
        // 坐标、旋转和位移，因此无论比例尺或方向如何变化都叠合在
        // 真实的剖切位置上。
        const QRectF overlookAtOrigin = overlook->sceneBoundingRect();
        overlook->setPos(layoutAnchor - overlookAtOrigin.center());
        for (MyItem* line : {xozSection, yozSection})
        {
            if (line)
                line->setPos(overlook->pos());
        }

        const QRectF overlookBounds = overlook->sceneBoundingRect();
        if (yoz)
        {
            const QRectF yozBounds = yoz->sceneBoundingRect();
            const qreal alignedCenterX = yozSection
                ? yozSection->sceneBoundingRect().center().x()
                : overlookBounds.center().x();
            // YOZ 剖面固定在俯视图正上方，其中心与俯视图中的
            // YOZ 剖面切割线对齐。
            yoz->setPos(
                alignedCenterX - yozBounds.center().x(),
                overlookBounds.top() - verticalGap - yozBounds.bottom());
        }
        if (xoz)
        {
            const QRectF xozBounds = xoz->sceneBoundingRect();
            const qreal alignedCenterY = xozSection
                ? xozSection->sceneBoundingRect().center().y()
                : overlookBounds.center().y();
            // XOZ 剖面固定在俯视图正右方，其中心与俯视图中的
            // XOZ 剖面切割线对齐。
            xoz->setPos(
                overlookBounds.right() + horizontalGap - xozBounds.left(),
                alignedCenterY - xozBounds.center().y());
        }

        // 极端范围下两个剖面可能在右上角相交。保持各自切割线
        // 对齐方向不变，只把右侧 XOZ 继续向右推开。
        if (xoz && yoz
            && xoz->sceneBoundingRect().intersects(
                yoz->sceneBoundingRect()))
        {
            const qreal shift = yoz->sceneBoundingRect().right()
                + horizontalGap - xoz->sceneBoundingRect().left();
            if (shift > 0.0)
                xoz->moveBy(shift, 0.0);
        }
    }
    else
    {
        // 正常交互会阻止在俯视图之前生成剖面。这里仅为旧项目或
        // 低层测试保留安全排版，避免已有图元重叠或丢失。
        qreal cursor = 0.0;
        for (MyItem* item : drawings)
        {
            const QRectF bounds = item->sceneBoundingRect();
            item->setPos(cursor - bounds.left(), -bounds.center().y());
            cursor = item->sceneBoundingRect().right() + horizontalGap;
        }

        // Before the plan exists, keep the temporary profile row visible.
        // Once the plan is generated the branch above restores the canonical
        // plan-anchored layout regardless of generation order.
        const QRectF temporaryBounds = projectionLayoutSceneBounds();
        const QPointF temporaryShift = DefaultDraftingSceneRect.center()
            - temporaryBounds.center();
        for (MyItem* item : drawings)
            item->moveBy(temporaryShift.x(), temporaryShift.y());
    }

    QRectF layoutBounds = projectionLayoutSceneBounds();
    const qreal horizontalMargin = qMax<qreal>(
        1.0, metricPixelsPerMillimeter.x()
            * OrthographicCanvasMarginMillimeters);
    const qreal verticalMargin = qMax<qreal>(
        1.0, metricPixelsPerMillimeter.y()
            * OrthographicCanvasMarginMillimeters);
    const QRectF requiredScene = layoutBounds.adjusted(
        -horizontalMargin, -verticalMargin,
        horizontalMargin, verticalMargin);
    // 为保持制图比例尺，内容过大时扩展画布，绝不把三向成果缩小塞入。
    scene->setSceneRect(DefaultDraftingSceneRect.united(requiredScene));
}

//比例尺改变后重新绘制
void MyGraphicsView::ReDraw(QPointF offset)
{
    QList<QGraphicsItem*> all_items = scene->items();

    qDebug()<< "items" << all_items.size();

    for (size_t i = 0; i < all_items.size(); i++)
    {
        if (all_items.at(i)->data(ItemCloud).isValid())
        {
            //获得scene中存在有效的item的有序点云和投影类型
            MyOrderCloudType item_ordercloud = all_items.at(i)->data(ItemCloud).value<MyOrderCloudType>();

            //将有序点云按照比例尺转为QT点集
            QVector<QPointF> item_points;
            PCLCloud2QTPoints(item_ordercloud.orderCloud, item_ordercloud.project_type, item_points, QPointF(offset.x(), offset.y()));

            //剖面线绘制
            if (item_points.size() >= 2 && item_points.size() < 4)
            {
                //计算两个点的直线向量
                QVector2D line_dir = QVector2D(item_points.back().x() - item_points.front().x(), item_points.back().y() - item_points.front().y());
                line_dir.normalize();

                QPointF P1, P2, P3, P4;
                P2 = QPointF(item_points.front().x(), item_points.front().y());
                P3 = QPointF(item_points.back().x(), item_points.back().y());
                P1 = QPointF(P2.x() - line_dir.x() * 20, P2.y() - line_dir.y() * 20);
                P4 = QPointF(P3.x() + line_dir.x() * 20, P3.y() + line_dir.y() * 20);

                item_points.clear();
                item_points.push_back(P1);
                item_points.push_back(P2);
                item_points.push_back(P3);
                item_points.push_back(P4);
            }

            if (auto* item = dynamic_cast<MyItem*>(all_items.at(i)))
                item->setPoints(item_points);
        }
    }

    rescaleMeasurementItems();
    rescaleTerrainItems();
    layoutOrthographicProjectionDrawings();

}

//弹出出图模板对话框
void MyGraphicsView::setDlgPlotTab()
{
    dlgPlotTab = new QDlgPlotTab(this);
    dlgPlotTab->setAttribute(Qt::WA_DeleteOnClose);
    Qt::WindowFlags flags = dlgPlotTab->windowFlags();
    dlgPlotTab->setWindowFlags(flags | Qt::WindowStaysOnTopHint);
    dlgPlotTab->show();
    //获得制图人，比例尺，日期文字
    //designerText = dlgPlotTab->getDesignerPTE();
    //scaleText = dlgPlotTab->getScalePTE();
    //dataText = dlgPlotTab->getDataPTE();
}

//绘制出图模板表格
void MyGraphicsView::drawFormTable()
{
    FormTabulationItem * item = new FormTabulationItem(designerText, dataText, scaleText, this);
    addItemWithHistory(item, tr("添加制图信息"));
    scene->clearSelection();

    item->setSelected(true);
    emit sendStrFromGraphicView2Console("已添加制图信息！");
}

//获得制图人，比例尺，日期文字
void MyGraphicsView::getText()
{
    designerText = dlgPlotTab->getDesignerPTE();
    scaleText = dlgPlotTab->getScalePTE();
    dataText = dlgPlotTab->getDataPTE();
}

//设置出图表对话框为空指针
void MyGraphicsView::setDlgPlotTabNull()
{
    this->dlgPlotTab = NULL;
}

//计算点到线的距离
double MyGraphicsView::point2LineDist(const DPPoint& p1, const DPPoint& p2, const DPPoint& p3)
{
    double dist;
    Eigen::Vector4f line_dir = { p1.x - p2.x,p1.y - p2.y,p1.z - p2.z ,0.0 };
    Eigen::Vector4f line_pt = { p1.x,p1.y,p1.z,0.0 };
    Eigen::Vector4f point3 = { p3.x,p3.y,p3.z,0.0 };
    dist = pcl::sqrPointToLineDistance(point3, line_pt, line_dir);              //PCL点到线的距离

    return sqrt(dist);
}

//计算点集中的最大值及其索引
std::pair<double, int> MyGraphicsView::getMaxDistAndIndex(std::vector<DPPoint>& Points, int begin, int end)
{
    double maxDistance = 0.0;
    int maxIndex = begin;
    for (int i = begin; i <= end; i++)
    {
        double dis = point2LineDist(Points[begin], Points[end], Points[i]);
        if (dis > maxDistance)
        {
            maxDistance = dis;
            maxIndex = i;
        }
    }
    return std::make_pair(maxDistance, maxIndex);
}

/*//DP算法简化线条点
\*输入点集，计算后获得DP简化后的点集
\*输入起始点的索引
\*输入终点的索引
\*输入阈值D
*/
void MyGraphicsView::computeDP(std::vector<DPPoint>& Points, int begin, int end, double threshold)
{
    std::stack<std::pair<int, int>> stk;
    stk.push({begin, end});
    while (!stk.empty()) {
        auto [b, e] = stk.top(); stk.pop();
        if (e - b <= 1) continue;
        auto maxDistAndIdx = getMaxDistAndIndex(Points, b, e);
        if (maxDistAndIdx.first > threshold)
        {
            int mid = maxDistAndIdx.second;
            stk.push({b, mid});
            stk.push({mid, e});
        }
        else
        {
            for (int i = b + 1; i < e; i++)
            {
                Points[i].isRemoved = true;
            }
        }
    }
}

void MyGraphicsView::slotOn_actPoints_triggered()
{
    QGraphicsEllipseItem   *item = new QGraphicsEllipseItem(-50, -30, 100, 60);
    item->setFlags(QGraphicsItem::ItemIsFocusable | QGraphicsItem::ItemIsMovable |
        QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusScope);
    item->setBrush(QBrush(Qt::blue)); //填充颜色
    addItemWithHistory(item, tr("添加图元"));
    scene->clearSelection();
    item->setSelected(true);
}

//切割的点云投影连线
void MyGraphicsView::slotOn_actProjLine_triggered()
{
    if (!project_cloud || project_cloud->size() < 10)
    {
        QMessageBox::warning(
            this, tr("自动绘图"),
            tr("请先生成至少 10 个点的投影预览。"));
        return;
    }
    const int drawingCountBefore = projectionDrawingCount();
    const famp::projection::Plane drawingPlane = projectionPlane_;
    /*if (project_type == XOY)
    {
        qDebug() << "XOY";
    }
    else if (project_type == XOZ)
    {
        qDebug() << "XOZ";
        drawXOZ();
    }
    else if (project_type == YOZ)
    {
        qDebug() << "YOZ";
        drawYOZ();
    }
    else if (project_type == OLXOY)
    {
        qDebug() << "OLXOY";
    }
    else if (project_type == NONE)
    {
    }*/

    switch (project_type)
    {
    case(XOY):      //XOY面投影连线
    {
        qDebug() << "XOY";
        int isdrawLine = dlgDrawXOYLine();      //是否绘制剖面线
        if (isdrawLine == 1)
        {
            //qDebug() << "绘制剖面线";
            getScaleOffset(this->deltaOffset);
            drawXOYLine(deltaOffset);
            emit sendDlgClipVisible(true);      //将VTK的平面裁剪对话框显示
            emit sendStrFromGraphicView2Console("已绘制剖面线！");
        }
        else if(isdrawLine == 0)
        {
            //qDebug() << "不绘制剖面线";
            getScaleOffset(this->deltaOffset);
            drawXOY(deltaOffset);
            emit sendDlgClipVisible(true);
            emit sendStrFromGraphicView2Console("投影到XOY面的点连线绘制完成！");
        }
        else if (isdrawLine == -1)
        {
            qDebug() << "取消";
            emit sendDlgClipVisible(true);
            return;
        }
    }
    break;

    case(XOZ):      //XOZ面投影连线
    {
        qDebug() << "XOZ";
        getScaleOffset(this->deltaOffset);
        drawXOZ(deltaOffset);
    }
    break;

    case(YOZ):      //YOZ面投影连线
    {
        qDebug() << "YOZ";
        getScaleOffset(this->deltaOffset);
        drawYOZ(deltaOffset);
    }
    break;

    case(OLXOY):    //俯视XOY面投影连线
    {
        qDebug() << "OLXOY";
        auto start = std::chrono::steady_clock::now();
        getScaleOffset(this->deltaOffset);
        drawOverLookXOY(deltaOffset);
        auto end = std::chrono::steady_clock::now();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
        emit sendStrFromGraphicView2Console(
            tr("俯视投影到 XOY 面的点连线绘制完成！用时 %1 秒")
                .arg(static_cast<qlonglong>(secs)));
    }
    break;

    case(NONE):     //点云数太少,不能连线
    {
        qDebug() << "NONE";
        QMessageBox::warning(this, "投影连线", "点云数太少(<10)！");
    }
    default:
        break;
    }

    if (projectionDrawingCount() > drawingCountBefore)
        emit projectionDrawingCreated(drawingPlane);
}

//删除Item
void MyGraphicsView::slotOn_actDeleteItem_triggered()
{
    const auto handles = handlesForItems(scene->selectedItems());
    if (!handles.isEmpty())
    {
        history->push(famp::graphics::makeRemoveItemsCommand(
            scene, handles, tr("删除图元")));
        layoutOrthographicProjectionDrawings();
    }
}

//清空
void MyGraphicsView::slotOn_actClearScene_triggered()
{
    const auto handles = handlesForItems(scene->items());
    if (!handles.isEmpty())
    {
        history->push(famp::graphics::makeRemoveItemsCommand(
            scene, handles, tr("清空画布")));
        layoutOrthographicProjectionDrawings();
    }
}

//组合按钮
void MyGraphicsView::slotOn_actGroup_triggered()
{
    const QList<QGraphicsItem*> selected = scene->selectedItems();
    if (selected.size() > 1)
    {
        auto* group = new QGraphicsItemGroup;
        group->setFlags(QGraphicsItem::ItemIsMovable
            | QGraphicsItem::ItemIsSelectable
            | QGraphicsItem::ItemIsFocusable);
        group->setZValue(++frontZ);
        history->push(famp::graphics::makeGroupItemsCommand(
            scene,
            handleForItem(group),
            handlesForItems(selected),
            tr("组合图元")));
    }
}

//打散按钮
void MyGraphicsView::slotOn_actBreak_triggered()
{
    int selectedCounts = this->scene->selectedItems().count();  //Scene中选中的个数
    if (selectedCounts ==1)
    {
        auto* group = qgraphicsitem_cast<QGraphicsItemGroup*>(
            scene->selectedItems().at(0));
        if (!group)
        {
            emit sendStrFromGraphicView2Console(tr("当前选中图元不是组合。"));
            return;
        }
        history->push(famp::graphics::makeUngroupItemsCommand(
            scene,
            handleForItem(group),
            handlesForItems(group->childItems()),
            tr("打散图元")));
    }
}

//向上移动
void MyGraphicsView::slotOn_actMoveUp_triggered()
{
    moveSelectedItemsBy(QPointF(0.0, -10.0), tr("上移图元"));
}

//向下移动
void MyGraphicsView::slotOn_actMoveDown_triggered()
{
    moveSelectedItemsBy(QPointF(0.0, 10.0), tr("下移图元"));
}

//向左移动
void MyGraphicsView::slotOn_actMoveLeft_triggered()
{
    moveSelectedItemsBy(QPointF(-10.0, 0.0), tr("左移图元"));
}

//向右移动
void MyGraphicsView::slotOn_actMoveRight_triggered()
{
    moveSelectedItemsBy(QPointF(10.0, 0.0), tr("右移图元"));
}

void MyGraphicsView::rotateSelectedItems(qreal deltaDegrees)
{
    const auto before = selectedItemStates();
    const int rotatedCount = famp::graphics::rotateItems(
        scene->selectedItems(), deltaDegrees);
    if (rotatedCount > 0)
    {
        emit sendStrFromGraphicView2Console(
            QStringLiteral("已旋转 %1 个图元 %2°")
                .arg(rotatedCount)
                .arg(deltaDegrees));
        pushTransformChange(before, tr("旋转图元"));
    }
}

void MyGraphicsView::slotOn_actRotateLeft_triggered()
{
    rotateSelectedItems(-famp::graphics::RotationStepDegrees);
}

void MyGraphicsView::slotOn_actRotateRight_triggered()
{
    rotateSelectedItems(famp::graphics::RotationStepDegrees);
}

//前置按钮
void MyGraphicsView::slotOn_actEditFront_triggered()
{
    int cnt = scene->selectedItems().count();
    if (cnt>0)
    {
        const auto before = selectedItemStates();
        QGraphicsItem * item = scene->selectedItems().at(0);
        item->setZValue(++frontZ);
        pushTransformChange(before, tr("前置图层"));
        emit sendStrFromGraphicView2Console("已将当前选中图层前置成功！");
    }
}

//后置按钮
void MyGraphicsView::slotOn_actEditBack_triggered()
{
    int cnt = scene->selectedItems().count();
    if (cnt > 0)
    {
        const auto before = selectedItemStates();
        QGraphicsItem * item = scene->selectedItems().at(0);
        item->setZValue(--backZ);
        pushTransformChange(before, tr("后置图层"));
        emit sendStrFromGraphicView2Console("已将当前选中图层后置成功！");
    }
}

//保存按钮
void MyGraphicsView::slotOn_actSave_triggered()
{
    QDialog settingsDialog(this);
    settingsDialog.setObjectName(QStringLiteral("professionalExportDialog"));
    settingsDialog.setWindowTitle(tr("专业成果导出"));
    QFormLayout layout(&settingsDialog);

    QComboBox formatCombo(&settingsDialog);
    formatCombo.addItem(tr("PDF 矢量文档"),
                        static_cast<int>(famp::exporting::Format::Pdf));
    formatCombo.addItem(tr("PNG 高分辨率图像"),
                        static_cast<int>(famp::exporting::Format::Png));
    formatCombo.addItem(tr("BMP 无损图像"),
                        static_cast<int>(famp::exporting::Format::Bmp));
    formatCombo.addItem(tr("SVG 矢量文档"),
                        static_cast<int>(famp::exporting::Format::Svg));

    QComboBox paperCombo(&settingsDialog);
    paperCombo.setObjectName(QStringLiteral("exportPaperCombo"));
    paperCombo.addItem(QStringLiteral("A4"),
                       static_cast<int>(famp::exporting::PaperSize::A4));
    paperCombo.addItem(QStringLiteral("A3"),
                       static_cast<int>(famp::exporting::PaperSize::A3));
    paperCombo.addItem(tr("自定义"),
                       static_cast<int>(famp::exporting::PaperSize::Custom));

    QDoubleSpinBox customWidthSpin(&settingsDialog);
    customWidthSpin.setRange(50.0, 2000.0);
    customWidthSpin.setDecimals(1);
    customWidthSpin.setSuffix(tr(" mm"));
    customWidthSpin.setValue(210.0);
    customWidthSpin.setEnabled(false);
    QDoubleSpinBox customHeightSpin(&settingsDialog);
    customHeightSpin.setRange(50.0, 2000.0);
    customHeightSpin.setDecimals(1);
    customHeightSpin.setSuffix(tr(" mm"));
    customHeightSpin.setValue(297.0);
    customHeightSpin.setEnabled(false);
    connect(&paperCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &settingsDialog,
            [&paperCombo, &customWidthSpin, &customHeightSpin](int) {
                const bool custom = static_cast<famp::exporting::PaperSize>(
                    paperCombo.currentData().toInt())
                    == famp::exporting::PaperSize::Custom;
                customWidthSpin.setEnabled(custom);
                customHeightSpin.setEnabled(custom);
            });

    QComboBox orientationCombo(&settingsDialog);
    orientationCombo.addItem(tr("横向"),
                             static_cast<int>(famp::exporting::Orientation::Landscape));
    orientationCombo.addItem(tr("纵向"),
                             static_cast<int>(famp::exporting::Orientation::Portrait));

    QComboBox dpiCombo(&settingsDialog);
    dpiCombo.addItem(QStringLiteral("150 DPI"), 150);
    dpiCombo.addItem(QStringLiteral("254 DPI（1 mm = 10 px）"), 254);
    dpiCombo.addItem(QStringLiteral("300 DPI"), 300);
    dpiCombo.addItem(QStringLiteral("508 DPI（1 mm = 20 px）"), 508);
    dpiCombo.addItem(QStringLiteral("600 DPI"), 600);
    dpiCombo.setCurrentIndex(dpiCombo.findData(254));

    QCheckBox metricGridCheck(
        tr("包含 1 mm 米格纸（5 mm / 10 mm 加粗）"),
        &settingsDialog);
    metricGridCheck.setChecked(true);
    metricGridCheck.setToolTip(
        tr("PDF/SVG 使用矢量毫米坐标；PNG/BMP 自动采用 254 或 508 DPI，"
           "确保每毫米对应整数像素。"));

    const auto enforceExactRasterGridDpi = [&]() {
        const auto format = static_cast<famp::exporting::Format>(
            formatCombo.currentData().toInt());
        const bool raster = format == famp::exporting::Format::Png
            || format == famp::exporting::Format::Bmp;
        const int dpi = dpiCombo.currentData().toInt();
        if (metricGridCheck.isChecked() && raster
            && dpi != 254 && dpi != 508)
        {
            dpiCombo.setCurrentIndex(dpiCombo.findData(254));
        }
    };
    connect(&formatCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &settingsDialog,
            [enforceExactRasterGridDpi](int) {
                enforceExactRasterGridDpi();
            });
    connect(&dpiCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &settingsDialog,
            [enforceExactRasterGridDpi](int) {
                enforceExactRasterGridDpi();
            });
    connect(&metricGridCheck, &QCheckBox::toggled,
            &settingsDialog,
            [enforceExactRasterGridDpi](bool) {
                enforceExactRasterGridDpi();
            });

    QComboBox scaleModeCombo(&settingsDialog);
    scaleModeCombo.setObjectName(QStringLiteral("exportScaleModeCombo"));
    scaleModeCombo.addItem(
        tr("保持当前制图比例尺（实尺，不缩放）"),
        static_cast<int>(famp::exporting::ScaleMode::PreservePhysicalScale));
    scaleModeCombo.addItem(
        tr("自动适合页面（会缩放成果）"),
        static_cast<int>(famp::exporting::ScaleMode::FitToPage));
    scaleModeCombo.setToolTip(
        tr("纸质成果要求比例尺准确时必须选择“实尺，不缩放”。"));

    QLabel exactPrintHint(
        tr("实尺出图：选择 A4/A3、勾选 1 mm 米格纸并保持“实尺，不缩放”；"
           "在打印机属性中再选择“实际大小/100%”，关闭“适合页面/缩小超大页面”。"),
        &settingsDialog);
    exactPrintHint.setObjectName(QStringLiteral("exactPrintHint"));
    exactPrintHint.setWordWrap(true);
    exactPrintHint.setStyleSheet(
        QStringLiteral("QLabel { color: #8a3b12; padding: 4px; }"));

    layout.addRow(tr("格式"), &formatCombo);
    layout.addRow(tr("纸张"), &paperCombo);
    layout.addRow(tr("自定义宽度"), &customWidthSpin);
    layout.addRow(tr("自定义高度"), &customHeightSpin);
    layout.addRow(tr("方向"), &orientationCombo);
    layout.addRow(tr("缩放"), &scaleModeCombo);
    layout.addRow(tr("分辨率"), &dpiCombo);
    layout.addRow(tr("米格纸"), &metricGridCheck);
    layout.addRow(tr("打印要求"), &exactPrintHint);

    QDialogButtonBox buttons(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel,
        &settingsDialog);
    buttons.button(QDialogButtonBox::Save)->setText(tr("选择路径…"));
    QPushButton* previewButton = buttons.addButton(
        tr("A4/A3 打印预览"), QDialogButtonBox::ActionRole);
    previewButton->setObjectName(QStringLiteral("printPreviewButton"));
    connect(&buttons, &QDialogButtonBox::accepted,
            &settingsDialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected,
            &settingsDialog, &QDialog::reject);

    const auto selectedOptions = [&]() {
        famp::exporting::Options options;
        options.format = static_cast<famp::exporting::Format>(
            formatCombo.currentData().toInt());
        options.paperSize = static_cast<famp::exporting::PaperSize>(
            paperCombo.currentData().toInt());
        options.customPageWidthMillimeters = customWidthSpin.value();
        options.customPageHeightMillimeters = customHeightSpin.value();
        options.orientation = static_cast<famp::exporting::Orientation>(
            orientationCombo.currentData().toInt());
        options.scaleMode = static_cast<famp::exporting::ScaleMode>(
            scaleModeCombo.currentData().toInt());
        options.dotsPerInch = dpiCombo.currentData().toInt();
        options.sceneUnitsPerMillimeterX = metricPixelsPerMillimeter.x();
        options.sceneUnitsPerMillimeterY = metricPixelsPerMillimeter.y();
        options.includeMetricGrid = metricGridCheck.isChecked();
        options.creator = QStringLiteral("FAMP %1").arg(
            QString::fromLatin1(famp::Version));
        options.title = tr("FAMP 专业成果预览");
        return options;
    };

    connect(previewButton, &QPushButton::clicked, &settingsDialog, [&]() {
        QPrinter printer(QPrinter::HighResolution);
        QPrintPreviewDialog preview(&printer, &settingsDialog);
        QString previewError;
        const auto options = selectedOptions();
        QString paperName;
        switch (options.paperSize)
        {
        case famp::exporting::PaperSize::A3:
            paperName = QStringLiteral("A3");
            break;
        case famp::exporting::PaperSize::Custom:
            paperName = tr("自定义纸张");
            break;
        case famp::exporting::PaperSize::A4:
        default:
            paperName = QStringLiteral("A4");
            break;
        }
        const QString orientationName =
            options.orientation == famp::exporting::Orientation::Landscape
            ? tr("横向") : tr("纵向");
        preview.setWindowTitle(
            tr("%1 %2实尺打印预览").arg(paperName, orientationName));
        connect(&preview, &QPrintPreviewDialog::paintRequested,
                &preview, [&](QPrinter* device) {
                    if (previewError.isEmpty())
                        famp::exporting::printScene(
                            scene, device, options, &previewError);
                });
        preview.exec();
        if (!previewError.isEmpty())
            QMessageBox::warning(
                &settingsDialog, tr("预览失败"), previewError);
    });
    layout.addRow(&buttons);
    if (settingsDialog.exec() != QDialog::Accepted)
        return;

    famp::exporting::Options options = selectedOptions();

    QString filter;
    switch (options.format)
    {
    case famp::exporting::Format::Pdf:
        filter = tr("PDF 文档 (*.pdf)");
        break;
    case famp::exporting::Format::Bmp:
        filter = tr("BMP 图像 (*.bmp)");
        break;
    case famp::exporting::Format::Svg:
        filter = tr("SVG 矢量文档 (*.svg)");
        break;
    case famp::exporting::Format::Png:
    default:
        filter = tr("PNG 图像 (*.png)");
        break;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("导出专业成果"),
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("FAMP-export")),
        filter);
    if (filePath.isEmpty())
        return;

    filePath = famp::exporting::pathWithFormatSuffix(filePath, options.format);
    options.title = QFileInfo(filePath).completeBaseName();
    QString saveError;
    if (!famp::exporting::exportScene(
            scene, filePath, options, &saveError))
    {
        QMessageBox::warning(this, tr("导出失败"), saveError);
        emit sendStrFromGraphicView2Console(tr("导出失败：") + saveError);
        return;
    }

    emit sendStrFromGraphicView2Console(
        tr("专业成果已导出到：") + filePath);
}

//指北针按钮
void MyGraphicsView::slotOn_actCompass_triggered()
{
    //QImage * img_compass = new QImage();
    //img_compass->load(":/images/images/compassMap.bmp");

    CompassItem * item = new CompassItem();
    addItemWithHistory(item, tr("添加指北针"));
    scene->clearSelection();

    item->setSelected(true);
    emit sendStrFromGraphicView2Console("已添加指北针！");
}

//添加文字按钮
void MyGraphicsView::slotOn_actText_triggered()
{
    QString str = QInputDialog::getText(this, "添加文字", "请输入文字");
    if (str.isEmpty())      return;

    QGraphicsTextItem *item = new QGraphicsTextItem(str);

    QFont font = this->font();
    font.setPointSize(15);
    font.setBold(true);
    item->setFont(font);
    item->setFlags(QGraphicsItem::ItemIsMovable
        | QGraphicsItem::ItemIsSelectable
        | QGraphicsItem::ItemIsFocusable);

    item->setPos(140, 200);
    item->setData(ItemName, "文字");
    item->setCursor(Qt::SizeAllCursor);
    addItemWithHistory(item, tr("添加文字"));
    scene->clearSelection();
    item->setSelected(true);
}

//接受ComBox发送过来的比例尺
void MyGraphicsView::getScaleComBoxCurrentIndexChanged(int index)
{
    ScaleType requested = scaleType;
    switch (index)
    {
    case 0: requested = OneToTen; break;
    case 1: requested = OneToTwenty; break;
    case 2: requested = OneToFifty; break;
    case 3: requested = OneToHundred; break;
    default: return;
    }
    if (requested == scaleType)
        return;
    const ScaleType previous = scaleType;
    if (!scene || scene->items().isEmpty())
    {
        applyScale(requested);
        return;
    }
    history->push(famp::graphics::makeCallbackCommand(
        [this, previous]() { applyScale(previous); },
        [this, requested]() { applyScale(requested); },
        tr("更改制图比例尺")));
}

void MyGraphicsView::applyScale(ScaleType scale)
{
    if (measurementActive)
        resetMeasurementInteraction(true);
    scaleType = scale;
    deltaOffset = scaleOffsetFor(scaleType);
    emit sendScaleOffset(deltaOffset);
    ReDraw(deltaOffset);
    emit scaleIndexChangedByHistory(static_cast<int>(scaleType));
    viewport()->update();
}

int MyGraphicsView::scaleDenominator(ScaleType scale)
{
    switch (scale)
    {
    case OneToTen:     return 10;
    case OneToTwenty:  return 20;
    case OneToFifty:   return 50;
    case OneToHundred: return 100;
    }
    return 50;
}

QPointF MyGraphicsView::pixelsPerMillimeterForScreen(QScreen *screen) const
{
    if (!screen)
    {
        const qreal fallback =
            famp::metric::deviceIndependentPixelsPerMillimeter(
                famp::metric::DefaultDotsPerInch);
        return QPointF(fallback, fallback);
    }

    const qreal dpiX = famp::metric::bestAvailableDotsPerInch(
        screen->physicalDotsPerInchX(), screen->logicalDotsPerInchX());
    const qreal dpiY = famp::metric::bestAvailableDotsPerInch(
        screen->physicalDotsPerInchY(), screen->logicalDotsPerInchY());
    const QPointF calibration = storedMetricCalibration(screen);
    return QPointF(
        famp::metric::calibratedPixelsPerMillimeter(
            famp::metric::deviceIndependentPixelsPerMillimeter(dpiX),
            calibration.x()),
        famp::metric::calibratedPixelsPerMillimeter(
            famp::metric::deviceIndependentPixelsPerMillimeter(dpiY),
            calibration.y()));
}

QPointF MyGraphicsView::scaleOffsetFor(ScaleType scale) const
{
    const int denominator = scaleDenominator(scale);
    return QPointF(
        famp::metric::pixelsPerMeterAtScale(metricPixelsPerMillimeter.x(), denominator),
        famp::metric::pixelsPerMeterAtScale(metricPixelsPerMillimeter.y(), denominator));
}

void MyGraphicsView::setMetricScreen(QScreen *screen)
{
    if (metricScreen == screen)
    {
        refreshMetricLayout();
        return;
    }

    if (metricScreen)
        disconnect(metricScreen.data(), nullptr, this, nullptr);

    metricScreen = screen;
    if (metricScreen)
    {
        connect(metricScreen.data(), &QScreen::physicalDotsPerInchChanged,
                this, &MyGraphicsView::refreshMetricLayout);
        connect(metricScreen.data(), &QScreen::logicalDotsPerInchChanged,
                this, &MyGraphicsView::refreshMetricLayout);
        connect(metricScreen.data(), &QScreen::physicalSizeChanged,
                this, &MyGraphicsView::refreshMetricLayout);
    }

    refreshMetricLayout();
}

void MyGraphicsView::refreshMetricLayout()
{
    const QPointF newPixelsPerMillimeter =
        pixelsPerMillimeterForScreen(metricScreen.data());
    if (fuzzyPointCompare(metricPixelsPerMillimeter,
                          newPixelsPerMillimeter))
    {
        viewport()->update();
        return;
    }

    if (measurementActive)
    {
        resetMeasurementInteraction(true);
        emit measurementStatus(tr("显示比例发生变化，已取消当前测量。"));
    }
    metricPixelsPerMillimeter = newPixelsPerMillimeter;
    deltaOffset = scaleOffsetFor(scaleType);
    emit sendScaleOffset(deltaOffset);

    if (scene && !scene->items().isEmpty())
        ReDraw(deltaOffset);

    viewport()->update();
}

//接受改变比例尺时重新画图
void MyGraphicsView::getReDraw(ScaleType scale)
{
    if (measurementActive)
    {
        resetMeasurementInteraction(true);
        emit measurementStatus(tr("制图比例尺发生变化，已取消当前测量。"));
    }
    applyScale(scale);
}

//得到比例尺变化后坐标的偏移量
void MyGraphicsView::getScaleOffset(QPointF offset)
{
    this->deltaOffset = offset;
}

//出图模板按钮
void MyGraphicsView::slotOn_actPlotTab_triggered()
{
    //sendDlgClipVisible(false);        //关闭裁剪对话框
    setDlgPlotTab();
    sendGetCurrentScale();
    //qDebug() << "scale" << currentScaleIndex;
    dlgPlotTab->getCurrentScaleIndex(currentScaleIndex);    //将当前比例尺发送给出图模板对话框
}

//显示按当前显示器物理DPI绘制的毫米米格纸
void MyGraphicsView::slotOn_actMiGe_triggered(bool checked)
{
    metricGridVisible = checked;
    emit metricGridVisibilityChanged(metricGridVisible);
    viewport()->update();
}

void MyGraphicsView::slotCalibrateMetricGrid()
{
    QScreen* screen = metricScreen.data();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
    {
        QMessageBox::warning(
            this, tr("屏幕毫米校准"), tr("当前没有可用的显示器。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("屏幕毫米校准"));
    auto* root = new QVBoxLayout(&dialog);

    auto* instructions = new QLabel(
        tr("当前显示器：%1\n"
           "请用实体直尺分别测量下方横向和纵向标距，"
           "再输入实测长度。请对齐端点中心，不要计入线宽。\n"
           "校准值按显示器单独保存，窗口移到其他显示器时"
           "会自动切换。")
            .arg(screen->name().isEmpty() ? tr("未命名屏幕")
                                          : screen->name()),
        &dialog);
    instructions->setWordWrap(true);
    root->addWidget(instructions);

    QPointF activeFactors = storedMetricCalibration(screen);
    auto* target = new MetricCalibrationTarget(
        pixelsPerMillimeterForScreen(screen), &dialog);
    auto* targetRow = new QHBoxLayout;
    targetRow->addStretch();
    targetRow->addWidget(target);
    targetRow->addStretch();
    root->addLayout(targetRow);

    auto* measurements = new QFormLayout;
    QDoubleSpinBox horizontalMeasured(&dialog);
    QDoubleSpinBox verticalMeasured(&dialog);
    for (QDoubleSpinBox* spinBox : {&horizontalMeasured, &verticalMeasured})
    {
        spinBox->setRange(20.0, 500.0);
        spinBox->setDecimals(2);
        spinBox->setSingleStep(0.1);
        spinBox->setValue(famp::metric::CalibrationReferenceMillimeters);
        spinBox->setSuffix(tr(" mm"));
    }
    measurements->addRow(tr("横向实测长度"), &horizontalMeasured);
    measurements->addRow(tr("纵向实测长度"), &verticalMeasured);
    root->addLayout(measurements);

    auto* metadata = new QLabel(
        tr("屏幕上报：%1 × %2 DPI；当前校准系数：%3 × %4")
            .arg(screen->physicalDotsPerInchX(), 0, 'f', 2)
            .arg(screen->physicalDotsPerInchY(), 0, 'f', 2)
            .arg(activeFactors.x(), 0, 'f', 6)
            .arg(activeFactors.y(), 0, 'f', 6),
        &dialog);
    metadata->setWordWrap(true);
    root->addWidget(metadata);

    QDialogButtonBox buttons(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons.button(QDialogButtonBox::Ok)->setText(tr("应用校准"));
    QPushButton* resetButton = buttons.addButton(
        tr("恢复显示器自动值"), QDialogButtonBox::ResetRole);
    connect(resetButton, &QPushButton::clicked, &dialog, [&]() {
        clearMetricCalibration(screen);
        activeFactors = QPointF(1.0, 1.0);
        refreshMetricLayout();
        target->setPixelsPerMillimeter(
            pixelsPerMillimeterForScreen(screen));
        horizontalMeasured.setValue(
            famp::metric::CalibrationReferenceMillimeters);
        verticalMeasured.setValue(
            famp::metric::CalibrationReferenceMillimeters);
        metadata->setText(
            tr("已恢复显示器上报值：%1 × %2 DPI；"
               "当前校准系数：1.000000 × 1.000000")
                .arg(screen->physicalDotsPerInchX(), 0, 'f', 2)
                .arg(screen->physicalDotsPerInchY(), 0, 'f', 2));
    });
    connect(&buttons, &QDialogButtonBox::accepted,
            &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected,
            &dialog, &QDialog::reject);
    root->addWidget(&buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const double horizontalAdjustment = famp::metric::calibrationAdjustment(
        famp::metric::CalibrationReferenceMillimeters,
        horizontalMeasured.value());
    const double verticalAdjustment = famp::metric::calibrationAdjustment(
        famp::metric::CalibrationReferenceMillimeters,
        verticalMeasured.value());
    const QPointF newFactors(
        activeFactors.x() * horizontalAdjustment,
        activeFactors.y() * verticalAdjustment);
    if (!famp::metric::isValidCalibrationFactor(newFactors.x())
        || !famp::metric::isValidCalibrationFactor(newFactors.y()))
    {
        QMessageBox::warning(
            this, tr("屏幕毫米校准"),
            tr("计算出的校准系数超出安全范围，未保存任何更改。"));
        return;
    }

    saveMetricCalibration(screen, newFactors);
    refreshMetricLayout();
    metricGridVisible = true;
    emit metricGridVisibilityChanged(true);
    viewport()->update();
    emit sendStrFromGraphicView2Console(
        tr("已校准显示器 %1 的毫米网格：横向 %2，纵向 %3。")
            .arg(screen->name())
            .arg(newFactors.x(), 0, 'f', 6)
            .arg(newFactors.y(), 0, 'f', 6));
}

void MyGraphicsView::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->save();
    painter->fillRect(rect, Qt::white);

    if (!metricGridVisible
        || metricPixelsPerMillimeter.x() <= 0.0
        || metricPixelsPerMillimeter.y() <= 0.0)
    {
        painter->restore();
        return;
    }

    famp::metricgrid::draw(
        *painter, rect, metricPixelsPerMillimeter);
    painter->restore();
}

void MyGraphicsView::drawForeground(QPainter* painter, const QRectF&)
{
    if (!scene || scene->selectedItems().isEmpty())
        return;

    painter->save();
    QPen selectionPen(QColor(255, 174, 0));
    selectionPen.setWidthF(2.0);
    selectionPen.setCosmetic(true);
    selectionPen.setStyle(Qt::DashLine);
    selectionPen.setCapStyle(Qt::RoundCap);
    selectionPen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(selectionPen);
    painter->setBrush(QColor(255, 193, 7, 28));
    for (QGraphicsItem* item : scene->selectedItems())
    {
        if (item && item->isVisible())
            painter->drawRect(item->sceneBoundingRect().adjusted(
                -3.0, -3.0, 3.0, 3.0));
    }
    painter->restore();
}

void MyGraphicsView::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);

    QWindow *windowHandle = window()->windowHandle();
    if (!windowHandle)
        return;

    connect(windowHandle, &QWindow::screenChanged,
            this, &MyGraphicsView::setMetricScreen,
            Qt::UniqueConnection);
    setMetricScreen(windowHandle->screen());
}

void MyGraphicsView::beginMeasurement(famp::measurement::Kind kind,
                                      bool announce)
{
    resetMeasurementInteraction(false);
    measurementActive = true;
    measurementKind = kind;
    measurementScenePoints.clear();
    measurementHasHoverPoint = false;
    scene->clearSelection();
    setDragMode(QGraphicsView::NoDrag);
    viewport()->setCursor(Qt::CrossCursor);

    QString instructions;
    if (kind == famp::measurement::Kind::Distance)
        instructions = tr("距离测量：依次点击折线节点，右键或双击完成，Esc 取消。");
    else if (kind == famp::measurement::Kind::Area)
        instructions = tr("面积测量：依次点击边界点，右键或双击闭合，Esc 取消。");
    else
        instructions = tr("角度测量：依次点击第一边点、顶点和第二边点，Esc 取消。");
    if (announce)
        emit measurementStatus(instructions);
}

void MyGraphicsView::startDistanceMeasurement(bool announce)
{
    beginMeasurement(famp::measurement::Kind::Distance, announce);
}

void MyGraphicsView::startAreaMeasurement(bool announce)
{
    beginMeasurement(famp::measurement::Kind::Area, announce);
}

void MyGraphicsView::startAngleMeasurement(bool announce)
{
    beginMeasurement(famp::measurement::Kind::Angle, announce);
}

void MyGraphicsView::deactivateMeasurement()
{
    resetMeasurementInteraction(false);
}

void MyGraphicsView::cancelMeasurement()
{
    if (!measurementActive)
        return;
    resetMeasurementInteraction(true);
    emit measurementStatus(tr("已取消测量。"));
}

void MyGraphicsView::resetMeasurementInteraction(bool notify)
{
    if (measurementPreviewPath)
    {
        if (QGraphicsScene* ownerScene = measurementPreviewPath->scene())
            ownerScene->removeItem(measurementPreviewPath);
        delete measurementPreviewPath;
        measurementPreviewPath = nullptr;
    }
    if (measurementPreviewLabel)
    {
        if (QGraphicsScene* ownerScene = measurementPreviewLabel->scene())
            ownerScene->removeItem(measurementPreviewLabel);
        delete measurementPreviewLabel;
        measurementPreviewLabel = nullptr;
    }

    const bool wasActive = measurementActive;
    measurementActive = false;
    measurementScenePoints.clear();
    measurementHasHoverPoint = false;
    setDragMode(QGraphicsView::RubberBandDrag);
    viewport()->setCursor(Qt::CrossCursor);
    if (notify && wasActive)
        emit measurementModeEnded();
}

void MyGraphicsView::updateMeasurementPreview()
{
    if (!measurementActive)
        return;

    QVector<QPointF> previewPoints = measurementScenePoints;
    if (measurementHasHoverPoint
        && (previewPoints.isEmpty()
            || QLineF(previewPoints.back(), measurementHoverPoint).length() > 0.01))
    {
        previewPoints.append(measurementHoverPoint);
    }

    if (!measurementPreviewPath)
    {
        measurementPreviewPath = scene->addPath(QPainterPath());
        measurementPreviewPath->setData(
            famp::graphicsdoc::TransientItemDataKey, true);
        QPen previewPen(QColor(0, 102, 204));
        previewPen.setWidthF(2.0);
        previewPen.setStyle(Qt::DashLine);
        measurementPreviewPath->setPen(previewPen);
        measurementPreviewPath->setBrush(
            measurementKind == famp::measurement::Kind::Area
                ? QBrush(QColor(0, 102, 204, 30))
                : Qt::NoBrush);
        measurementPreviewPath->setZValue(9999.0);
    }

    QPainterPath previewPath;
    if (!previewPoints.isEmpty())
    {
        previewPath.moveTo(previewPoints.front());
        for (int index = 1; index < previewPoints.size(); ++index)
            previewPath.lineTo(previewPoints.at(index));
        if (measurementKind == famp::measurement::Kind::Area
            && previewPoints.size() >= 3)
        {
            previewPath.closeSubpath();
        }
    }
    measurementPreviewPath->setPath(previewPath);

    QString label;
    QVector<QPointF> meterPoints;
    if (famp::measurement::sceneToMeters(
            previewPoints, deltaOffset, meterPoints, nullptr))
    {
        if (meterPoints.size()
            >= famp::measurement::minimumPointCount(measurementKind))
        {
            label = famp::measurement::formatSummary(
                measurementKind, meterPoints);
        }
    }

    if (label.isEmpty())
    {
        if (measurementPreviewLabel)
            measurementPreviewLabel->hide();
        return;
    }
    if (!measurementPreviewLabel)
    {
        measurementPreviewLabel = scene->addSimpleText(QString());
        measurementPreviewLabel->setData(
            famp::graphicsdoc::TransientItemDataKey, true);
        measurementPreviewLabel->setBrush(QColor(0, 70, 140));
        measurementPreviewLabel->setZValue(10000.0);
    }
    measurementPreviewLabel->setText(label);
    measurementPreviewLabel->setPos(
        previewPoints.back() + QPointF(8.0, -22.0));
    measurementPreviewLabel->show();
}

void MyGraphicsView::finishMeasurement()
{
    if (!measurementActive)
        return;

    if (measurementScenePoints.size()
        < famp::measurement::minimumPointCount(measurementKind))
    {
        emit measurementStatus(
            tr("测量点不足：距离至少需要 2 点，面积和角度至少需要 3 点。"));
        return;
    }

    QVector<QPointF> meterPoints;
    QString error;
    if (!famp::measurement::sceneToMeters(
            measurementScenePoints, deltaOffset, meterPoints, &error))
    {
        emit measurementStatus(error);
        return;
    }

    const double value = famp::measurement::value(measurementKind, meterPoints);
    if (!std::isfinite(value) || value <= 1.0e-9)
    {
        emit measurementStatus(tr("测量结果为零，请重新选择不同的点。"));
        return;
    }

    auto* item = new MeasurementItem(
        measurementKind, meterPoints, deltaOffset);
    QString commandText = tr("添加距离测量");
    if (measurementKind == famp::measurement::Kind::Area)
        commandText = tr("添加面积测量");
    else if (measurementKind == famp::measurement::Kind::Angle)
        commandText = tr("添加角度测量");
    addItemWithHistory(item, commandText);
    const QString resultText = famp::measurement::formatSummary(
        measurementKind, meterPoints);
    resetMeasurementInteraction(true);
    emit measurementStatus(tr("测量完成：%1").arg(resultText));
}

int MyGraphicsView::measurementCount() const
{
    int count = 0;
    for (QGraphicsItem* item : scene->items())
    {
        if (item && item->type() == MeasurementItem::Type)
            ++count;
    }
    return count;
}

void MyGraphicsView::clearMeasurements(bool announce)
{
    if (measurementActive)
        resetMeasurementInteraction(true);

    QVector<famp::graphics::ItemHandle> handles;
    for (QGraphicsItem* item : scene->items())
    {
        if (item && item->type() == MeasurementItem::Type)
            handles.append(handleForItem(item));
    }
    if (handles.isEmpty())
    {
        if (announce)
            emit measurementStatus(tr("当前画布没有测量结果。"));
        return;
    }

    const int count = handles.size();
    history->push(famp::graphics::makeRemoveItemsCommand(
        scene, handles, tr("清除测量结果")));
    if (announce)
    {
        emit measurementStatus(tr("已清除 %1 个测量结果，可通过撤销恢复。")
                                   .arg(count));
    }
}

void MyGraphicsView::rescaleMeasurementItems()
{
    for (QGraphicsItem* item : scene->items())
    {
        if (auto* measurementItem = dynamic_cast<MeasurementItem*>(item))
            measurementItem->setSceneUnitsPerMeter(deltaOffset);
    }
}

void MyGraphicsView::rescaleTerrainItems()
{
    for (QGraphicsItem* item : scene->items())
    {
        if (auto* contourItem = dynamic_cast<ContourItem*>(item))
            contourItem->setSceneUnitsPerMeter(deltaOffset);
    }
}

void MyGraphicsView::keyPressEvent(QKeyEvent *e)
{
    if (measurementActive && e->key() == Qt::Key_Escape)
    {
        cancelMeasurement();
        e->accept();
        return;
    }
    emit this->keyPress(e);
    QGraphicsView::keyPressEvent(e);
}

void MyGraphicsView::mousePressEvent(QMouseEvent *e)
{
    if (measurementActive)
    {
        if (e->button() == Qt::RightButton)
        {
            finishMeasurement();
            e->accept();
            return;
        }
        if (e->button() == Qt::LeftButton)
        {
            const QPointF scenePoint = mapToScene(e->pos());
            if (measurementScenePoints.isEmpty()
                || QLineF(measurementScenePoints.back(), scenePoint).length() > 0.01)
            {
                measurementScenePoints.append(scenePoint);
            }
            measurementHasHoverPoint = false;
            updateMeasurementPreview();
            if (measurementKind == famp::measurement::Kind::Angle
                && measurementScenePoints.size() >= 3)
            {
                finishMeasurement();
            }
            e->accept();
            return;
        }
    }

    if (e->button() == Qt::LeftButton)
    {
        QPoint point = e->pos();
        emit this->mouseClicked(point);
    }
    QGraphicsView::mousePressEvent(e);
    mousePressItemStates = e->button() == Qt::LeftButton
        ? selectedItemStates()
        : QVector<famp::graphics::ItemState>();
}

void MyGraphicsView::mouseMoveEvent(QMouseEvent *e)
{
    QPoint point = e->pos();
    gvScenePos = this->mapToScene(point);
    emit this->mouseMovePoint(point);

    if (measurementActive)
    {
        measurementHoverPoint = gvScenePos;
        measurementHasHoverPoint = true;
        updateMeasurementPreview();
        e->accept();
        return;
    }

    return QGraphicsView::mouseMoveEvent(e);
}

void MyGraphicsView::mouseReleaseEvent(QMouseEvent *e)
{
    if (measurementActive)
    {
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton && !mousePressItemStates.isEmpty())
        pushTransformChange(mousePressItemStates, tr("拖动图元"));
    mousePressItemStates.clear();
}

void MyGraphicsView::mouseDoubleClickEvent(QMouseEvent * e)
{
    if (measurementActive)
    {
        if (e->button() == Qt::LeftButton)
        {
            const QPointF scenePoint = mapToScene(e->pos());
            if (measurementScenePoints.isEmpty()
                || QLineF(measurementScenePoints.back(), scenePoint).length() > 0.01)
            {
                measurementScenePoints.append(scenePoint);
            }
            finishMeasurement();
        }
        e->accept();
        return;
    }

    QPoint point = e->pos();
    QPointF pointScene = this->mapToScene(point);       //转换到Scene坐标

    QGraphicsItem  *item = NULL;
    item = scene->itemAt(pointScene, this->transform());    //获取光标下的绘图项

    if (item == NULL)   return;

    switch (item->type())
    {
    case(QGraphicsTextItem::Type):
    {
        QGraphicsTextItem * textItem = qgraphicsitem_cast<QGraphicsTextItem*>(item);

        const QFont previousFont = textItem->font();
        QFont font = previousFont;
        bool ok = false;
        font = QFontDialog::getFont(&ok, font, this, "设置字体");
        if (ok && font != previousFont)
        {
            textItem->setFont(font);
            history->push(famp::graphics::makeTextFontCommand(
                handleForItem(textItem), previousFont, font, tr("修改文字字体")));
        }
    }
    break;

    default:
        break;
    }
}
