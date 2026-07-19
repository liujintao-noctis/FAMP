/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: VTK 3D 视图门面 — 平面切割、投影
 *****************************************************************/

#include "MyVTK.h"
#include "CloudLayer.h"
#include "CrsService.h"
#include "PlaneWidgetPlacement.h"
#include "QDlgClip.h"
#include "VTKRenderManager.h"
#include "VTKPointCloudManager.h"
#include "VTKProjectionManager.h"

#include <QDebug>
#include <QSet>
#include <QTimer>

#include <vtkCellArray.h>
#include <vtkConeSource.h>
#include <vtkCommand.h>
#include <vtkLineSource.h>
#include <vtkObjectFactory.h>
#include <vtkPlaneSource.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper.h>
#include <vtkTextProperty.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
class FampPlaneWidget final : public vtkPlaneWidget
{
public:
    static FampPlaneWidget* New();
    vtkTypeMacro(FampPlaneWidget, vtkPlaneWidget);

    void SetNormalHandleLengthRatio(double ratio)
    {
        normalHandleLengthRatio = std::clamp(ratio, 0.02, 0.2);
        RefreshNormalHandles();
    }

protected:
    FampPlaneWidget()
    {
        normalHandleCallback = vtkSmartPointer<vtkCallbackCommand>::New();
        normalHandleCallback->SetClientData(this);
        normalHandleCallback->SetCallback(
            &FampPlaneWidget::normalHandleInteractionCallback);
        interactionObserverTag = AddObserver(
            vtkCommand::InteractionEvent, normalHandleCallback);
    }

    ~FampPlaneWidget() override
    {
        if (interactionObserverTag != 0)
            RemoveObserver(interactionObserverTag);
    }

private:
    static void normalHandleInteractionCallback(
        vtkObject*, unsigned long, void* clientData, void*)
    {
        auto* self = static_cast<FampPlaneWidget*>(clientData);
        if (self)
            self->RefreshNormalHandles();
    }

    void RefreshNormalHandles()
    {
        double center[3];
        PlaneSource->GetCenter(center);
        double normal[3];
        PlaneSource->GetNormal(normal);
        if (vtkMath::Normalize(normal) <= 0.0)
            return;
        const double diagonal = std::sqrt(vtkMath::Distance2BetweenPoints(
            PlaneSource->GetPoint1(), PlaneSource->GetPoint2()));
        if (!std::isfinite(diagonal)
            || diagonal <= std::numeric_limits<double>::epsilon())
        {
            return;
        }

        const double length = diagonal * normalHandleLengthRatio;
        LineSource->SetPoint1(center);
        LineSource2->SetPoint1(center);
        double endpoint[3]{
            center[0] + length * normal[0],
            center[1] + length * normal[1],
            center[2] + length * normal[2]};
        LineSource->SetPoint2(endpoint);
        ConeSource->SetCenter(endpoint);
        ConeSource->SetDirection(normal);
        endpoint[0] = center[0] - length * normal[0];
        endpoint[1] = center[1] - length * normal[1];
        endpoint[2] = center[2] - length * normal[2];
        LineSource2->SetPoint2(endpoint);
        ConeSource2->SetCenter(endpoint);
        ConeSource2->SetDirection(normal);
    }

    double normalHandleLengthRatio = 0.12;
    vtkSmartPointer<vtkCallbackCommand> normalHandleCallback;
    unsigned long interactionObserverTag = 0;
};

vtkStandardNewMacro(FampPlaneWidget);

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

vtkSmartPointer<vtkPolyData> measurementPolyData(
    const QVector<QVector3D>& points,
    famp::measurement::Kind kind)
{
    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    auto pointsData = vtkSmartPointer<vtkPoints>::New();
    auto vertices = vtkSmartPointer<vtkCellArray>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();

    pointsData->SetNumberOfPoints(points.size());
    for (int index = 0; index < points.size(); ++index)
    {
        const QVector3D& point = points.at(index);
        pointsData->SetPoint(index, point.x(), point.y(), point.z());
        vertices->InsertNextCell(1);
        vertices->InsertCellPoint(index);
    }

    if (points.size() >= 2)
    {
        std::vector<vtkIdType> ids;
        ids.reserve(static_cast<std::size_t>(points.size()) + 1U);
        for (int index = 0; index < points.size(); ++index)
            ids.push_back(index);
        if (kind == famp::measurement::Kind::Area && points.size() >= 3)
            ids.push_back(0);
        lines->InsertNextCell(static_cast<vtkIdType>(ids.size()), ids.data());
    }

    polyData->SetPoints(pointsData);
    polyData->SetVerts(vertices);
    polyData->SetLines(lines);
    return polyData;
}

void styleMeasurementActor(vtkActor* actor,
                           double red,
                           double green,
                           double blue)
{
    actor->GetProperty()->SetColor(red, green, blue);
    actor->GetProperty()->SetLineWidth(3.0);
    actor->GetProperty()->SetPointSize(9.0);
    actor->GetProperty()->SetRenderLinesAsTubes(true);
    actor->GetProperty()->SetRenderPointsAsSpheres(true);
    actor->PickableOff();
}

void styleMeasurementLabel(vtkBillboardTextActor3D* label,
                           double red,
                           double green,
                           double blue)
{
    label->GetTextProperty()->SetColor(red, green, blue);
    label->GetTextProperty()->SetFontSize(18);
    label->GetTextProperty()->BoldOn();
    label->GetTextProperty()->SetBackgroundColor(0.05, 0.05, 0.05);
    label->GetTextProperty()->SetBackgroundOpacity(0.65);
    label->PickableOff();
}

void stylePlaneManipulator(
    vtkPlaneWidget* plane,
    const std::array<double, 3>& handleColor)
{
    if (!plane)
        return;

    // vtkPlaneWidget defaults to 0.05 of the fitted diagonal, which makes
    // its four spheres and normal cones dominate archaeological clouds.
    // fitPlaneWidgetToCloud may reduce this further for narrow point sets.
    plane->SetHandleSize(0.006);
    vtkProperty* handle = plane->GetHandleProperty();
    handle->SetColor(handleColor[0], handleColor[1], handleColor[2]);
    handle->SetLineWidth(1.25);
    handle->SetPointSize(4.0);
    handle->SetOpacity(0.9);
    vtkProperty* selected = plane->GetSelectedHandleProperty();
    selected->SetColor(1.0, 0.78, 0.12);
    selected->SetLineWidth(1.75);
    selected->SetPointSize(5.0);
    if (auto* adaptivePlane = FampPlaneWidget::SafeDownCast(plane))
        adaptivePlane->SetNormalHandleLengthRatio(0.12);
}

bool placePlaneForCloud(
    vtkPlaneWidget* plane,
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud,
    const std::array<double, 3>& normal,
    const std::array<double, 3>& preferredAxis)
{
    if (!plane || !cloud)
        return false;
    const auto placement = famp::viewport::fitPlaneWidgetToCloud(
        *cloud, normal, preferredAxis);
    if (!placement.has_value())
        return false;

    plane->SetOrigin(
        placement->origin[0], placement->origin[1], placement->origin[2]);
    plane->SetPoint1(
        placement->point1[0], placement->point1[1], placement->point1[2]);
    plane->SetPoint2(
        placement->point2[0], placement->point2[1], placement->point2[2]);
    plane->SetHandleSize(placement->handleSize);
    // No VTK input is attached here. PlaceWidget therefore preserves the
    // fitted geometry and recalculates handle sizes from its dimensions.
    plane->PlaceWidget();
    if (auto* adaptivePlane = FampPlaneWidget::SafeDownCast(plane))
    {
        adaptivePlane->SetNormalHandleLengthRatio(
            placement->normalHandleLengthRatio);
    }
    return true;
}
}

MyVTK::MyVTK(QWidget *parent)
    : FAMP_QVTK_WIDGET(parent)
{
    m_renderManager = new VTKRenderManager(this);
    m_renderManager->Init(); //初始化渲染器、交互器、颜色、相机
    m_renderManager->mixBackGround();   //混色背景
    m_renderManager->setWidgetAxes();   //坐标轴

    m_pointCloudManager = new VTKPointCloudManager();
    m_projectionManager = new VTKProjectionManager();
    m_projectionManager->initProjectionActor(m_renderManager->renderer());

    orignalCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);  //传入的点云初始化
    currentItemCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);

    AABB_Polydata = NULL;
    randomPlane = NULL;
    verticalPlane = NULL;
    horizonalPlane = NULL;
    clipPlane = NULL;
    dlgClip = NULL;
    vtkDlgClip = NULL;
    cloud_cut_plane = NULL;

    isActiveDlgClip = false;
    isClipSucessed = false;

    measurementPicker = vtkSmartPointer<vtkPointPicker>::New();
    measurementPicker->PickFromListOn();
    measurementPicker->SetTolerance(0.015);
    measurementCallback = vtkSmartPointer<vtkCallbackCommand>::New();
    measurementCallback->SetClientData(this);
    measurementCallback->SetCallback(&MyVTK::measurementEventCallback);
    vtkRenderWindowInteractor* interactor =
        m_renderManager->renderWindowInteractor();
    if (interactor)
    {
        constexpr unsigned long events[] = {
            vtkCommand::LeftButtonPressEvent,
            vtkCommand::RightButtonPressEvent,
            vtkCommand::MouseMoveEvent,
            vtkCommand::KeyPressEvent};
        for (const unsigned long eventId : events)
        {
            measurementObserverTags.push_back(interactor->AddObserver(
                eventId, measurementCallback, 1.0));
        }
    }

    // Defer the first Render() until Qt has shown the widget and created the
    // native OpenGL surface. Rendering from the constructor can crash on
    // Windows with QVTKOpenGLNativeWidget.
    QTimer::singleShot(0, this, [this]() {
        initCamera();       //相机初始化
    });

    //切割平面初始化
    randomPlane = FampPlaneWidget::New();
    horizonalPlane = FampPlaneWidget::New();
    verticalPlane = FampPlaneWidget::New();

    connect(this, SIGNAL(sendAABBPolydata(vtkPolyData *)), this, SLOT(getAABBPolydata(vtkPolyData *))); //传送AABBPolyData
}

MyVTK::~MyVTK()
{
    if (m_renderManager && m_renderManager->renderWindowInteractor())
    {
        for (const unsigned long tag : measurementObserverTags)
            m_renderManager->renderWindowInteractor()->RemoveObserver(tag);
    }
    vtkRenderer* renderer = m_renderManager ? m_renderManager->renderer() : nullptr;
    if (renderer)
    {
        if (measurementPreviewActor)
            renderer->RemoveActor(measurementPreviewActor);
        if (measurementPreviewLabel)
            renderer->RemoveActor(measurementPreviewLabel);
        if (profileSelectionPreviewActor)
            renderer->RemoveActor(profileSelectionPreviewActor);
        if (profileSelectionPreviewLabel)
            renderer->RemoveActor(profileSelectionPreviewLabel);
        for (const MeasurementVisual& visual : measurementVisuals)
        {
            renderer->RemoveActor(visual.geometry);
            renderer->RemoveActor(visual.label);
        }
    }
    measurementVisuals.clear();
    if (AABB_Polydata)
    {
        AABB_Polydata->Delete();
        AABB_Polydata = nullptr;
    }
    delete m_renderManager;
    delete m_pointCloudManager;
    delete m_projectionManager;
    if (randomPlane)    randomPlane->Delete();
    if (verticalPlane)  verticalPlane->Delete();
    if (horizonalPlane) horizonalPlane->Delete();
}

vtkCamera * MyVTK::getCamera() const
{
    return m_renderManager->camera();
}

void MyVTK::measurementEventCallback(vtkObject*,
                                     unsigned long eventId,
                                     void* clientData,
                                     void*)
{
    auto* self = static_cast<MyVTK*>(clientData);
    if (!self || !self->measurementCallback)
        return;
    self->measurementCallback->SetAbortFlag(
        self->handleMeasurementEvent(eventId) ? 1 : 0);
}

bool MyVTK::handleMeasurementEvent(unsigned long eventId)
{
    if (profileSelectionActive)
        return handleProfileSelectionEvent(eventId);
    if (!measurementActive)
        return false;

    if (eventId == vtkCommand::KeyPressEvent)
    {
        const char* key = m_renderManager->renderWindowInteractor()->GetKeySym();
        if (key && std::strcmp(key, "Escape") == 0)
        {
            cancelMeasurement();
            return true;
        }
        return false;
    }

    if (eventId == vtkCommand::RightButtonPressEvent)
    {
        finishMeasurement();
        return true;
    }

    if (eventId != vtkCommand::LeftButtonPressEvent
        && eventId != vtkCommand::MouseMoveEvent)
    {
        return false;
    }

    QVector3D pickedPoint;
    QString pickedLayerId;
    QString pickedCrs;
    if (!pickMeasurementPoint(pickedPoint, pickedLayerId, pickedCrs))
    {
        if (eventId == vtkCommand::LeftButtonPressEvent)
        {
            emit measurementStatus(
                tr("未拾取到点云点，请点击可见点云；可放大后重试。"));
        }
        else if (measurementHasHoverPoint)
        {
            measurementHasHoverPoint = false;
            updateMeasurementPreview();
        }
        return eventId == vtkCommand::LeftButtonPressEvent;
    }

    if (!measurementLayerId.isEmpty() && pickedLayerId != measurementLayerId)
    {
        if (eventId == vtkCommand::LeftButtonPressEvent)
        {
            emit measurementStatus(
                tr("同一项三维测量只能使用同一点云图层的点。"));
        }
        else if (measurementHasHoverPoint)
        {
            measurementHasHoverPoint = false;
            updateMeasurementPreview();
        }
        return eventId == vtkCommand::LeftButtonPressEvent;
    }

    if (eventId == vtkCommand::MouseMoveEvent)
    {
        if (measurementHasHoverPoint
            && (measurementHoverPoint - pickedPoint).lengthSquared() <= 1.0e-12f)
        {
            return false;
        }
        measurementHoverPoint = pickedPoint;
        measurementHasHoverPoint = true;
        updateMeasurementPreview();
        // Keep camera pan/zoom interaction available while measurement mode is
        // active. Left presses are still intercepted, so they cannot rotate.
        return false;
    }

    if (measurementPoints.isEmpty()
        || (measurementPoints.back() - pickedPoint).lengthSquared() > 1.0e-12f)
    {
        if (measurementPoints.isEmpty())
        {
            measurementLayerId = pickedLayerId;
            measurementCrs = pickedCrs;
        }
        measurementPoints.append(pickedPoint);
    }
    measurementHasHoverPoint = false;
    updateMeasurementPreview();
    emit measurementStatus(
        tr("点云测量：已拾取第 %1 点 (%2, %3, %4) m")
            .arg(measurementPoints.size())
            .arg(pickedPoint.x(), 0, 'f', 3)
            .arg(pickedPoint.y(), 0, 'f', 3)
            .arg(pickedPoint.z(), 0, 'f', 3));

    if (measurementKind == famp::measurement::Kind::Angle
        && measurementPoints.size() >= 3)
    {
        finishMeasurement();
    }
    return true;
}

bool MyVTK::pickMeasurementPoint(QVector3D& point,
                                 QString& layerId,
                                 QString& crs,
                                 QVector3D* localPoint)
{
    vtkRenderWindowInteractor* interactor =
        m_renderManager->renderWindowInteractor();
    if (!interactor || !measurementPicker || cloudActors.empty())
        return false;

    const int* position = interactor->GetEventPosition();
    if (!position
        || measurementPicker->Pick(position[0], position[1], 0.0,
                                   m_renderManager->renderer()) == 0
        || measurementPicker->GetPointId() < 0)
    {
        return false;
    }

    vtkActor* actor = measurementPicker->GetActor();
    const auto metadata = cloudActorMetadata.constFind(actor);
    if (!actor || metadata == cloudActorMetadata.cend()
        || metadata->layerId.isEmpty())
    {
        return false;
    }

    const double* picked = measurementPicker->GetPickPosition();
    if (!picked || !std::isfinite(picked[0])
        || !std::isfinite(picked[1]) || !std::isfinite(picked[2]))
    {
        return false;
    }
    point = QVector3D(static_cast<float>(picked[0]),
                      static_cast<float>(picked[1]),
                      static_cast<float>(picked[2]));
    if (localPoint)
    {
        auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
        vtkPolyData* polyData = mapper ? mapper->GetInput() : nullptr;
        const vtkIdType pointId = measurementPicker->GetPointId();
        if (!polyData || pointId < 0
            || pointId >= polyData->GetNumberOfPoints())
        {
            return false;
        }
        double local[3]{};
        polyData->GetPoint(pointId, local);
        if (!std::isfinite(local[0]) || !std::isfinite(local[1])
            || !std::isfinite(local[2]))
        {
            return false;
        }
        *localPoint = QVector3D(static_cast<float>(local[0]),
                               static_cast<float>(local[1]),
                               static_cast<float>(local[2]));
    }
    layerId = metadata->layerId;
    crs = metadata->crs;
    return true;
}

void MyVTK::beginMeasurement(famp::measurement::Kind kind, bool announce)
{
    if (profileSelectionActive)
    {
        resetProfileSelection(false);
        emit profileLineSelectionCancelled();
    }
    resetMeasurementInteraction(false);
    measurementActive = true;
    measurementKind = kind;
    measurementLayerId.clear();
    measurementCrs.clear();
    measurementPoints.clear();
    measurementHasHoverPoint = false;
    setCursor(Qt::CrossCursor);

    measurementPreviewMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    measurementPreviewMapper->ScalarVisibilityOff();
    measurementPreviewActor = vtkSmartPointer<vtkActor>::New();
    measurementPreviewActor->SetMapper(measurementPreviewMapper);
    styleMeasurementActor(measurementPreviewActor, 1.0, 0.82, 0.0);
    m_renderManager->renderer()->AddActor(measurementPreviewActor);

    measurementPreviewLabel =
        vtkSmartPointer<vtkBillboardTextActor3D>::New();
    styleMeasurementLabel(measurementPreviewLabel, 1.0, 0.9, 0.2);
    measurementPreviewLabel->SetVisibility(false);
    m_renderManager->renderer()->AddActor(measurementPreviewLabel);
    m_renderManager->render();

    if (!announce)
        return;
    QString instructions;
    if (kind == famp::measurement::Kind::Distance)
        instructions = tr("点云距离测量：左键拾取节点，右键完成，Esc 取消。");
    else if (kind == famp::measurement::Kind::Area)
        instructions = tr("点云面积测量：左键拾取边界点，右键闭合，Esc 取消。");
    else
        instructions = tr("点云角度测量：依次拾取第一边点、顶点和第二边点。");
    emit measurementStatus(instructions);
}

void MyVTK::startDistanceMeasurement(bool announce)
{
    beginMeasurement(famp::measurement::Kind::Distance, announce);
}

void MyVTK::startAreaMeasurement(bool announce)
{
    beginMeasurement(famp::measurement::Kind::Area, announce);
}

void MyVTK::startAngleMeasurement(bool announce)
{
    beginMeasurement(famp::measurement::Kind::Angle, announce);
}

void MyVTK::cancelMeasurement()
{
    if (!measurementActive)
        return;
    resetMeasurementInteraction(true);
    emit measurementStatus(tr("已取消点云测量。"));
}

void MyVTK::deactivateMeasurement()
{
    resetMeasurementInteraction(false);
}

void MyVTK::resetMeasurementInteraction(bool notify)
{
    const bool wasActive = measurementActive;
    measurementActive = false;
    measurementLayerId.clear();
    measurementCrs.clear();
    measurementPoints.clear();
    measurementHasHoverPoint = false;
    unsetCursor();

    if (measurementPreviewActor)
        m_renderManager->renderer()->RemoveActor(measurementPreviewActor);
    if (measurementPreviewLabel)
        m_renderManager->renderer()->RemoveActor(measurementPreviewLabel);
    measurementPreviewActor = nullptr;
    measurementPreviewMapper = nullptr;
    measurementPreviewLabel = nullptr;

    if (m_renderManager)
        m_renderManager->render();
    if (notify && wasActive)
        emit measurementModeEnded();
}

void MyVTK::updateMeasurementPreview()
{
    if (!measurementActive || !measurementPreviewMapper)
        return;

    QVector<QVector3D> previewPoints = measurementPoints;
    if (measurementHasHoverPoint
        && (previewPoints.isEmpty()
            || (previewPoints.back() - measurementHoverPoint).lengthSquared()
                > 1.0e-12f))
    {
        previewPoints.append(measurementHoverPoint);
    }
    measurementPreviewMapper->SetInputData(
        measurementPolyData(previewPoints, measurementKind));

    if (previewPoints.size()
            >= famp::measurement::minimumPointCount(measurementKind)
        && famp::measurement::finitePoints(previewPoints))
    {
        const QByteArray label = famp::measurement::formatSummary(
            measurementKind, previewPoints).toUtf8();
        measurementPreviewLabel->SetInput(label.constData());
        const QVector3D& position = previewPoints.back();
        measurementPreviewLabel->SetPosition(
            position.x(), position.y(), position.z());
        measurementPreviewLabel->SetVisibility(true);
    }
    else
    {
        measurementPreviewLabel->SetVisibility(false);
    }
    m_renderManager->render();
}

void MyVTK::finishMeasurement()
{
    if (!measurementActive)
        return;
    if (measurementPoints.size()
        < famp::measurement::minimumPointCount(measurementKind))
    {
        emit measurementStatus(
            tr("点云测量点不足：距离至少 2 点，面积和角度至少 3 点。"));
        return;
    }

    const double result = famp::measurement::value(
        measurementKind, measurementPoints);
    if (!std::isfinite(result) || result <= 1.0e-9)
    {
        emit measurementStatus(tr("点云测量结果为零或无效，请重新拾取。"));
        return;
    }

    famp::measurement::Record3D record;
    record.id = famp::measurement::createRecordId();
    record.layerId = measurementLayerId;
    record.crs = measurementCrs;
    record.kind = measurementKind;
    record.points = measurementPoints;
    QString validationError;
    if (!famp::measurement::validateRecord3D(record, &validationError))
    {
        emit measurementStatus(validationError);
        return;
    }

    const QString resultText = famp::measurement::formatSummary(
        record.kind, record.points);
    resetMeasurementInteraction(true);
    emit measurementCompleted(record);
    emit measurementStatus(tr("点云测量完成：%1").arg(resultText));
}

void MyVTK::addMeasurementVisual(
    const famp::measurement::Record3D& record)
{
    MeasurementVisual visual;
    visual.record = record;
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(measurementPolyData(record.points, record.kind));
    mapper->ScalarVisibilityOff();
    visual.geometry = vtkSmartPointer<vtkActor>::New();
    visual.geometry->SetMapper(mapper);
    styleMeasurementActor(visual.geometry, 0.0, 0.8, 1.0);

    visual.label = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    const QString labelText = famp::measurement::formatSummary(
        record.kind, record.points);
    const QByteArray encodedLabel = labelText.toUtf8();
    visual.label->SetInput(encodedLabel.constData());
    const QVector3D& position = record.points.back();
    visual.label->SetPosition(position.x(), position.y(), position.z());
    styleMeasurementLabel(visual.label, 0.2, 0.9, 1.0);

    const bool actorVisible = visual.visible && isLayerVisible(record.layerId);
    visual.geometry->SetVisibility(actorVisible);
    visual.label->SetVisibility(actorVisible);

    m_renderManager->renderer()->AddActor(visual.geometry);
    m_renderManager->renderer()->AddActor(visual.label);
    measurementVisuals.push_back(std::move(visual));
}

int MyVTK::measurementCount() const
{
    return static_cast<int>(measurementVisuals.size());
}

QVector<famp::measurement::Record3D> MyVTK::measurements() const
{
    QVector<famp::measurement::Record3D> records;
    records.reserve(static_cast<qsizetype>(measurementVisuals.size()));
    for (const MeasurementVisual& visual : measurementVisuals)
        records.append(visual.record);
    return records;
}

bool MyVTK::addMeasurement(
    const famp::measurement::Record3D& record,
    QString* errorMessage)
{
    QString validationError;
    if (!famp::measurement::validateRecord3D(record, &validationError))
    {
        setError(errorMessage, validationError);
        return false;
    }
    if (!recordMatchesRegisteredLayer(record, errorMessage))
        return false;
    const auto duplicate = std::find_if(
        measurementVisuals.cbegin(), measurementVisuals.cend(),
        [&record](const MeasurementVisual& visual) {
            return visual.record.id.compare(
                record.id, Qt::CaseInsensitive) == 0;
        });
    if (duplicate != measurementVisuals.cend())
    {
        setError(errorMessage, tr("三维测量 ID 重复。"));
        return false;
    }

    addMeasurementVisual(record);
    m_renderManager->render();
    if (errorMessage)
        errorMessage->clear();
    emit measurementsChanged();
    return true;
}

bool MyVTK::removeMeasurement(const QString& recordId)
{
    const auto found = std::find_if(
        measurementVisuals.begin(), measurementVisuals.end(),
        [&recordId](const MeasurementVisual& visual) {
            return visual.record.id.compare(
                recordId.trimmed(), Qt::CaseInsensitive) == 0;
        });
    if (found == measurementVisuals.end())
        return false;

    m_renderManager->renderer()->RemoveActor(found->geometry);
    m_renderManager->renderer()->RemoveActor(found->label);
    measurementVisuals.erase(found);
    m_renderManager->render();
    emit measurementsChanged();
    return true;
}

bool MyVTK::setMeasurementVisible(const QString& recordId, bool visible)
{
    const QString normalized = recordId.trimmed();
    const auto found = std::find_if(
        measurementVisuals.begin(), measurementVisuals.end(),
        [&normalized](const MeasurementVisual& visual) {
            return visual.record.id.compare(
                normalized, Qt::CaseInsensitive) == 0;
        });
    if (found == measurementVisuals.end())
        return false;

    found->visible = visible;
    const bool actorVisible = visible && isLayerVisible(found->record.layerId);
    found->geometry->SetVisibility(actorVisible);
    found->label->SetVisibility(actorVisible);
    m_renderManager->render();
    return true;
}

bool MyVTK::setMeasurementSelected(const QString& recordId, bool selected)
{
    const QString normalized = recordId.trimmed();
    const auto found = std::find_if(
        measurementVisuals.begin(), measurementVisuals.end(),
        [&normalized](const MeasurementVisual& visual) {
            return visual.record.id.compare(
                normalized, Qt::CaseInsensitive) == 0;
        });
    if (found == measurementVisuals.end())
        return false;

    if (selected)
    {
        styleMeasurementActor(found->geometry, 1.0, 0.58, 0.0);
        found->geometry->GetProperty()->SetLineWidth(5.0);
        found->geometry->GetProperty()->SetPointSize(12.0);
        styleMeasurementLabel(found->label, 1.0, 0.75, 0.15);
        found->label->GetTextProperty()->SetFontSize(20);
    }
    else
    {
        styleMeasurementActor(found->geometry, 0.0, 0.8, 1.0);
        styleMeasurementLabel(found->label, 0.2, 0.9, 1.0);
    }
    m_renderManager->render();
    update();
    return true;
}

void MyVTK::clearMeasurementSelection()
{
    for (MeasurementVisual& visual : measurementVisuals)
    {
        styleMeasurementActor(visual.geometry, 0.0, 0.8, 1.0);
        styleMeasurementLabel(visual.label, 0.2, 0.9, 1.0);
    }
    m_renderManager->render();
    update();
}

bool MyVTK::setMeasurements(
    const QVector<famp::measurement::Record3D>& records,
    QString* errorMessage)
{
    QSet<QString> ids;
    for (const auto& record : records)
    {
        QString validationError;
        const QString normalizedId = record.id.trimmed().toLower();
        if (!famp::measurement::validateRecord3D(record, &validationError))
        {
            setError(errorMessage, validationError);
            return false;
        }
        if (ids.contains(normalizedId))
        {
            setError(errorMessage, tr("三维测量 ID 重复。"));
            return false;
        }
        if (!recordMatchesRegisteredLayer(record, errorMessage))
            return false;
        ids.insert(normalizedId);
    }

    for (const MeasurementVisual& visual : measurementVisuals)
    {
        m_renderManager->renderer()->RemoveActor(visual.geometry);
        m_renderManager->renderer()->RemoveActor(visual.label);
    }
    measurementVisuals.clear();
    for (const auto& record : records)
        addMeasurementVisual(record);
    m_renderManager->render();
    if (errorMessage)
        errorMessage->clear();
    emit measurementsChanged();
    return true;
}

void MyVTK::clearMeasurements(bool announce)
{
    if (measurementActive)
        resetMeasurementInteraction(true);

    const int count = measurementCount();
    for (const MeasurementVisual& visual : measurementVisuals)
    {
        m_renderManager->renderer()->RemoveActor(visual.geometry);
        m_renderManager->renderer()->RemoveActor(visual.label);
    }
    measurementVisuals.clear();
    if (m_renderManager)
        m_renderManager->render();
    if (count > 0)
        emit measurementsChanged();

    if (!announce)
        return;
    if (count == 0)
        emit measurementStatus(tr("当前点云视图没有测量结果。"));
    else
        emit measurementStatus(tr("已清除 %1 个点云测量结果。").arg(count));
}

bool MyVTK::startProfileLineSelection(const QString& layerId)
{
    const QString normalized = layerId.trimmed().toLower();
    if (normalized.isEmpty() || !hasRegisteredLayer(normalized)
        || !isLayerVisible(normalized))
    {
        emit measurementStatus(
            tr("所选点云图层未在三维视图中显示，无法拾取剖面线。"));
        return false;
    }
    if (measurementActive)
        resetMeasurementInteraction(true);
    if (profileSelectionActive)
        resetProfileSelection(false);

    profileSelectionActive = true;
    profileSelectionLayerId = normalized;
    profileSelectionDisplayPoints.clear();
    profileSelectionLocalPoints.clear();
    profileSelectionHasHoverPoint = false;
    setCursor(Qt::CrossCursor);

    profileSelectionPreviewMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    profileSelectionPreviewMapper->ScalarVisibilityOff();
    profileSelectionPreviewActor = vtkSmartPointer<vtkActor>::New();
    profileSelectionPreviewActor->SetMapper(profileSelectionPreviewMapper);
    styleMeasurementActor(profileSelectionPreviewActor, 1.0, 0.35, 0.1);
    m_renderManager->renderer()->AddActor(profileSelectionPreviewActor);
    profileSelectionPreviewLabel =
        vtkSmartPointer<vtkBillboardTextActor3D>::New();
    styleMeasurementLabel(profileSelectionPreviewLabel, 1.0, 0.55, 0.2);
    profileSelectionPreviewLabel->SetVisibility(false);
    m_renderManager->renderer()->AddActor(profileSelectionPreviewLabel);
    m_renderManager->render();
    emit measurementStatus(
        tr("点云剖面：请在所选点云上左键拾取剖面起点和终点，Esc 或右键取消。"));
    return true;
}

void MyVTK::cancelProfileLineSelection()
{
    if (!profileSelectionActive)
        return;
    resetProfileSelection(false);
    emit profileLineSelectionCancelled();
    emit measurementStatus(tr("已取消点云剖面线拾取。"));
}

bool MyVTK::handleProfileSelectionEvent(unsigned long eventId)
{
    if (!profileSelectionActive)
        return false;
    if (eventId == vtkCommand::KeyPressEvent)
    {
        const char* key = m_renderManager->renderWindowInteractor()->GetKeySym();
        if (key && std::strcmp(key, "Escape") == 0)
        {
            cancelProfileLineSelection();
            return true;
        }
        return false;
    }
    if (eventId == vtkCommand::RightButtonPressEvent)
    {
        cancelProfileLineSelection();
        return true;
    }
    if (eventId != vtkCommand::LeftButtonPressEvent
        && eventId != vtkCommand::MouseMoveEvent)
    {
        return false;
    }

    QVector3D displayPoint;
    QVector3D localPoint;
    QString layerId;
    QString crs;
    if (!pickMeasurementPoint(displayPoint, layerId, crs, &localPoint)
        || layerId != profileSelectionLayerId)
    {
        if (eventId == vtkCommand::LeftButtonPressEvent)
        {
            emit measurementStatus(
                tr("请在当前所选点云图层上拾取剖面端点。"));
        }
        else if (profileSelectionHasHoverPoint)
        {
            profileSelectionHasHoverPoint = false;
            updateProfileSelectionPreview();
        }
        return eventId == vtkCommand::LeftButtonPressEvent;
    }

    if (eventId == vtkCommand::MouseMoveEvent)
    {
        profileSelectionHoverPoint = displayPoint;
        profileSelectionHasHoverPoint = true;
        updateProfileSelectionPreview();
        return false;
    }

    if (!profileSelectionDisplayPoints.isEmpty()
        && (profileSelectionDisplayPoints.front() - displayPoint)
                .lengthSquared() <= 1.0e-12f)
    {
        emit measurementStatus(tr("剖面终点不能与起点重合，请重新拾取。"));
        return true;
    }
    profileSelectionDisplayPoints.append(displayPoint);
    profileSelectionLocalPoints.append(localPoint);
    profileSelectionHasHoverPoint = false;
    updateProfileSelectionPreview();
    if (profileSelectionDisplayPoints.size() == 1)
    {
        emit measurementStatus(
            tr("点云剖面：起点已拾取，请左键拾取终点；Esc 或右键取消。"));
        return true;
    }

    const QString completedLayerId = profileSelectionLayerId;
    const QVector3D start = profileSelectionLocalPoints.at(0);
    const QVector3D end = profileSelectionLocalPoints.at(1);
    resetProfileSelection(false);
    emit measurementStatus(tr("剖面线拾取完成，正在设置采样参数…"));
    emit profileLineSelected(completedLayerId, start, end);
    return true;
}

void MyVTK::updateProfileSelectionPreview()
{
    if (!profileSelectionActive || !profileSelectionPreviewMapper)
        return;
    QVector<QVector3D> points = profileSelectionDisplayPoints;
    if (profileSelectionHasHoverPoint)
        points.append(profileSelectionHoverPoint);
    profileSelectionPreviewMapper->SetInputData(
        measurementPolyData(points, famp::measurement::Kind::Distance));
    if (!points.isEmpty())
    {
        const QByteArray text = profileSelectionDisplayPoints.isEmpty()
            ? tr("剖面起点").toUtf8() : tr("剖面终点").toUtf8();
        profileSelectionPreviewLabel->SetInput(text.constData());
        const QVector3D& position = points.back();
        profileSelectionPreviewLabel->SetPosition(
            position.x(), position.y(), position.z());
        profileSelectionPreviewLabel->SetVisibility(true);
    }
    else
    {
        profileSelectionPreviewLabel->SetVisibility(false);
    }
    m_renderManager->render();
}

void MyVTK::resetProfileSelection(bool notify)
{
    const bool wasActive = profileSelectionActive;
    profileSelectionActive = false;
    profileSelectionLayerId.clear();
    profileSelectionDisplayPoints.clear();
    profileSelectionLocalPoints.clear();
    profileSelectionHasHoverPoint = false;
    unsetCursor();
    if (profileSelectionPreviewActor)
        m_renderManager->renderer()->RemoveActor(profileSelectionPreviewActor);
    if (profileSelectionPreviewLabel)
        m_renderManager->renderer()->RemoveActor(profileSelectionPreviewLabel);
    profileSelectionPreviewMapper = nullptr;
    profileSelectionPreviewActor = nullptr;
    profileSelectionPreviewLabel = nullptr;
    if (m_renderManager)
        m_renderManager->render();
    if (notify && wasActive)
        emit profileLineSelectionCancelled();
}

bool MyVTK::hasRegisteredLayer(const QString& layerId) const
{
    const QString normalized = layerId.trimmed().toLower();
    for (auto iterator = cloudActorMetadata.cbegin();
         iterator != cloudActorMetadata.cend(); ++iterator)
    {
        if (iterator->layerId == normalized)
            return true;
    }
    return false;
}

bool MyVTK::recordMatchesRegisteredLayer(
    const famp::measurement::Record3D& record,
    QString* errorMessage) const
{
    const QString normalizedLayerId = record.layerId.trimmed().toLower();
    const QString normalizedCrs = record.crs.trimmed().isEmpty()
        ? QString() : famp::crs::normalizedEpsg(record.crs);
    bool foundLayer = false;
    for (auto iterator = cloudActorMetadata.cbegin();
         iterator != cloudActorMetadata.cend(); ++iterator)
    {
        if (iterator->layerId != normalizedLayerId)
            continue;
        foundLayer = true;
        if (iterator->crs == normalizedCrs)
        {
            if (errorMessage)
                errorMessage->clear();
            return true;
        }
    }
    setError(errorMessage,
             foundLayer
                 ? tr("三维测量坐标系与关联点云图层不一致。")
                 : tr("三维测量关联的点云图层未加载。"));
    return false;
}

bool MyVTK::isLayerVisible(const QString& layerId) const
{
    const QString normalized = layerId.trimmed().toLower();
    return std::any_of(
        cloudActors.cbegin(), cloudActors.cend(),
        [this, &normalized](vtkActor* actor) {
            const auto metadata = cloudActorMetadata.constFind(actor);
            return metadata != cloudActorMetadata.cend()
                && metadata->layerId == normalized;
        });
}

void MyVTK::updateMeasurementVisibility(
    const QString& layerId,
    bool visible)
{
    const QString normalized = layerId.trimmed().toLower();
    for (MeasurementVisual& visual : measurementVisuals)
    {
        if (visual.record.layerId.trimmed().toLower() != normalized)
            continue;
        const bool actorVisible = visible && visual.visible;
        visual.geometry->SetVisibility(actorVisible);
        visual.label->SetVisibility(actorVisible);
    }
}

void MyVTK::rebuildMeasurementPickList()
{
    measurementPicker->InitializePickList();
    for (vtkActor* actor : cloudActors)
    {
        if (actor)
            measurementPicker->AddPickList(actor);
    }
}

float MyVTK::getAABBCoordinateMax(pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud)
{
    return m_pointCloudManager->getAABBCoordinateMax(inCloud);
}

void MyVTK::computeCurrentCloudAABB(pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud)
{
    m_pointCloudManager->computeCurrentCloudAABB(inCloud);
}

void MyVTK::initCamera()
{
    float pos = m_pointCloudManager->getAABBCoordinateMax(orignalCloud);
    m_renderManager->initCamera(pos);
    this->update();
}

void MyVTK::refresh()
{
    m_renderManager->render();
    update();
}

//显示相机垂直面
vtkPlaneWidget * MyVTK::DisplayVerticalPlane()
{
    vtkCamera *camera = m_renderManager->camera();
    vtkRenderWindowInteractor *interactor = m_renderManager->renderWindowInteractor();

    double* viewUp = camera->GetViewUp();
    double* lookat = camera->GetDirectionOfProjection();

    double  viewright[3];
    vtkMath::Cross(viewUp, lookat, viewright);

    verticalPlane->SetInteractor(interactor);   //与交互器关联
    verticalPlane->SetNormal(viewright);
    verticalPlane->SetResolution(100);//即：设置网格数
    verticalPlane->GetPlaneProperty()->SetColor(0.5, .8, 0.5);//设置颜色
    verticalPlane->GetPlaneProperty()->SetOpacity(0.5);//设置透明度
    stylePlaneManipulator(verticalPlane, {0.0, 0.4, 0.7});
    verticalPlane->SetRepresentationToSurface();
    if (!placePlaneForCloud(
            verticalPlane, currentItemCloud,
            {viewright[0], viewright[1], viewright[2]},
            {viewUp[0], viewUp[1], viewUp[2]}))
    {
        verticalPlane->PlaceWidget();
    }
    return  verticalPlane;
}

//显示相机水平面
vtkPlaneWidget * MyVTK::DisplayHorizonalPlane()
{
    vtkCamera *camera = m_renderManager->camera();
    vtkRenderWindowInteractor *interactor = m_renderManager->renderWindowInteractor();

    double * viewUp;
    viewUp = camera->GetViewUp();

    horizonalPlane->SetInteractor(interactor);  //与交互器关联
    horizonalPlane->SetNormal(viewUp);
    horizonalPlane->SetResolution(100);//即：设置网格数
    horizonalPlane->GetPlaneProperty()->SetColor(0.4, 0.3, 0.8);//设置颜色
    horizonalPlane->GetPlaneProperty()->SetOpacity(0.7);//设置透明度
    stylePlaneManipulator(horizonalPlane, {0.0, 0.4, 0.7});
    horizonalPlane->SetRepresentationToSurface();
    const double* lookat = camera->GetDirectionOfProjection();
    if (!placePlaneForCloud(
            horizonalPlane, currentItemCloud,
            {viewUp[0], viewUp[1], viewUp[2]},
            {lookat[0], lookat[1], lookat[2]}))
    {
        horizonalPlane->PlaceWidget();
    }
    return  horizonalPlane;
}

//显示随机面
vtkPlaneWidget * MyVTK::DisplayRandomPlane()
{
    vtkRenderWindowInteractor *interactor = m_renderManager->renderWindowInteractor();

    randomPlane->SetInteractor(interactor); //与交互器关联
    randomPlane->SetResolution(100);//即：设置网格数
    randomPlane->GetPlaneProperty()->SetColor(0.5, 0.2, 0.6);//设置颜色
    randomPlane->GetPlaneProperty()->SetOpacity(0.6);//设置透明度
    stylePlaneManipulator(randomPlane, {0.6, 0.2, 0.3});
    randomPlane->SetRepresentationToSurface();
    randomPlane->SetNormalToYAxis(1.0);
    if (!placePlaneForCloud(
            randomPlane, currentItemCloud,
            {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}))
    {
        randomPlane->PlaceWidget();
    }
    return  randomPlane;
}

//开始进行切割
void MyVTK::beginClipPlane()
{
    clearPendingPlaneClip();
    isClipSucessed = false;
    //获得当前DB Tree下的点云
    emit sendGetDBItem();
    if (!currentItemCloud || currentItemCloud->empty()
        || !clipPlane || !vtkDlgClip)
    {
        return;
    }

    double threshold;
    this->vtkDlgClip->getSpinBoxValue(threshold);

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cutCloud;
    QVector<qint64> sourceIndices;
    m_projectionManager->beginClipPlane(
        currentItemCloud, clipPlane, threshold, currentSourcePointSize,
        m_renderManager->renderer(),
        cutCloud, sourceIndices);

    cloud_cut_plane = cutCloud;

    emit sendStrFromVTK2Console(QString::asprintf("共有%d个点在切割面上", static_cast<int>(cloud_cut_plane->size())));

    if (cloud_cut_plane->size() != 0)
    {
        const double* origin = clipPlane->GetOrigin();
        const double* normal = clipPlane->GetNormal();
        pendingClipSourceIndices = std::move(sourceIndices);
        pendingClipPlaneOrigin = QVector3D(
            origin[0], origin[1], origin[2]);
        pendingClipPlaneNormal = QVector3D(
            normal[0], normal[1], normal[2]);
        pendingClipThreshold = threshold;
        clipResultPending = true;
        vtkDlgClip->setConfirmButtonEnabled(true);
        emit sendStrFromVTK2Console(
            tr("切割预览已生成：%1 个点；点击“确认”后加入内容列表。")
                .arg(cloud_cut_plane->size()));
    }
    m_pointCloudManager->computeCurrentCloudAABB(currentItemCloud);

    m_renderManager->render();
    this->update();
}

void MyVTK::confirmClipPlane()
{
    if (!clipResultPending || !cloud_cut_plane
        || cloud_cut_plane->empty())
    {
        QMessageBox::warning(
            vtkDlgClip, tr("确认切割"),
            tr("没有可确认的切割预览，请先点击“开始切割”。"));
        return;
    }

    clipResultPending = false;
    if (vtkDlgClip)
        vtkDlgClip->setConfirmButtonEnabled(false);
    emit sendClipCloudResult(
        cloud_cut_plane, pendingClipSourceIndices,
        pendingClipPlaneOrigin, pendingClipPlaneNormal,
        pendingClipThreshold);
    isClipSucessed = true;
}

bool MyVTK::hasPendingClipResult() const
{
    return clipResultPending;
}

void MyVTK::clearPendingPlaneClip()
{
    pendingClipSourceIndices.clear();
    pendingClipPlaneOrigin = {};
    pendingClipPlaneNormal = {};
    pendingClipThreshold = 0.0;
    clipResultPending = false;
    if (vtkDlgClip)
        vtkDlgClip->setConfirmButtonEnabled(false);
}

//弹出切割平面对话框
void  MyVTK::setDlgClip()
{
    clearPendingPlaneClip();
    if (vtkDlgClip)
    {
        isActiveDlgClip = true;
        vtkDlgClip->setClipButtonEnable(true);
        vtkDlgClip->setConfirmButtonEnabled(false);
        vtkDlgClip->show();
        vtkDlgClip->raise();
        vtkDlgClip->activateWindow();
        return;
    }

    dlgClip = new QDlgClip(this);
    this->vtkDlgClip = dlgClip;
    isActiveDlgClip = true;
    this->vtkDlgClip->setAttribute(Qt::WA_DeleteOnClose);
    Qt::WindowFlags flags = dlgClip->windowFlags();
    this->vtkDlgClip->setWindowFlags(flags | Qt::WindowStaysOnTopHint);
    this->vtkDlgClip->setClipButtonEnable(true);
    this->vtkDlgClip->setConfirmButtonEnabled(false);
    this->vtkDlgClip->show();
}

//设置弹出的对话框是否隐藏
void MyVTK::setDlgClipVisible(bool enable)
{
    if (vtkDlgClip)
        vtkDlgClip->setVisible(enable);
}

void MyVTK::setQDlgClipNULL()
{
    clearPendingPlaneClip();
    this->dlgClip = NULL;
    this->vtkDlgClip = NULL;
}

void MyVTK::getDBItemCloud(
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud,
    double sourcePointSize)
{
    this->currentItemCloud = Cloud;
    currentSourcePointSize = std::isfinite(sourcePointSize)
        ? std::clamp(sourcePointSize, 1.0, 20.0) : 2.0;
    if (currentItemCloud && !currentItemCloud->empty())
        m_pointCloudManager->computeCurrentCloudAABB(currentItemCloud);
}

//切割结束后关闭掉切割平面和移除掉显示的选中的演员
void MyVTK::endCutRemoveActors()
{
    m_projectionManager->endCutRemoveActors(m_renderManager->renderer(),
        verticalPlane, randomPlane, horizonalPlane);
    m_renderManager->render();
    this->update();
}

void MyVTK::getAABBPolydata(vtkPolyData * polyData)
{
    if (AABB_Polydata == polyData)
        return;
    if (AABB_Polydata)
        AABB_Polydata->Delete();
    this->AABB_Polydata = polyData;
}

void MyVTK::getClipPlane(vtkPlaneWidget * plane)
{
    this->clipPlane = plane;
}

//从主窗口获得切割按钮禁用与否
void MyVTK::getPbnClipEnable(bool enable)
{
    if (this->vtkDlgClip)
    {
        this->vtkDlgClip->setClipButtonEnable(enable);
    }
}

//接受GraphicsView发送过来的演员
void MyVTK::getActorFromGraphicView(vtkActor * actor)
{
    m_renderManager->getActorFromGraphicView(actor);
    this->update();
}

//统一的投影到平面方法
void MyVTK::projectToPlane(famp::projection::Plane plane)
{
    emit sendGetDBItem();
    if (!currentItemCloud || currentItemCloud->empty())
    {
        QMessageBox::warning(
            this, tr("投影预览"),
            tr("请先在左侧内容列表中选择一个点云。"));
        return;
    }

    const famp::projection::Result result =
        famp::projection::projectToMinimumPlane(currentItemCloud, plane);
    if (!result.succeeded())
    {
        QMessageBox::warning(
            this, tr("投影预览"), result.error);
        return;
    }

    computeCurrentCloudAABB(currentItemCloud);
    m_projectionManager->showProjectionPreview(
        plane, result.points, currentSourcePointSize);

    // Render before notifying MainWindow. The receiving slot may display a
    // decision window; rendering after the signal made the preview appear
    // only after that UI was dismissed.
    m_renderManager->render();
    this->update();
    emit sendProjectedCloudPreview(result.points, plane);
    emit sendStrFromVTK2Console(
        tr("%1 投影预览已生成：%2 个点，尚未加入内容列表。")
            .arg(famp::projection::displayName(plane))
            .arg(result.points->size()));

}

void MyVTK::clearProjectionPreview()
{
    m_projectionManager->clearProjectionPreview();
    m_renderManager->render();
    update();
}

bool MyVTK::hasVisibleClipPreview() const
{
    return m_projectionManager->clipPreviewVisible();
}

bool MyVTK::hasVisibleProjectionPreview() const
{
    return m_projectionManager->projectionPreviewVisible();
}

double MyVTK::clipPreviewPointSize() const
{
    return m_projectionManager->clipPreviewPointSize();
}

double MyVTK::projectionPreviewPointSize() const
{
    return m_projectionManager->projectionPreviewPointSize();
}

std::array<double, 3> MyVTK::clipPreviewColor() const
{
    return m_projectionManager->clipPreviewColor();
}

std::array<double, 3> MyVTK::projectionPreviewColor() const
{
    return m_projectionManager->projectionPreviewColor();
}

//投影到YOZ面按钮
void MyVTK::slotActProjYOZ_triggered()
{
    projectToPlane(famp::projection::Plane::YOZ);
}

//投影到XOZ面按钮
void MyVTK::slotActProjXOZ_triggered()
{
    projectToPlane(famp::projection::Plane::XOZ);
}

//投影到XOY面按钮
void MyVTK::slotActProjXOY_triggered()
{
    projectToPlane(famp::projection::Plane::XOY);
}

//俯视投影按钮
void MyVTK::slotActOverLookProj_triggered()
{
    projectToPlane(famp::projection::Plane::Overlook);
}

//以AABB最小的坐标创建坐标轴
void MyVTK::AABBOrignalPosAxis(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud)
{
    m_pointCloudManager->AABBOrignalPosAxis(incloud, m_renderManager->renderer());
    m_renderManager->render();
    this->update();
}

//显示AABB最小的坐标创建坐标轴
void MyVTK::displayAABBOrignalPosAxis(bool enable)
{
    m_pointCloudManager->displayAABBOrignalPosAxis(enable);
    m_renderManager->render();
    this->update();
}

void MyVTK::setFrontView()
{
    float pos = m_pointCloudManager->getAABBCoordinateMax(orignalCloud);
    m_renderManager->setFrontView(pos);
    this->update();
}

void MyVTK::setTopView()
{
    float pos = m_pointCloudManager->getAABBCoordinateMax(orignalCloud);
    m_renderManager->setTopView(pos);
    this->update();
}

void MyVTK::setLeftView()
{
    float pos = m_pointCloudManager->getAABBCoordinateMax(orignalCloud);
    m_renderManager->setLeftView(pos);
    this->update();
}

void MyVTK::setRightView()
{
    float pos = m_pointCloudManager->getAABBCoordinateMax(orignalCloud);
    m_renderManager->setRightView(pos);
    this->update();
}

void MyVTK::setBottomView()
{
    float pos = m_pointCloudManager->getAABBCoordinateMax(orignalCloud);
    m_renderManager->setBottomView(pos);
    this->update();
}

void MyVTK::setBackView()
{
    float pos = m_pointCloudManager->getAABBCoordinateMax(orignalCloud);
    m_renderManager->setBackView(pos);
    this->update();
}

vtkActor * MyVTK::appendCloudActor()
{
    vtkPolyData * polyData = nullptr;
    vtkActor * cloudActor = m_pointCloudManager->appendCloudActor(
        m_renderManager->renderer(), orignalCloud, &polyData);
    if (cloudActor
        && std::find(cloudActors.cbegin(), cloudActors.cend(), cloudActor)
            == cloudActors.cend())
    {
        // Only actual point-cloud actors belong in the picker list.  AABB and
        // projection actors also pass through display(), but picking those
        // would produce measurements unrelated to the loaded cloud points.
        cloudActors.push_back(cloudActor);
        cloudActorMetadata.insert(cloudActor, {});
        rebuildMeasurementPickList();
    }
    if (polyData)
    {
        emit sendAABBPolydata(polyData);
    }
    this->update();
    return cloudActor;
}

//添加AABB包围盒演员
vtkActor * MyVTK::appendAABBActor()
{
    return m_pointCloudManager->appendAABBActor(AABB_Polydata);
}

bool MyVTK::updateCloudActors(
    vtkActor * cloudActor,
    vtkActor * aabbActor,
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud)
{
    if (!m_pointCloudManager->updateCloudActors(
            cloudActor, aabbActor, cloud))
    {
        return false;
    }
    m_renderManager->render();
    update();
    return true;
}

bool MyVTK::setCloudActorMetadata(vtkActor* actor,
                                  const QString& layerId,
                                  const QString& crs,
                                  QString* errorMessage)
{
    const QString normalizedLayerId = layerId.trimmed().toLower();
    if (!actor || !famp::cloud::isValidLayerId(normalizedLayerId))
    {
        setError(errorMessage, tr("点云演员或图层 ID 无效。"));
        return false;
    }
    const QString normalizedCrs = crs.trimmed().isEmpty()
        ? QString() : famp::crs::normalizedEpsg(crs);
    if (!crs.trimmed().isEmpty() && normalizedCrs.isEmpty())
    {
        setError(errorMessage, tr("点云图层坐标系无效。"));
        return false;
    }

    CloudActorMetadata metadata;
    metadata.layerId = normalizedLayerId;
    metadata.crs = normalizedCrs;
    cloudActorMetadata.insert(actor, metadata);
    if (errorMessage)
        errorMessage->clear();
    return true;
}

void MyVTK::unregisterCloudActor(vtkActor* actor)
{
    if (!actor)
        return;
    const auto metadata = cloudActorMetadata.constFind(actor);
    const QString layerId = metadata == cloudActorMetadata.cend()
        ? QString() : metadata->layerId;
    if (measurementActive && !layerId.isEmpty()
        && measurementLayerId == layerId)
    {
        resetMeasurementInteraction(true);
        emit measurementStatus(tr("点云图层已移除，当前三维测量已取消。"));
    }
    if (profileSelectionActive && !layerId.isEmpty()
        && profileSelectionLayerId == layerId)
    {
        resetProfileSelection(true);
        emit measurementStatus(tr("点云图层已移除，当前剖面线拾取已取消。"));
    }

    cloudActors.erase(std::remove(cloudActors.begin(), cloudActors.end(), actor),
                      cloudActors.end());
    cloudActorMetadata.remove(actor);
    rebuildMeasurementPickList();
    m_renderManager->removeCloudDisplay(actor);

    bool removedMeasurement = false;
    if (!layerId.isEmpty() && !hasRegisteredLayer(layerId))
    {
        for (auto iterator = measurementVisuals.begin();
             iterator != measurementVisuals.end();)
        {
            if (iterator->record.layerId.compare(
                    layerId, Qt::CaseInsensitive) != 0)
            {
                ++iterator;
                continue;
            }
            m_renderManager->renderer()->RemoveActor(iterator->geometry);
            m_renderManager->renderer()->RemoveActor(iterator->label);
            iterator = measurementVisuals.erase(iterator);
            removedMeasurement = true;
        }
    }
    m_renderManager->render();
    update();
    if (removedMeasurement)
        emit measurementsChanged();
}

void MyVTK::display(vtkActor * actor)
{
    if (!actor)
        return;
    const auto metadata = cloudActorMetadata.constFind(actor);
    if (metadata != cloudActorMetadata.cend()
        && std::find(cloudActors.cbegin(), cloudActors.cend(), actor)
            == cloudActors.cend())
    {
        cloudActors.push_back(actor);
        rebuildMeasurementPickList();
        if (!metadata->layerId.isEmpty())
            updateMeasurementVisibility(metadata->layerId, true);
    }
    m_renderManager->display(actor);
    m_renderManager->render();
    this->update();
}

void MyVTK::removeCloudDisplay(vtkActor * actor)
{
    if (!actor)
        return;
    const auto metadata = cloudActorMetadata.constFind(actor);
    if (metadata != cloudActorMetadata.cend()
        && measurementActive
        && !metadata->layerId.isEmpty()
        && measurementLayerId == metadata->layerId)
    {
        resetMeasurementInteraction(true);
        emit measurementStatus(tr("点云图层已隐藏，当前三维测量已取消。"));
    }
    if (metadata != cloudActorMetadata.cend()
        && profileSelectionActive
        && !metadata->layerId.isEmpty()
        && profileSelectionLayerId == metadata->layerId)
    {
        resetProfileSelection(true);
        emit measurementStatus(tr("点云图层已隐藏，当前剖面线拾取已取消。"));
    }
    cloudActors.erase(std::remove(cloudActors.begin(), cloudActors.end(), actor),
                      cloudActors.end());
    rebuildMeasurementPickList();
    if (metadata != cloudActorMetadata.cend()
        && !metadata->layerId.isEmpty())
    {
        updateMeasurementVisibility(metadata->layerId, false);
    }
    m_renderManager->removeCloudDisplay(actor);
    m_renderManager->render();
    this->update();
}

void MyVTK::removeAABBDisplay(vtkActor * actor)
{
    m_renderManager->removeAABBDisplay(actor);
    this->update();
}

void MyVTK::getOrignalCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud)
{
    this->orignalCloud = incloud;
}
