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

void MyVTK::display(vtkActor * actor)
{
    m_renderManager->display(actor);
    this->update();
}

void MyVTK::removeCloudDisplay(vtkActor * actor)
{
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
