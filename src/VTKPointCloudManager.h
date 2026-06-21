/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: V1.0
 * Description: VTK 点云演员管理 — Actor 创建、AABB 包围盒
 *****************************************************************/

#pragma once

#include <QVector3D>
#include <QDebug>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/common/common.h>

#include <vtkActor.h>
#include <vtkSmartPointer.h>
#include <vtkCellArray.h>
#include <vtkNamedColors.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkLineSource.h>
#include <vtkOutlineFilter.h>
#include <vtkProperty.h>
#include <vtkPointData.h>
#include <vtkRenderer.h>
#include <vtkUnsignedCharArray.h>

#include <vector>

// 管理点云 Actor 创建、AABB 包围盒、AABB 坐标轴
class VTKPointCloudManager
{
public:
    VTKPointCloudManager();
    ~VTKPointCloudManager();

    // 点云 Actor 创建
    vtkActor * appendCloudActor(vtkRenderer * renderer,
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr orignalCloud,
        vtkPolyData ** outPolyData = nullptr);

    // AABB 包围盒 Actor
    vtkActor * appendAABBActor(vtkPolyData * AABB_Polydata);

    // AABB 计算
    float getAABBCoordinateMax(pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud);
    void computeCurrentCloudAABB(pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud);

    // AABB 最小坐标轴
    void AABBOrignalPosAxis(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud, vtkRenderer * renderer);
    void displayAABBOrignalPosAxis(bool enable);

    // AABB 计算结果 getter
    pcl::PointXYZRGB P1() const { return m_P1; }
    pcl::PointXYZRGB P2() const { return m_P2; }
    pcl::PointXYZRGB P3() const { return m_P3; }
    pcl::PointXYZRGB P4() const { return m_P4; }
    float XMaxMin() const { return m_XMaxMin; }
    float YMaxMin() const { return m_YMaxMin; }
    float ZMaxMin() const { return m_ZMaxMin; }
    QVector3D XOYNormal() const { return m_XOYNormal; }
    QVector3D XOZNormal() const { return m_XOZNormal; }
    QVector3D YOZNormal() const { return m_YOZNormal; }

private:
    // AABB 计算相关
    pcl::PointXYZRGB m_min_point_AABB;
    pcl::PointXYZRGB m_max_point_AABB;
    pcl::PointXYZRGB m_P1;
    pcl::PointXYZRGB m_P2;
    pcl::PointXYZRGB m_P3;
    pcl::PointXYZRGB m_P4;
    float m_XMaxMin;
    float m_YMaxMin;
    float m_ZMaxMin;
    QVector3D m_XOYNormal;
    QVector3D m_XOZNormal;
    QVector3D m_YOZNormal;

    // AABB 最小坐标轴 VTK 对象
    vtkSmartPointer<vtkLineSource> m_lineScourceX;
    vtkSmartPointer<vtkLineSource> m_lineScourceY;
    vtkSmartPointer<vtkLineSource> m_lineScourceZ;
    vtkSmartPointer<vtkPolyDataMapper> m_mapperX;
    vtkSmartPointer<vtkPolyDataMapper> m_mapperY;
    vtkSmartPointer<vtkPolyDataMapper> m_mapperZ;
    vtkSmartPointer<vtkActor> m_actorX;
    vtkSmartPointer<vtkActor> m_actorY;
    vtkSmartPointer<vtkActor> m_actorZ;
};
