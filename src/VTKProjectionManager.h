/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: VTK 投影与切割管理 — 平面裁剪、投影
 *****************************************************************/

#pragma once

#include "MyVTK.h"  // for ProjectionPlane enum definition

#include <QVector3D>
#include <QString>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <vtkActor.h>
#include <vtkSmartPointer.h>
#include <vtkCellArray.h>
#include <vtkNamedColors.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkPlane.h>
#include <vtkPlaneWidget.h>
#include <vtkPlaneSource.h>
#include <vtkVertexGlyphFilter.h>
#include <vtkRenderer.h>

// 投影平面枚举（定义在 MyVTK.h 中）
enum class ProjectionPlane : int;

// 管理切割、投影到平面、保存切割点云
class VTKProjectionManager
{
public:
    VTKProjectionManager();
    ~VTKProjectionManager();

    // 切割：计算切割面上的点并在 VTK 中显示
    // 返回：outCloudCutPlane 切割面上的点云, outCloudCutProjection 用于后续投影的点云副本
    void beginClipPlane(pcl::PointCloud<pcl::PointXYZRGB>::Ptr currentItemCloud,
        vtkPlaneWidget *clipPlane, double threshold,
        vtkRenderer *renderer, vtkNamedColors *colors,
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloudCutPlane,
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloudCutProjection);

    // 统一投影到平面方法
    void projectToPlane(ProjectionPlane plane,
        QVector3D normal, pcl::PointXYZRGB P1,
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr sourceCloud,
        vtkRenderer *renderer, vtkNamedColors *colors,
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outProjectCloud,
        bool& outIsOverLook,
        const char*& outPlaneName);

    // 清理切割相关演员
    void endCutRemoveActors(vtkRenderer *renderer,
        vtkPlaneWidget *verticalPlane,
        vtkPlaneWidget *randomPlane,
        vtkPlaneWidget *horizonalPlane);

    // 保存切割点云文件
    bool saveDlgCloudFile(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_cut_plane,
        QWidget *parent, QString& outFilePath);

    // 将投影 actor 添加到渲染器（构造后调用一次）
    void initProjectionActor(vtkRenderer *renderer);

    // 投影点云 getter/setter
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr getCloudCutProjection() { return m_cloud_cut_projection; }
    void setCloudCutProjection(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud) { m_cloud_cut_projection = cloud; }

private:
    // 切割点 VTK 对象
    vtkSmartPointer<vtkPolyData> m_selectedPolyData;
    vtkSmartPointer<vtkPoints> m_selectedPoints;
    vtkIdType m_selectedidtype;
    vtkSmartPointer<vtkCellArray> m_cellSelected;
    vtkSmartPointer<vtkPolyDataMapper> m_selectedmapper;
    vtkSmartPointer<vtkVertexGlyphFilter> m_selectedGlfilter;
    vtkSmartPointer<vtkActor> m_selectedActor;

    // 投影相关
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr m_cloud_cut_projection;

    vtkSmartPointer<vtkPlane> m_planeProject;
    vtkSmartPointer<vtkPolyData> m_projectPointPolyData;
    vtkSmartPointer<vtkPoints> m_projectPoints;
    vtkSmartPointer<vtkCellArray> m_projectPointCell;
    vtkSmartPointer<vtkPolyDataMapper> m_projectPointmapper;
    vtkSmartPointer<vtkVertexGlyphFilter> m_projectPointGlfilter;
    vtkSmartPointer<vtkActor> m_projectPointActor;
};
