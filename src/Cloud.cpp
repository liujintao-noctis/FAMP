#include "Cloud.h"


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

	std::vector <float> moment_of_inertia;
	std::vector <float> eccentricity;

	Eigen::Matrix3f rotational_matrix_OBB;
	float major_value, middle_value, minor_value;
	Eigen::Vector3f major_vector, middle_vector, minor_vector;
	Eigen::Vector3f mass_center;
	
	feature_extractor.getMomentOfInertia(moment_of_inertia);
	feature_extractor.getEccentricity(eccentricity);
	feature_extractor.getOBB(min_point_OBB, max_point_OBB, position_OBB, rotational_matrix_OBB);
	feature_extractor.getEigenValues(major_value, middle_value, minor_value);
	feature_extractor.getEigenVectors(major_vector, middle_vector, minor_vector);
	feature_extractor.getMassCenter(mass_center);
	
}


pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud::computeDecentrationCloud()	//点云去中心化
{
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr outcloud(new	pcl::PointCloud<pcl::PointXYZRGB>);
	for (auto iter = cloud_origin->begin();iter != cloud_origin->end();iter++)
	{
		pcl::PointXYZRGB outpoint;
		outpoint.x = (*iter).x - centriod[0];
		outpoint.y = (*iter).y - centriod[1];
		outpoint.z = (*iter).z - centriod[2];
		outpoint.r = (*iter).r;
		outpoint.g = (*iter).g;
		outpoint.b = (*iter).b;

		outcloud->push_back(outpoint);
	}

	return outcloud;
}
