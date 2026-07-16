/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: VTK 3D 视图门面 — 平面切割、投影
 *****************************************************************/

#include "MyVTK.h"
#include "VTKRenderManager.h"
#include "VTKPointCloudManager.h"
#include "VTKProjectionManager.h"

#include <QDebug>
#include <QTimer>

#include <vtkCellArray.h>
#include <vtkCommand.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper.h>
#include <vtkTextProperty.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace
{
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
    isProjectionSuccess = false;

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
    randomPlane = vtkPlaneWidget::New();
    horizonalPlane = vtkPlaneWidget::New();
    verticalPlane = vtkPlaneWidget::New();

    connect(this, SIGNAL(sendAABBPolydata(vtkPolyData *)), this, SLOT(getAABBPolydata(vtkPolyData *))); //传送AABBPolyData
}

MyVTK::~MyVTK()
{
    if (m_renderManager && m_renderManager->renderWindowInteractor())
    {
        for (const unsigned long tag : measurementObserverTags)
            m_renderManager->renderWindowInteractor()->RemoveObserver(tag);
    }
    if (measurementPreviewActor)
        m_renderManager->renderer()->RemoveActor(measurementPreviewActor);
    if (measurementPreviewLabel)
        m_renderManager->renderer()->RemoveActor(measurementPreviewLabel);
    for (const MeasurementVisual& visual : measurementVisuals)
    {
        m_renderManager->renderer()->RemoveActor(visual.geometry);
        m_renderManager->renderer()->RemoveActor(visual.label);
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
    if (!pickMeasurementPoint(pickedPoint))
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

bool MyVTK::pickMeasurementPoint(QVector3D& point)
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

    const double* picked = measurementPicker->GetPickPosition();
    if (!picked || !std::isfinite(picked[0])
        || !std::isfinite(picked[1]) || !std::isfinite(picked[2]))
    {
        return false;
    }
    point = QVector3D(static_cast<float>(picked[0]),
                      static_cast<float>(picked[1]),
                      static_cast<float>(picked[2]));
    return true;
}

void MyVTK::beginMeasurement(famp::measurement::Kind kind, bool announce)
{
    resetMeasurementInteraction(false);
    measurementActive = true;
    measurementKind = kind;
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

    const QString resultText = famp::measurement::formatSummary(
        measurementKind, measurementPoints);
    addMeasurementVisual(measurementPoints, resultText);
    resetMeasurementInteraction(true);
    emit measurementStatus(tr("点云测量完成：%1").arg(resultText));
}

void MyVTK::addMeasurementVisual(const QVector<QVector3D>& points,
                                 const QString& labelText)
{
    MeasurementVisual visual;
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(measurementPolyData(points, measurementKind));
    mapper->ScalarVisibilityOff();
    visual.geometry = vtkSmartPointer<vtkActor>::New();
    visual.geometry->SetMapper(mapper);
    styleMeasurementActor(visual.geometry, 0.0, 0.8, 1.0);

    visual.label = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    const QByteArray encodedLabel = labelText.toUtf8();
    visual.label->SetInput(encodedLabel.constData());
    const QVector3D& position = points.back();
    visual.label->SetPosition(position.x(), position.y(), position.z());
    styleMeasurementLabel(visual.label, 0.2, 0.9, 1.0);

    m_renderManager->renderer()->AddActor(visual.geometry);
    m_renderManager->renderer()->AddActor(visual.label);
    measurementVisuals.push_back(std::move(visual));
}

int MyVTK::measurementCount() const
{
    return static_cast<int>(measurementVisuals.size());
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

    if (!announce)
        return;
    if (count == 0)
        emit measurementStatus(tr("当前点云视图没有测量结果。"));
    else
        emit measurementStatus(tr("已清除 %1 个点云测量结果。").arg(count));
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

    double *viewUp;
    double *posistion;
    double *facalPosition;
    double *lookat;
    double distance;

    viewUp = camera->GetViewUp();
    posistion = camera->GetPosition();
    facalPosition = camera->GetFocalPoint();
    lookat = camera->GetDirectionOfProjection();
    distance = camera->GetDistance();

    double  viewright[3];
    vtkMath::Cross(viewUp, lookat, viewright);

    verticalPlane->SetInteractor(interactor);   //与交互器关联
    verticalPlane->SetNormal(viewright);
    verticalPlane->SetResolution(100);//即：设置网格数
    verticalPlane->GetPlaneProperty()->SetColor(0.5, .8, 0.5);//设置颜色
    verticalPlane->GetPlaneProperty()->SetOpacity(0.5);//设置透明度
    verticalPlane->GetHandleProperty()->SetColor(0, .4, .7);//设置平面顶点颜色;
    verticalPlane->GetHandleProperty()->SetLineWidth(3.0);//设置平面线宽
    verticalPlane->GetHandleProperty()->SetPointSize(8.0);  //设置平面顶点大小;
    verticalPlane->SetRepresentationToSurface();
    verticalPlane->PlaceWidget();//放置平面
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
    horizonalPlane->GetHandleProperty()->SetColor(0, .4, .7);//设置平面顶点颜色;
    horizonalPlane->GetHandleProperty()->SetLineWidth(3.0);//设置平面线宽
    horizonalPlane->GetHandleProperty()->SetPointSize(8.0); //设置平面顶点大小;
    horizonalPlane->SetRepresentationToSurface();
    horizonalPlane->PlaceWidget();//放置平面
    return  horizonalPlane;
}

//显示随机面
vtkPlaneWidget * MyVTK::DisplayRandomPlane()
{
    vtkCamera *camera = m_renderManager->camera();
    vtkRenderWindowInteractor *interactor = m_renderManager->renderWindowInteractor();

    qDebug() << "camera->GetDistance" << camera->GetDistance();

    randomPlane->SetInteractor(interactor); //与交互器关联
    randomPlane->SetResolution(100);//即：设置网格数
    randomPlane->GetPlaneProperty()->SetColor(0.5, 0.2, 0.6);//设置颜色
    randomPlane->GetPlaneProperty()->SetOpacity(0.6);//设置透明度
    randomPlane->GetHandleProperty()->SetColor(0.6 ,0.2, 0.3);//设置平面顶点颜色;
    randomPlane->GetHandleProperty()->SetPointSize(8.0);    //设置平面顶点大小;
    randomPlane->GetHandleProperty()->SetLineWidth(3.0);//设置平面线宽
    randomPlane->SetRepresentationToSurface();
    randomPlane->SetNormalToYAxis(1.0);
    return  randomPlane;
}

//开始进行切割
void MyVTK::beginClipPlane()
{
    //获得当前DB Tree下的点云
    emit sendGetDBItem();
    if (currentItemCloud->empty())      return;

    double threshold;
    this->vtkDlgClip->getSpinBoxValue(threshold);

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cutCloud;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cutProjection;
    m_projectionManager->beginClipPlane(currentItemCloud, clipPlane, threshold,
        m_renderManager->renderer(), m_renderManager->colors(),
        cutCloud, cutProjection);

    cloud_cut_plane = cutCloud;
    m_projectionManager->setCloudCutProjection(cutProjection);

    emit sendStrFromVTK2Console(QString::asprintf("共有%d个点在切割面上", static_cast<int>(cloud_cut_plane->size())));

    if (cloud_cut_plane->size() != 0)
    {
        isClipSucessed = true;
        triggeredSignalXOYProj();
        triggeredSignalXOZProj();
        triggeredSignalYOZProj();
        triggeredSignalOverLookProj();
    }
    else
    {
        isClipSucessed = false;
        triggeredSignalXOYProj();
        triggeredSignalXOZProj();
        triggeredSignalYOZProj();
        triggeredSignalOverLookProj();
    }

    m_pointCloudManager->computeCurrentCloudAABB(currentItemCloud);
    emit sendAABBBoxXYZMAX(m_pointCloudManager->XMaxMin(), m_pointCloudManager->YMaxMin(), m_pointCloudManager->ZMaxMin());

    m_renderManager->render();
    this->update();
}

//弹出切割平面对话框
void  MyVTK::setDlgClip()
{
    dlgClip = new QDlgClip(this);
    this->vtkDlgClip = dlgClip;
    isActiveDlgClip = true;
    this->vtkDlgClip->setAttribute(Qt::WA_DeleteOnClose);
    Qt::WindowFlags flags = dlgClip->windowFlags();
    this->vtkDlgClip->setWindowFlags(flags | Qt::WindowStaysOnTopHint);
    this->vtkDlgClip->setClipButtonEnable(true);
    this->vtkDlgClip->show();
}

//设置弹出的对话框是否隐藏
void MyVTK::setDlgClipVisible(bool enable)
{
    this->vtkDlgClip->setVisible(enable);
}

void MyVTK::setQDlgClipNULL()
{
    this->dlgClip = NULL;
    this->vtkDlgClip = NULL;
}

void MyVTK::getDBItemCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud)
{
    this->currentItemCloud = Cloud;
}

//切割结束后关闭掉切割平面和移除掉显示的选中的演员
void MyVTK::endCutRemoveActors()
{
    m_projectionManager->endCutRemoveActors(m_renderManager->renderer(),
        verticalPlane, randomPlane, horizonalPlane);
    m_renderManager->render();
    this->update();
}

//保存点云文件
void MyVTK::saveDlgCloudFile()
{
    QString filePath;
    bool ok = m_projectionManager->saveDlgCloudFile(cloud_cut_plane, dlgClip, filePath);
    if (ok && !filePath.isEmpty())
    {
        emit sendStrFromVTK2Console("保存切割点云到路径" + filePath + "成功！");
    }
}

//触发sendActProjLineEnable(bool enable)信号
void MyVTK::triggeredSignalProj()
{
    emit sendActProjLineEnable(isProjectionSuccess);
}

//触发发送投影到XOY按钮的信号
void MyVTK::triggeredSignalXOYProj()
{
    emit sendActProjXOYEnable(isClipSucessed);
}

//触发发送投影到XOZ按钮的信号
void MyVTK::triggeredSignalXOZProj()
{
    emit sendActProjXOZEnable(isClipSucessed);
}

//触发发送投影到YOZ按钮的信号
void MyVTK::triggeredSignalYOZProj()
{
    emit sendActProjYOZEnable(isClipSucessed);
}

//触发发送俯视投影按钮的信号
void MyVTK::triggeredSignalOverLookProj()
{
    sendActOverLookProjEnable(isClipSucessed);
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
void MyVTK::projectToPlane(ProjectionPlane plane)
{
    //根据投影面选择法向量、源点云等信息
    QVector3D normal;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr sourceCloud;
    bool isOverLook = false;

    switch (plane)
    {
    case ProjectionPlane::YOZ:
        normal = m_pointCloudManager->YOZNormal();
        sourceCloud = m_projectionManager->getCloudCutProjection();
        break;
    case ProjectionPlane::XOZ:
        normal = m_pointCloudManager->XOZNormal();
        sourceCloud = m_projectionManager->getCloudCutProjection();
        break;
    case ProjectionPlane::XOY:
        normal = m_pointCloudManager->XOYNormal();
        sourceCloud = m_projectionManager->getCloudCutProjection();
        break;
    case ProjectionPlane::OVERLOOK:
        normal = m_pointCloudManager->XOYNormal();
        isOverLook = true;
        break;
    }

    //俯视投影：先获得当前DB Tree下的点云
    if (isOverLook)
    {
        emit sendGetDBItem();
        if (currentItemCloud->empty()) return;
        sourceCloud = currentItemCloud;
    }

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr outProjectCloud;
    const char* planeName;
    m_projectionManager->projectToPlane(plane, normal, m_pointCloudManager->P1(),
        sourceCloud,
        m_renderManager->renderer(), m_renderManager->colors(),
        outProjectCloud, isOverLook, planeName);

    //保存投影点云文件
    const char* saveFileName;
    switch (plane) {
    case ProjectionPlane::YOZ:      saveFileName = "projectPointCloudYOZ.pcd"; break;
    case ProjectionPlane::XOZ:      saveFileName = "projectPointCloudXOZ.pcd"; break;
    case ProjectionPlane::XOY:      saveFileName = "projectPointCloudXOY.pcd"; break;
    case ProjectionPlane::OVERLOOK: saveFileName = "projectPointCloudOverLook.pcd"; break;
    }
    // 调试：保存投影点云
    // pcl::io::savePCDFileASCII(saveFileName, *outProjectCloud);

    //投影完成，触发米格纸投影连线按钮
    isProjectionSuccess = true;
    emit sendActProjLineEnable(isProjectionSuccess);
    if (isOverLook)
    {
        emit sendIsOverLookProj(true);
        emit sendProjCloud2GraphicsView(outProjectCloud);
        emit sendStrFromVTK2Console(QString::asprintf("俯视投影完成！共有%d个点在%s面上", static_cast<int>(outProjectCloud->size()), planeName));
    }
    else
    {
        emit sendProjCloud2GraphicsView(outProjectCloud);
        emit sendStrFromVTK2Console(QString::asprintf("%s面投影完成！共有%d个点在%s面上", planeName, static_cast<int>(outProjectCloud->size()), planeName));
    }

    m_renderManager->render();
    this->update();
}

//投影到YOZ面按钮
void MyVTK::slotActProjYOZ_triggered()
{
    projectToPlane(ProjectionPlane::YOZ);
}

//投影到XOZ面按钮
void MyVTK::slotActProjXOZ_triggered()
{
    projectToPlane(ProjectionPlane::XOZ);
}

//投影到XOY面按钮
void MyVTK::slotActProjXOY_triggered()
{
    projectToPlane(ProjectionPlane::XOY);
}

//俯视投影按钮
void MyVTK::slotActOverLookProj_triggered()
{
    projectToPlane(ProjectionPlane::OVERLOOK);
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
    clearMeasurements(false);
    m_renderManager->render();
    update();
    return true;
}

void MyVTK::display(vtkActor * actor)
{
    m_renderManager->display(actor);
    this->update();
}

void MyVTK::removeCloudDisplay(vtkActor * actor)
{
    const bool removedPointCloud = std::find(
        cloudActors.cbegin(), cloudActors.cend(), actor) != cloudActors.cend();
    cloudActors.erase(std::remove(cloudActors.begin(), cloudActors.end(), actor),
                      cloudActors.end());
    rebuildMeasurementPickList();
    if (removedPointCloud)
        clearMeasurements(false);
    m_renderManager->removeCloudDisplay(actor);
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
