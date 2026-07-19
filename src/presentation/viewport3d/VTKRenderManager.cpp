/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: VTK 渲染管理 — 渲染器、相机、视图控制
 *****************************************************************/

#include "VTKRenderManager.h"

#include <vtkCaptionActor2D.h>
#include <vtkNew.h>
#include <vtkProperty.h>
#include <vtkTextProperty.h>

VTKRenderManager::VTKRenderManager(QWidget *parent)
    : m_parent(parent)
{
}

VTKRenderManager::~VTKRenderManager()
{
    if (m_orientationMarkerWidget)
    {
        m_orientationMarkerWidget->SetEnabled(0);
        m_orientationMarkerWidget->SetInteractor(nullptr);
    }
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
    if (!m_renderWindowInteractor)
        return;

    if (m_orientationMarkerWidget)
    {
        m_orientationMarkerWidget->SetEnabled(0);
        m_orientationMarkerWidget->SetInteractor(nullptr);
    }

    m_orientationAxes = vtkSmartPointer<vtkAxesActor>::New();
    m_orientationAxes->SetXAxisLabelText("X");
    m_orientationAxes->SetYAxisLabelText("Y");
    m_orientationAxes->SetZAxisLabelText("Z");
    m_orientationAxes->SetTotalLength(1.0, 1.0, 1.0);
    m_orientationAxes->SetNormalizedShaftLength(0.72, 0.72, 0.72);
    m_orientationAxes->SetNormalizedTipLength(0.28, 0.28, 0.28);
    m_orientationAxes->SetShaftTypeToCylinder();
    m_orientationAxes->SetCylinderRadius(0.035);
    m_orientationAxes->SetConeRadius(0.16);

    constexpr double xColor[] = {0.90, 0.18, 0.18};
    constexpr double yColor[] = {0.18, 0.72, 0.24};
    constexpr double zColor[] = {0.16, 0.40, 0.92};
    m_orientationAxes->GetXAxisShaftProperty()->SetColor(
        xColor[0], xColor[1], xColor[2]);
    m_orientationAxes->GetXAxisTipProperty()->SetColor(
        xColor[0], xColor[1], xColor[2]);
    m_orientationAxes->GetYAxisShaftProperty()->SetColor(
        yColor[0], yColor[1], yColor[2]);
    m_orientationAxes->GetYAxisTipProperty()->SetColor(
        yColor[0], yColor[1], yColor[2]);
    m_orientationAxes->GetZAxisShaftProperty()->SetColor(
        zColor[0], zColor[1], zColor[2]);
    m_orientationAxes->GetZAxisTipProperty()->SetColor(
        zColor[0], zColor[1], zColor[2]);

    const auto styleCaption = [](vtkCaptionActor2D* caption,
                                 const double color[3]) {
        if (!caption || !caption->GetCaptionTextProperty())
            return;
        vtkTextProperty* text = caption->GetCaptionTextProperty();
        text->SetColor(color[0], color[1], color[2]);
        text->BoldOn();
        text->ShadowOff();
    };
    styleCaption(m_orientationAxes->GetXAxisCaptionActor2D(), xColor);
    styleCaption(m_orientationAxes->GetYAxisCaptionActor2D(), yColor);
    styleCaption(m_orientationAxes->GetZAxisCaptionActor2D(), zColor);

    m_orientationMarkerWidget =
        vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    m_orientationMarkerWidget->SetOrientationMarker(m_orientationAxes);
    m_orientationMarkerWidget->SetInteractor(m_renderWindowInteractor);
    // Normalized viewport coordinates: fixed to the lower-left corner and
    // independent of point-cloud bounds or camera zoom.
    m_orientationMarkerWidget->SetViewport(0.0, 0.0, 0.18, 0.18);
    m_orientationMarkerWidget->SetOutlineColor(0.92, 0.92, 0.92);
    m_orientationMarkerWidget->SetEnabled(1);
    m_orientationMarkerWidget->SetInteractive(0);
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

void VTKRenderManager::render()
{
    if (m_renderWindow)
    {
        m_renderWindow->Render();
    }
}
