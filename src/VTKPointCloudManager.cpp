/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: V1.0
 * Description: VTK 点云演员管理 — Actor 创建、AABB 包围盒
 *****************************************************************/

#include "VTKPointCloudManager.h"
#include <vtkNew.h>
#include <algorithm>
#include <cmath>

VTKPointCloudManager::VTKPointCloudManager()
{
    // AABB 坐标轴初始化
    m_lineScourceX = vtkSmartPointer<vtkLineSource>::New();
    m_lineScourceY = vtkSmartPointer<vtkLineSource>::New();
    m_lineScourceZ = vtkSmartPointer<vtkLineSource>::New();
    m_mapperX = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_mapperY = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_mapperZ = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_actorX = vtkSmartPointer<vtkActor>::New();
    m_actorY = vtkSmartPointer<vtkActor>::New();
    m_actorZ = vtkSmartPointer<vtkActor>::New();

    m_XMaxMin = 0.0f;
    m_YMaxMin = 0.0f;
    m_ZMaxMin = 0.0f;
}

VTKPointCloudManager::~VTKPointCloudManager()
{
}

float VTKPointCloudManager::getAABBCoordinateMax(pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud)
{
    pcl::PointXYZRGB AABB_Max, AABB_min;
    if (inCloud->size() == 0) return 1.0;
    pcl::getMinMax3D(*inCloud, AABB_min, AABB_Max);

    std::vector<float> vec;
    vec.reserve(inCloud->size());
    vec.push_back(AABB_Max.x);
    vec.push_back(AABB_Max.y);
    vec.push_back(AABB_Max.z);
    vec.push_back(AABB_min.x);
    vec.push_back(AABB_min.y);
    vec.push_back(AABB_min.z);

    std::sort(vec.begin(), vec.end(), [](float p1, float p2) {return p1 < p2; });

    float coordinateM = (vec.at(5) - vec.at(0)) * 0.75;
    return static_cast<float>(std::fabs(coordinateM));
}

vtkActor * VTKPointCloudManager::appendCloudActor(vtkRenderer * renderer,
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr orignalCloud,
    vtkPolyData ** outPolyData)
{
    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> cell;
    vtkNew<vtkLookupTable> lookup;
    vtkNew<vtkPolyData> polyData;
    vtkNew<vtkFloatArray> myscalar;
    vtkNew<vtkPolyVertex> polyVertex;
    vtkNew<vtkFloatArray> pointsScalars;
    vtkNew<vtkActor> cloudActor;

    lookup->SetNumberOfTableValues(orignalCloud->size());
    lookup->Build();

    vtkIdType idtype;
    for (size_t i = 0; i < orignalCloud->points.size(); i++)
    {
        idtype = points->InsertNextPoint(orignalCloud->points[i].x, orignalCloud->points[i].y, orignalCloud->points[i].z);
        myscalar->InsertNextTuple1(1);
        lookup->SetTableValue(i, (double)orignalCloud->points[i].r / 255.0, (double)orignalCloud->points[i].g / 255.0, (double)orignalCloud->points[i].b / 255.0, 1);
        cell->InsertNextCell(1, &idtype);
    }

    polyData->SetPoints(points);
    polyData->SetVerts(cell);
    polyData->GetPointData()->SetScalars(myscalar);

    if (outPolyData)
    {
        *outPolyData = polyData;
        (*outPolyData)->Register(nullptr);  // 防止 vtkNew 析构时释放对象
    }

    polyVertex->GetPointIds()->SetNumberOfIds(orignalCloud->size());
    pointsScalars->SetNumberOfTuples(orignalCloud->size());
    for (size_t i = 0; i < orignalCloud->size(); i++)
    {
        polyVertex->GetPointIds()->SetId(i, i);
        pointsScalars->InsertValue(i, i);
    }

    vtkNew<vtkUnstructuredGrid> cloudGrid;
    cloudGrid->Allocate(1, 1);
    cloudGrid->SetPoints(points);
    cloudGrid->GetPointData()->SetScalars(pointsScalars);
    cloudGrid->InsertNextCell(polyVertex->GetCellType(), polyVertex->GetPointIds());

    vtkNew<vtkDataSetMapper> cloudMapper;
    cloudMapper->SetInputData(cloudGrid);
    cloudMapper->ScalarVisibilityOn();
    cloudMapper->SetScalarRange(0, static_cast<double>(orignalCloud->size() - 1));
    cloudMapper->SetLookupTable(lookup);

    vtkNew<vtkIdFilter> idFilter;
    idFilter->SetInputData(polyData);
#if VTK_MAJOR_VERSION >= 9
    idFilter->SetPointIdsArrayName("OriginalIds");
    idFilter->SetCellIdsArrayName("OriginalIds");
#else
    idFilter->SetIdsArrayName("OriginalIds");
#endif
    vtkNew<vtkDataSetSurfaceFilter> sufaceFilter;
    sufaceFilter->SetInputConnection(idFilter->GetOutputPort());
    sufaceFilter->Update();

    cloudActor->SetMapper(cloudMapper);
    cloudActor->GetProperty()->SetPointSize(2);
    cloudActor->GetProperty()->SetRepresentationToPoints();
    renderer->AddActor(cloudActor);

    // vtkNew 析构时会 Delete，调用 Register 防止返回的裸指针悬空
    cloudActor->Register(nullptr);
    return cloudActor;
}

vtkActor * VTKPointCloudManager::appendAABBActor(vtkPolyData * AABB_Polydata)
{
    double bound[6];
    if (AABB_Polydata) {
        AABB_Polydata->GetBounds(bound);
    }
    qDebug() << "OBB Size:\t"
        << bound[1] - bound[0] << ", " << bound[3] - bound[2] << ", " << bound[5] - bound[4];

    vtkNew<vtkOutlineFilter> AABBOutlineData;
    AABBOutlineData->SetInputData(AABB_Polydata);
    AABBOutlineData->Update();

    vtkNew<vtkPolyDataMapper> mapOutline;
    mapOutline->SetInputConnection(AABBOutlineData->GetOutputPort());

    vtkNew<vtkActor> AABBActor;
    AABBActor->SetMapper(mapOutline);
    AABBActor->GetProperty()->SetColor(1, 0, 0);

    AABBActor->Register(nullptr);  // 防止 vtkNew 析构时释放对象
    return AABBActor;
}

void VTKPointCloudManager::computeCurrentCloudAABB(pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud)
{
    pcl::getMinMax3D(*inCloud, m_min_point_AABB, m_max_point_AABB);

    m_P1.x = m_min_point_AABB.x;
    m_P1.y = m_min_point_AABB.y;
    m_P1.z = m_min_point_AABB.z;

    m_P2.x = m_max_point_AABB.x;
    m_P2.y = m_min_point_AABB.y;
    m_P2.z = m_min_point_AABB.z;

    m_P3.x = m_min_point_AABB.x;
    m_P3.y = m_max_point_AABB.y;
    m_P3.z = m_min_point_AABB.z;

    m_P4.x = m_min_point_AABB.x;
    m_P4.y = m_min_point_AABB.y;
    m_P4.z = m_max_point_AABB.z;

    m_XOYNormal = QVector3D(m_P4.x - m_P1.x, m_P4.y - m_P1.y, m_P4.z - m_P1.z);
    m_XOZNormal = QVector3D(m_P3.x - m_P1.x, m_P3.y - m_P1.y, m_P3.z - m_P3.z);
    m_YOZNormal = QVector3D(m_P2.x - m_P1.x, m_P2.y - m_P1.y, m_P2.z - m_P1.z);

    m_XMaxMin = m_P2.x - m_P1.x;
    m_YMaxMin = m_P3.y - m_P1.y;
    m_ZMaxMin = m_P4.z - m_P1.z;
}

void VTKPointCloudManager::AABBOrignalPosAxis(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud, vtkRenderer * renderer)
{
    pcl::PointXYZRGB min_point;
    pcl::PointXYZRGB max_point;
    pcl::getMinMax3D(*incloud, min_point, max_point);

    QVector3D P1, P2, P3, P4;

    P1.setX(min_point.x);
    P1.setY(min_point.y);
    P1.setZ(min_point.z);

    P2.setX(max_point.x);
    P2.setY(min_point.y);
    P2.setZ(min_point.z);

    P3.setX(min_point.x);
    P3.setY(max_point.y);
    P3.setZ(min_point.z);

    P4.setX(min_point.x);
    P4.setY(min_point.y);
    P4.setZ(max_point.z);

    QVector3D X_Normal, Z_Normal, Y_Normal;
    X_Normal = QVector3D(P2.x() - P1.x(), P2.y() - P1.y(), P2.z() - P1.z());
    Y_Normal = QVector3D(P3.x() - P1.x(), P3.y() - P1.y(), P3.z() - P3.z());
    Z_Normal = QVector3D(P4.x() - P1.x(), P4.y() - P1.y(), P4.z() - P1.z());

    X_Normal.normalize();
    Y_Normal.normalize();
    Z_Normal.normalize();

    float XMax = max_point.x - min_point.x;
    float YMax = max_point.y - min_point.y;
    float ZMax = max_point.z - min_point.z;
    std::vector<float> max;
    max.push_back(XMax);
    max.push_back(YMax);
    max.push_back(ZMax);
    std::sort(max.begin(), max.end(), [=](float temp1, float temp2) {return temp1 > temp2; });

    float max_element = max.front();
    float line_length = max_element / 6.0;

    m_lineScourceX->SetPoint1(min_point.x, min_point.y, min_point.z);
    m_lineScourceX->SetPoint2(min_point.x + X_Normal.x()*line_length, min_point.y + X_Normal.y()*line_length, min_point.z + X_Normal.z()*line_length);
    m_lineScourceX->Update();
    m_mapperX->SetInputConnection(m_lineScourceX->GetOutputPort());
    m_actorX->SetMapper(m_mapperX);
    m_actorX->GetProperty()->SetColor(1, 0, 0);
    m_actorX->GetProperty()->SetLineWidth(5.0);

    m_lineScourceY->SetPoint1(min_point.x, min_point.y, min_point.z);
    m_lineScourceY->SetPoint2(min_point.x + Y_Normal.x()*line_length, min_point.y + Y_Normal.y()*line_length, min_point.z + Y_Normal.z()*line_length);
    m_lineScourceY->Update();
    m_mapperY->SetInputConnection(m_lineScourceY->GetOutputPort());
    m_actorY->SetMapper(m_mapperY);
    m_actorY->GetProperty()->SetColor(0, 1, 0);
    m_actorY->GetProperty()->SetLineWidth(5.0);

    m_lineScourceZ->SetPoint1(min_point.x, min_point.y, min_point.z);
    m_lineScourceZ->SetPoint2(min_point.x + Z_Normal.x()*line_length, min_point.y + Z_Normal.y()*line_length, min_point.z + Z_Normal.z()*line_length);
    m_lineScourceZ->Update();
    m_mapperZ->SetInputConnection(m_lineScourceZ->GetOutputPort());
    m_actorZ->SetMapper(m_mapperZ);
    m_actorZ->GetProperty()->SetColor(0, 0, 1);
    m_actorZ->GetProperty()->SetLineWidth(5.0);

    m_actorX->SetVisibility(false);
    m_actorY->SetVisibility(false);
    m_actorZ->SetVisibility(false);

    renderer->AddActor(m_actorX);
    renderer->AddActor(m_actorY);
    renderer->AddActor(m_actorZ);
}

void VTKPointCloudManager::displayAABBOrignalPosAxis(bool enable)
{
    m_actorX->SetVisibility(enable);
    m_actorY->SetVisibility(enable);
    m_actorZ->SetVisibility(enable);
}
