/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: V1.0
 * Description: VTK 投影与切割管理 — 平面裁剪、投影
 *****************************************************************/

#include "VTKProjectionManager.h"
#include "QstringAndStringConvert.h"
#include <vtkNew.h>

#include <QFileDialog>
#include <QMessageBox>
#include <QCoreApplication>

#include <cmath>
#include <cstdint>

VTKProjectionManager::VTKProjectionManager()
{
    // 切割的点显示的 VTK 数据初始化
    m_selectedPolyData = vtkSmartPointer<vtkPolyData>::New();
    m_selectedPoints = vtkSmartPointer<vtkPoints>::New();
    m_cellSelected = vtkSmartPointer<vtkCellArray>::New();
    m_selectedmapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_selectedActor = vtkSmartPointer<vtkActor>::New();
    m_selectedGlfilter = vtkSmartPointer<vtkVertexGlyphFilter>::New();

    // 切割的点云投影到面
    m_cloud_cut_projection.reset(new pcl::PointCloud<pcl::PointXYZRGB>);

    // 投影到平面的点
    m_planeProject = vtkSmartPointer<vtkPlane>::New();
    m_projectPointPolyData = vtkSmartPointer<vtkPolyData>::New();
    m_projectPoints = vtkSmartPointer<vtkPoints>::New();
    m_projectPointCell = vtkSmartPointer<vtkCellArray>::New();
    m_projectPointmapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_projectPointGlfilter = vtkSmartPointer<vtkVertexGlyphFilter>::New();
    m_projectPointActor = vtkSmartPointer<vtkActor>::New();
    m_projectPointActor->VisibilityOff();
}

VTKProjectionManager::~VTKProjectionManager()
{
}

void VTKProjectionManager::initProjectionActor(vtkRenderer *renderer)
{
    renderer->AddActor(m_projectPointActor);
}

void VTKProjectionManager::beginClipPlane(pcl::PointCloud<pcl::PointXYZRGB>::Ptr currentItemCloud,
    vtkPlaneWidget *clipPlane, double threshold,
    vtkRenderer *renderer, vtkNamedColors *colors,
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloudCutPlane,
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outCloudCutProjection)
{
    double *origin = clipPlane->GetOrigin();
    double *normal = clipPlane->GetNormal();

    outCloudCutPlane.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
    outCloudCutPlane->points.reserve(currentItemCloud ? currentItemCloud->size() : 0);
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
        }
    }
    outCloudCutPlane->width = static_cast<std::uint32_t>(outCloudCutPlane->size());
    outCloudCutPlane->height = 1;
    outCloudCutPlane->is_dense = false;

    // 将切割生成的点云副本保存用于投影
    outCloudCutProjection = outCloudCutPlane;

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
        m_selectedmapper->ScalarVisibilityOn();

        m_selectedActor->SetMapper(m_selectedmapper);
        m_selectedActor->GetProperty()->SetColor(colors->GetColor3d("Blue").GetData());
        m_selectedActor->GetProperty()->SetPointSize(5);

        renderer->AddActor(m_selectedActor);
    }

    if (outCloudCutPlane->points.empty())
    {
        renderer->RemoveActor(m_selectedActor);
    }
}

void VTKProjectionManager::projectToPlane(ProjectionPlane plane,
    QVector3D normal, pcl::PointXYZRGB P1,
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr sourceCloud,
    vtkRenderer *renderer, vtkNamedColors *colors,
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr& outProjectCloud,
    bool& outIsOverLook,
    const char*& outPlaneName)
{
    switch (plane)
    {
    case ProjectionPlane::YOZ:
        outPlaneName = "YOZ";
        outIsOverLook = false;
        break;
    case ProjectionPlane::XOZ:
        outPlaneName = "XOZ";
        outIsOverLook = false;
        break;
    case ProjectionPlane::XOY:
        outPlaneName = "XOY";
        outIsOverLook = false;
        break;
    case ProjectionPlane::OVERLOOK:
        outPlaneName = "XOY";
        outIsOverLook = true;
        break;
    }

    if (!colors) return;

    // 设置投影面，点法式
    vtkNew<vtkPlaneSource> project_plane;
    project_plane->SetCenter(P1.x, P1.y, P1.z);
    project_plane->SetNormal(normal.x(), normal.y(), normal.z());
    project_plane->Update();

    outProjectCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);

    // 将点投影到指定平面
    for (size_t i = 0; i < sourceCloud->size(); i++)
    {
        double projectPoint[3];
        double point[3];
        pcl::PointXYZRGB point_project;

        point[0] = sourceCloud->points[i].x;
        point[1] = sourceCloud->points[i].y;
        point[2] = sourceCloud->points[i].z;

        m_planeProject->ProjectPoint(point, project_plane->GetCenter(), project_plane->GetNormal(), projectPoint);

        point_project.x = projectPoint[0];
        point_project.y = projectPoint[1];
        point_project.z = projectPoint[2];

        outProjectCloud->push_back(point_project);
    }

    // 设置投影 actor 的外观
    const char* colorName = "LightPink";
    int pointSize = 3;
    switch (plane)
    {
    case ProjectionPlane::YOZ:  colorName = "SeaGreen";    pointSize = 3; break;
    case ProjectionPlane::XOZ:  colorName = "DeepSkyBlue";  pointSize = 3; break;
    case ProjectionPlane::XOY:  colorName = "LightPink";    pointSize = 3; break;
    case ProjectionPlane::OVERLOOK: colorName = "LightPink"; pointSize = 2; break;
    }

    m_projectPoints->Reset();
    vtkIdType projectidtype;

    for (size_t i = 0; i < outProjectCloud->size(); i++)
    {
        projectidtype = m_projectPoints->InsertNextPoint(outProjectCloud->points[i].x, outProjectCloud->points[i].y, outProjectCloud->points[i].z);
        m_projectPointCell->InsertNextCell(1, &projectidtype);
    }

    m_projectPointPolyData->SetPoints(m_projectPoints);
    m_projectPointPolyData->SetVerts(m_projectPointCell);
    m_projectPointGlfilter->SetInputData(m_projectPointPolyData);
    m_projectPointGlfilter->Update();
    m_projectPointmapper->SetInputData(m_projectPointGlfilter->GetOutput());
    m_projectPointmapper->ScalarVisibilityOn();
    m_projectPointmapper->Update();

    m_projectPointActor->SetMapper(m_projectPointmapper);
    m_projectPointActor->GetProperty()->SetColor(colors->GetColor3d(colorName).GetData());
    m_projectPointActor->GetProperty()->SetPointSize(pointSize);
    m_projectPointActor->VisibilityOn();
}

void VTKProjectionManager::endCutRemoveActors(vtkRenderer *renderer,
    vtkPlaneWidget *verticalPlane,
    vtkPlaneWidget *randomPlane,
    vtkPlaneWidget *horizonalPlane)
{
    renderer->RemoveActor(m_selectedActor);
    if (verticalPlane)  verticalPlane->Off();
    if (randomPlane)    randomPlane->Off();
    if (horizonalPlane) horizonalPlane->Off();
    m_projectPointActor->VisibilityOff();
}

bool VTKProjectionManager::saveDlgCloudFile(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_cut_plane,
    QWidget *parent, QString& outFilePath)
{
    if (cloud_cut_plane == NULL || cloud_cut_plane->empty())
    {
        QMessageBox::warning(parent, "警告", "未进行切割或切割点云数为0！");
        return false;
    }

    outFilePath = QFileDialog::getSaveFileName(parent, "保存文件", QCoreApplication::applicationDirPath(), "(*pcd);;所有文件(*.*)");
    if (outFilePath.isEmpty())
        return false;

    std::string pathPCD = qstr2str(outFilePath);
    pcl::io::savePCDFileASCII(pathPCD, *cloud_cut_plane);
    return true;
}
