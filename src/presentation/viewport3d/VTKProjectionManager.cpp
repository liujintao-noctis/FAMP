/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: VTK 投影与切割管理 — 平面裁剪、投影
 *****************************************************************/

#include "VTKProjectionManager.h"
#include <vtkNew.h>
#include <vtkProperty.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace
{
double emphasizedPointSize(double sourcePointSize, double increment,
                           double minimum)
{
    const double normalized = std::isfinite(sourcePointSize)
        ? std::clamp(sourcePointSize, 1.0, 20.0)
        : 2.0;
    return std::clamp(normalized + increment, minimum, 24.0);
}

std::array<double, 3> projectionColor(famp::projection::Plane plane)
{
    switch (plane)
    {
    case famp::projection::Plane::YOZ:
        return {0.0, 0.9, 1.0};
    case famp::projection::Plane::XOZ:
        return {0.45, 1.0, 0.15};
    case famp::projection::Plane::XOY:
        return {1.0, 0.15, 0.65};
    case famp::projection::Plane::Overlook:
        return {0.68, 0.35, 1.0};
    }
    return {1.0, 0.15, 0.65};
}

std::array<double, 3> actorColor(vtkActor* actor)
{
    if (!actor || !actor->GetProperty())
        return {};
    const double* color = actor->GetProperty()->GetColor();
    return {color[0], color[1], color[2]};
}
}

VTKProjectionManager::VTKProjectionManager()
{
    // 切割的点显示的 VTK 数据初始化
    m_selectedPolyData = vtkSmartPointer<vtkPolyData>::New();
    m_selectedPoints = vtkSmartPointer<vtkPoints>::New();
    m_cellSelected = vtkSmartPointer<vtkCellArray>::New();
    m_selectedmapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_selectedActor = vtkSmartPointer<vtkActor>::New();
    m_selectedGlfilter = vtkSmartPointer<vtkVertexGlyphFilter>::New();
    m_selectedActor->VisibilityOff();
    m_selectedActor->PickableOff();

    // 投影到平面的点
    m_projectPointPolyData = vtkSmartPointer<vtkPolyData>::New();
    m_projectPoints = vtkSmartPointer<vtkPoints>::New();
    m_projectPointCell = vtkSmartPointer<vtkCellArray>::New();
    m_projectPointmapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_projectPointGlfilter = vtkSmartPointer<vtkVertexGlyphFilter>::New();
    m_projectPointActor = vtkSmartPointer<vtkActor>::New();
    m_projectPointActor->VisibilityOff();
    m_projectPointActor->PickableOff();
}

VTKProjectionManager::~VTKProjectionManager()
{
}

void VTKProjectionManager::initProjectionActor(vtkRenderer *renderer)
{
    renderer->AddActor(m_projectPointActor);
}

void VTKProjectionManager::beginClipPlane(pcl::PointCloud<pcl::PointXYZRGB>::Ptr currentItemCloud,
    vtkPlaneWidget *clipPlane, double threshold, double sourcePointSize,
    vtkRenderer *renderer,
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloudCutPlane,
    QVector<qint64>& outSourceIndices)
{
    double *origin = clipPlane->GetOrigin();
    double *normal = clipPlane->GetNormal();

    outCloudCutPlane.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
    outCloudCutPlane->points.reserve(currentItemCloud ? currentItemCloud->size() : 0);
    outSourceIndices.clear();
    m_selectedPoints->Reset();
    m_cellSelected->Reset();
    m_selectedPolyData->Initialize();

    for (size_t i = 0; currentItemCloud && i < currentItemCloud->size(); i++)
    {
        const auto& sourcePoint = currentItemCloud->points[i];
        double Point[3];
        Point[0] = sourcePoint.x;
        Point[1] = sourcePoint.y;
        Point[2] = sourcePoint.z;

        double distance = vtkPlane::DistanceToPlane(Point, normal, origin);

        if (std::fabs(distance) <= threshold)
        {
            pcl::PointXYZRGB pointSelected = sourcePoint;
            pointSelected.x = Point[0];
            pointSelected.y = Point[1];
            pointSelected.z = Point[2];
            outCloudCutPlane->push_back(pointSelected);
            outSourceIndices.append(static_cast<qint64>(i));
        }
    }
    outCloudCutPlane->width = static_cast<std::uint32_t>(outCloudCutPlane->size());
    outCloudCutPlane->height = 1;
    outCloudCutPlane->is_dense = false;

    // 在 VTK 中显示切割的点
    if (!outCloudCutPlane->points.empty())
    {
        for (size_t i = 0; i < outCloudCutPlane->size(); i++)
        {
            m_selectedidtype = m_selectedPoints->InsertNextPoint(outCloudCutPlane->points[i].x, outCloudCutPlane->points[i].y, outCloudCutPlane->points[i].z);
            m_cellSelected->InsertNextCell(1, &m_selectedidtype);
        }
        m_selectedPolyData->SetPoints(m_selectedPoints);
        m_selectedPolyData->SetVerts(m_cellSelected);
        m_selectedGlfilter->SetInputData(m_selectedPolyData);
        m_selectedGlfilter->Update();
        m_selectedmapper->SetInputData(m_selectedGlfilter->GetOutput());
        m_selectedmapper->ScalarVisibilityOff();

        m_selectedActor->SetMapper(m_selectedmapper);
        m_selectedActor->GetProperty()->SetColor(1.0, 0.58, 0.0);
        m_selectedActor->GetProperty()->SetPointSize(
            emphasizedPointSize(sourcePointSize, 3.5, 5.5));
        m_selectedActor->GetProperty()->RenderPointsAsSpheresOn();
        m_selectedActor->GetProperty()->LightingOff();
        m_selectedActor->GetProperty()->SetOpacity(1.0);
        m_selectedActor->VisibilityOn();

        renderer->AddActor(m_selectedActor);
    }

    if (outCloudCutPlane->points.empty())
    {
        m_selectedActor->VisibilityOff();
        renderer->RemoveActor(m_selectedActor);
    }
}

void VTKProjectionManager::showProjectionPreview(
    famp::projection::Plane plane,
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud,
    double sourcePointSize)
{
    if (!cloud || cloud->empty())
    {
        clearProjectionPreview();
        return;
    }

    const std::array<double, 3> color = projectionColor(plane);

    m_projectPoints->Reset();
    m_projectPointCell->Reset();
    vtkIdType projectidtype;

    for (const pcl::PointXYZRGB& point : cloud->points)
    {
        projectidtype = m_projectPoints->InsertNextPoint(
            point.x, point.y, point.z);
        m_projectPointCell->InsertNextCell(1, &projectidtype);
    }

    m_projectPointPolyData->SetPoints(m_projectPoints);
    m_projectPointPolyData->SetVerts(m_projectPointCell);
    m_projectPointGlfilter->SetInputData(m_projectPointPolyData);
    m_projectPointGlfilter->Update();
    m_projectPointmapper->SetInputData(m_projectPointGlfilter->GetOutput());
    m_projectPointmapper->ScalarVisibilityOff();
    m_projectPointmapper->Update();

    m_projectPointActor->SetMapper(m_projectPointmapper);
    m_projectPointActor->GetProperty()->SetColor(
        color[0], color[1], color[2]);
    m_projectPointActor->GetProperty()->SetPointSize(
        emphasizedPointSize(sourcePointSize, 2.5, 4.5));
    m_projectPointActor->GetProperty()->RenderPointsAsSpheresOn();
    m_projectPointActor->GetProperty()->LightingOff();
    m_projectPointActor->GetProperty()->SetOpacity(1.0);
    m_projectPointActor->VisibilityOn();
}

void VTKProjectionManager::clearProjectionPreview()
{
    m_projectPoints->Reset();
    m_projectPointCell->Reset();
    m_projectPointPolyData->Initialize();
    m_projectPointActor->VisibilityOff();
}

bool VTKProjectionManager::clipPreviewVisible() const
{
    return m_selectedActor && m_selectedActor->GetVisibility() != 0
        && m_selectedPoints && m_selectedPoints->GetNumberOfPoints() > 0;
}

bool VTKProjectionManager::projectionPreviewVisible() const
{
    return m_projectPointActor && m_projectPointActor->GetVisibility() != 0
        && m_projectPoints && m_projectPoints->GetNumberOfPoints() > 0;
}

double VTKProjectionManager::clipPreviewPointSize() const
{
    return m_selectedActor && m_selectedActor->GetProperty()
        ? m_selectedActor->GetProperty()->GetPointSize() : 0.0;
}

double VTKProjectionManager::projectionPreviewPointSize() const
{
    return m_projectPointActor && m_projectPointActor->GetProperty()
        ? m_projectPointActor->GetProperty()->GetPointSize() : 0.0;
}

std::array<double, 3> VTKProjectionManager::clipPreviewColor() const
{
    return actorColor(m_selectedActor);
}

std::array<double, 3> VTKProjectionManager::projectionPreviewColor() const
{
    return actorColor(m_projectPointActor);
}

void VTKProjectionManager::endCutRemoveActors(vtkRenderer *renderer,
    vtkPlaneWidget *verticalPlane,
    vtkPlaneWidget *randomPlane,
    vtkPlaneWidget *horizonalPlane)
{
    renderer->RemoveActor(m_selectedActor);
    m_selectedActor->VisibilityOff();
    if (verticalPlane)  verticalPlane->Off();
    if (randomPlane)    randomPlane->Off();
    if (horizonalPlane) horizonalPlane->Off();
}
