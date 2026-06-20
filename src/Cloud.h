#pragma once
#include <iostream>
#include <string>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/common/io.h>
#include <pcl/io/pcd_io.h>
#include <pcl/common/common.h>
#include <pcl/common/transforms.h>
#include <pcl/features/moment_of_inertia_estimation.h>
#include<qdebug.h>


class Cloud
{
public:
	Cloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud);
	~Cloud();

private:
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_origin;
	void computeCentriod();	//计算点云质心

	
public:
	Eigen::Vector4f centriod;		//点云质心

public:
	void computeOBB(pcl::PointXYZRGB &min_point_OBB, pcl::PointXYZRGB &max_point_OBB, pcl::PointXYZRGB &position_OBB);//计算获得点云OBB
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr computeDecentrationCloud();//获得去中心化的点云
};

