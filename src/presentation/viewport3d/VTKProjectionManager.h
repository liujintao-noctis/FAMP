/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: VTK 投影与切割管理 — 平面裁剪、投影
 *****************************************************************/

#pragma once

#include "CloudProjection.h"

#include <QVector3D>
#include <QVector>
#include <QString>

#include <array>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <vtkActor.h>
#include <vtkSmartPointer.h>
#include <vtkCellArray.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkPlane.h>
#include <vtkPlaneWidget.h>
#include <vtkVertexGlyphFilter.h>
#include <vtkRenderer.h>

// 管理切割预览与平面投影
class VTKProjectionManager
{
public:
    VTKProjectionManager();
    ~VTKProjectionManager();

    // 切割：计算切割面上的点并在 VTK 中显示。后续投影从
    // 内容列表当前实体取数，不在渲染管理器中缓存点云。
    void beginClipPlane(pcl::PointCloud<pcl::PointXYZRGB>::Ptr currentItemCloud,
        vtkPlaneWidget *clipPlane, double threshold, double sourcePointSize,
        vtkRenderer *renderer,
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloudCutPlane,
        QVector<qint64>& outSourceIndices);

    // 显示已经由 application 层计算完成的瞬时投影预览。
    void showProjectionPreview(
        famp::projection::Plane plane,
        const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud,
        double sourcePointSize);
    void clearProjectionPreview();
    bool clipPreviewVisible() const;
    bool projectionPreviewVisible() const;
    double clipPreviewPointSize() const;
    double projectionPreviewPointSize() const;
    std::array<double, 3> clipPreviewColor() const;
    std::array<double, 3> projectionPreviewColor() const;

    // 清理切割相关演员
    void endCutRemoveActors(vtkRenderer *renderer,
        vtkPlaneWidget *verticalPlane,
        vtkPlaneWidget *randomPlane,
        vtkPlaneWidget *horizonalPlane);

    // 将投影 actor 添加到渲染器（构造后调用一次）
    void initProjectionActor(vtkRenderer *renderer);

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
    vtkSmartPointer<vtkPolyData> m_projectPointPolyData;
    vtkSmartPointer<vtkPoints> m_projectPoints;
    vtkSmartPointer<vtkCellArray> m_projectPointCell;
    vtkSmartPointer<vtkPolyDataMapper> m_projectPointmapper;
    vtkSmartPointer<vtkVertexGlyphFilter> m_projectPointGlfilter;
    vtkSmartPointer<vtkActor> m_projectPointActor;
};
