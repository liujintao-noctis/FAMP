/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 点云工具类 — 质心计算、OBB包围盒、去中心化
 *****************************************************************/

#define PCL_NO_PRECOMPILE
#include "Cloud.h"

#include <pcl/features/moment_of_inertia_estimation.h>
#include <pcl/features/impl/moment_of_inertia_estimation.hpp>

Cloud::Cloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud):
    cloud_origin (NULL)
{

    cloud_origin = cloud;

    computeCentriod();
    //qDebug() << "centriod"<<centriod[0]<< centriod[1]<< centriod[2];
}

Cloud::~Cloud()
{
}

void Cloud::computeCentriod()
{
    pcl::compute3DCentroid(*cloud_origin, centriod);

}

void Cloud::computeOBB(pcl::PointXYZRGB & min_point_OBB, pcl::PointXYZRGB & max_point_OBB, pcl::PointXYZRGB & position_OBB)
{
    pcl::MomentOfInertiaEstimation <pcl::PointXYZRGB> feature_extractor;
    feature_extractor.setInputCloud(cloud_origin);
    feature_extractor.compute();

    Eigen::Matrix3f rotational_matrix_OBB;

    feature_extractor.getOBB(min_point_OBB, max_point_OBB, position_OBB, rotational_matrix_OBB);

}

pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud::computeDecentrationCloud()    //点云去中心化
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr outcloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    outcloud->reserve(cloud_origin->size());
    pcl::copyPointCloud(*cloud_origin, *outcloud);
    for (auto& point : outcloud->points)
    {
        point.x -= centriod[0];
        point.y -= centriod[1];
        point.z -= centriod[2];
    }

    return outcloud;
}
