/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: VTK 渲染管理 — 渲染器、相机、视图控制
 *****************************************************************/

#pragma once

#include <QDebug>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <vtkAutoInit.h>
#include <vtkVersion.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkActor.h>
#include <vtkSmartPointer.h>
#include <vtkCameraActor.h>
#include <vtkCellArray.h>
#include <vtkNamedColors.h>
#include <vtkSphereSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkCamera.h>
#include <vtkAxesActor.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>

#if VTK_MAJOR_VERSION >= 9
#include <QVTKOpenGLNativeWidget.h>
#define FAMP_QVTK_WIDGET QVTKOpenGLNativeWidget
#else
#include <QVTKWidget.h>
#define FAMP_QVTK_WIDGET QVTKWidget
#endif

// 管理 VTK 渲染器、交互器、相机、视图控制、背景、坐标轴
class VTKRenderManager
{
public:
    VTKRenderManager(QWidget *parent);
    ~VTKRenderManager();

    void Init();
    void initColors();
    void mixBackGround();
    void setWidgetAxes();

    void initCamera(float pos);
    void setFrontView(float pos);
    void setTopView(float pos);
    void setLeftView(float pos);
    void setRightView(float pos);
    void setBottomView(float pos);
    void setBackView(float pos);

    void display(vtkActor *actor);
    void removeCloudDisplay(vtkActor *actor);
    void removeAABBDisplay(vtkActor *actor);
    void getActorFromGraphicView(vtkActor *actor);
    void render();

    // Getters for shared VTK objects
    vtkRenderer * renderer() const { return m_renderer; }
    vtkRenderWindowInteractor * renderWindowInteractor() const { return m_renderWindowInteractor; }
    vtkNamedColors * colors() const { return m_colors; }
    vtkCamera * camera() const { return m_camera; }

private:
    QWidget *m_parent;

    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderWindowInteractor> m_renderWindowInteractor;
    vtkSmartPointer<vtkNamedColors> m_colors;
    vtkSmartPointer<vtkCamera> m_camera;
};
