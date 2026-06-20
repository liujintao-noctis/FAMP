/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: V1.0
 * Description: VTK 渲染管理 — 渲染器、相机、视图控制
 *****************************************************************/

#include "VTKRenderManager.h"
#include <vtkNew.h>

VTKRenderManager::VTKRenderManager(QWidget *parent)
    : m_parent(parent)
{
}

VTKRenderManager::~VTKRenderManager()
{
}

void VTKRenderManager::Init()
{
    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();

    m_renderWindow->AddRenderer(m_renderer);
#if VTK_MAJOR_VERSION >= 9
    FAMP_QVTK_WIDGET * qvtkWidget = qobject_cast<FAMP_QVTK_WIDGET *>(m_parent);
    if (qvtkWidget) {
        qvtkWidget->setRenderWindow(m_renderWindow);
        m_renderWindowInteractor = qvtkWidget->interactor();
    }
#else
    m_renderWindowInteractor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
    m_renderWindowInteractor->SetRenderWindow(m_renderWindow);
    FAMP_QVTK_WIDGET * qvtkWidget = qobject_cast<FAMP_QVTK_WIDGET *>(m_parent);
    if (qvtkWidget) {
        qvtkWidget->SetRenderWindow(m_renderWindow);
    }
    m_renderWindowInteractor->Initialize();
#endif

    m_colors = vtkSmartPointer<vtkNamedColors>::New();
    m_camera = vtkSmartPointer<vtkCamera>::New();

    // 鼠标交互
    vtkNew<vtkInteractorStyleTrackballCamera> style;
    m_renderWindowInteractor->SetInteractorStyle(style);
}

void VTKRenderManager::initColors()
{
    m_colors = vtkSmartPointer<vtkNamedColors>::New();
}

void VTKRenderManager::mixBackGround()
{
    m_renderer->GradientBackgroundOn();
    m_renderer->SetBackground(m_colors->GetColor3d("SlateGray").GetData());
    m_renderer->SetBackground2(m_colors->GetColor3d("Wheat").GetData());
}

void VTKRenderManager::setWidgetAxes()
{
    vtkNew<vtkAxesActor> axesActor;
    vtkNew<vtkOrientationMarkerWidget> widgetAxes;
    widgetAxes->SetOrientationMarker(axesActor);
    widgetAxes->SetInteractor(m_renderWindowInteractor);
    widgetAxes->SetEnabled(1);
    widgetAxes->SetInteractive(0);
}

void VTKRenderManager::initCamera(float pos)
{
    m_camera->SetPosition(0, 0, pos);
    m_camera->SetFocalPoint(0, 0, 0);
    m_camera->SetViewUp(0, 1, 0);
    m_camera->ParallelProjectionOn();
    m_renderer->SetActiveCamera(m_camera);
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VTKRenderManager::setFrontView(float pos)
{
    m_camera->SetViewUp(0, 0, 1);
    m_camera->SetFocalPoint(0, 0, 0);
    m_camera->SetPosition(pos, 0, 0);
    m_camera->ParallelProjectionOn();
    m_renderer->SetActiveCamera(m_camera);
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VTKRenderManager::setTopView(float pos)
{
    m_camera->SetViewUp(-1, 0, 0);
    m_camera->SetFocalPoint(0, 0, 0);
    m_camera->SetPosition(0, 0, pos);
    m_camera->ParallelProjectionOn();
    m_renderer->SetActiveCamera(m_camera);
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VTKRenderManager::setLeftView(float pos)
{
    m_camera->SetViewUp(0, 1, 0);
    m_camera->SetFocalPoint(0, 0, 0);
    m_camera->SetPosition(0, -pos, 0);
    m_camera->ParallelProjectionOn();
    m_renderer->SetActiveCamera(m_camera);
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VTKRenderManager::setRightView(float pos)
{
    m_camera->SetViewUp(0, 1, 0);
    m_camera->SetFocalPoint(0, 0, 0);
    m_camera->SetPosition(0, pos, 0);
    m_camera->ParallelProjectionOn();
    m_renderer->SetActiveCamera(m_camera);
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VTKRenderManager::setBottomView(float pos)
{
    m_camera->SetViewUp(1, 0, 0);
    m_camera->SetFocalPoint(0, 0, 0);
    m_camera->SetPosition(0, 0, -pos);
    m_camera->ParallelProjectionOn();
    m_renderer->SetActiveCamera(m_camera);
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VTKRenderManager::setBackView(float pos)
{
    m_camera->SetViewUp(0, 0, 1);
    m_camera->SetFocalPoint(0, 0, 0);
    m_camera->SetPosition(-pos, 0, 0);
    m_camera->ParallelProjectionOn();
    m_renderer->SetActiveCamera(m_camera);
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VTKRenderManager::display(vtkActor *actor)
{
    m_renderer->AddActor(actor);
    m_renderWindow->Render();
}

void VTKRenderManager::removeCloudDisplay(vtkActor *actor)
{
    m_renderer->RemoveActor(actor);
    m_renderWindow->Render();
}

void VTKRenderManager::removeAABBDisplay(vtkActor *actor)
{
    m_renderer->RemoveActor(actor);
    m_renderWindow->Render();
}

void VTKRenderManager::getActorFromGraphicView(vtkActor *actor)
{
    m_renderer->AddActor(actor);
    m_renderWindow->Render();
}
