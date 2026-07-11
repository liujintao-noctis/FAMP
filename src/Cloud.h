/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 点云工具类 — 质心计算、OBB包围盒、去中心化
 *****************************************************************/

#pragma once

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/common/io.h>
#include <pcl/io/pcd_io.h>
#include <pcl/common/common.h>
#include <pcl/common/transforms.h>

#include <QDebug>

class Cloud
{
public:
    Cloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud);
    ~Cloud();

    Eigen::Vector4f centriod;       //点云质心

    void computeOBB(pcl::PointXYZRGB &min_point_OBB, pcl::PointXYZRGB &max_point_OBB, pcl::PointXYZRGB &position_OBB);//计算获得点云OBB
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr computeDecentrationCloud();//获得去中心化的点云

private:
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_origin;
    void computeCentriod(); //计算点云质心
};
