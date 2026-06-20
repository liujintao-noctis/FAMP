#include "MyGraphicsView.h"
#include"MainWindow.h"
Q_DECLARE_METATYPE(MyOrderCloudType)


MyGraphicsView::MyGraphicsView(QWidget *parent)
	: QGraphicsView(parent)
{
	//初始化指针
	project_cloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
	currentItemCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);

	dlgPlotTab = NULL;
	

	labelScene = new QLabel("Scene坐标");
	labelScene->setMinimumWidth(200);
	labelScene->adjustSize();

	isOverLookProjLine = false;	//是否进行俯视XOY点云连线

	this->setCursor(Qt::CrossCursor);	//设置鼠标样式
	this->setMouseTracking(true);		//设置鼠标追踪
	this->setDragMode(QGraphicsView::RubberBandDrag);	//选中鼠标框选内容

	scaleType = OneToFifty;			//比例尺默认1:50
	deltaOffset = QPointF(86.0, 86.0);

	scene = new QGraphicsScene(-1500, -1500, 3000, 3000);
	this->setScene(scene);

	
}

MyGraphicsView::~MyGraphicsView()
{
}

void MyGraphicsView::getDBItemCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud)
{
	this->currentItemCloud = Cloud;
}

//判断投影的点云是投影到哪个面
ProjectType MyGraphicsView::projectType(pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud)
{
	ProjectType str;
	std::vector<float> vecX;
	std::vector<float> vecY;
	std::vector<float> vecZ;
	if (Cloud->size() < 10)	 return NONE;	//切割投影的点云数过少，无法连线！

	//选取10个点的索引进行检测判断
	vector<int> pointindexs;
	
	int stripe = Cloud->size() / 10;
	for (size_t i = 0; i < 10; i++)
	{
		int index = stripe * i;
		//qDebug() << i << "	" << index << endl;
		pointindexs.push_back(index);
	}

	
	for (int i = 0; i < pointindexs.size(); i++)
	{
		//qDebug() << i;
		vecX.push_back(Cloud->points[pointindexs[i]].x);
		vecY.push_back(Cloud->points[pointindexs[i]].y);
		vecZ.push_back(Cloud->points[pointindexs[i]].z);
	}

	for (size_t i = 0; i < 10;)
	{
		if (vecX[i] == vecX[0])
		{
			if (i == 9) { str = YOZ; }
			++i;
		}
		else if (vecY[i] == vecY[0])
		{
			if (i == 9) { str = XOZ; }
			++i;
		}
		else if (vecZ[i] == vecZ[0])
		{
			if (i == 9 && isOverLookProjLine) { str = OLXOY; }
			else if(i == 9 && !isOverLookProjLine) { str = XOY; }
			++i;
		}
	}

	return str;
}

//是否绘制切割线对话框
int MyGraphicsView::dlgDrawXOYLine()
{
	QString dlgTitle = "XOY面投影线绘制";
	QString strInfo = "是否生成剖面切割线？";

	emit sendDlgClipVisible(false);		//将VTK的平面裁剪对话框隐藏
	QMessageBox msg;
	msg.setText(strInfo);
	msg.setWindowTitle(dlgTitle);
	msg.setStandardButtons(QMessageBox::Yes | QMessageBox::No| QMessageBox::Cancel);
	msg.setDefaultButton(QMessageBox::Cancel);
	int result = msg.exec();

	switch (result)
	{
	case(QMessageBox::Yes):
	{
		return 1;		//绘制剖面线
	}
	break;

	case(QMessageBox::No):
	{
		return 0;	//不绘制剖面线
	}
	break;

	case(QMessageBox::Cancel):
	{
		return -1;			//取消
	}
	break;

	default:
		break;
	} 
}

//计算点云的平均密度（点与点之间的平均距离）
double MyGraphicsView::computeCloudMeanDis(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud)
{
	double meanDis = 0.0;	//平均距离
	int numberOfPoints = 0;		//有效的点云数
	vector<int> indices;		
	vector<float> squareDistances;

	pcl::search::KdTree< pcl::PointXYZRGB> kdtree;
	kdtree.setInputCloud(cloud);

	for (size_t i = 0; i < cloud->size(); i++)
	{
		if (!pcl::isFinite(cloud->points[i]))		continue;		//检查该值是否为正常值（有限）

		if (kdtree.nearestKSearch(i, 2, indices, squareDistances) > 0)
		{
			meanDis += sqrt(squareDistances[1]);
			++numberOfPoints;
		}
	}

	if (numberOfPoints != 0)
	{
		meanDis /= numberOfPoints;
	}
	return meanDis;
}

//画出XOZ面的投影
void MyGraphicsView::drawXOZ(QPointF offset)
{
	//二维点绘制到米格纸上
	//QVector<QPointF> points_xoz;
	//PCLCloud2QTPoints(project_cloud, XOZ, points_xoz);
	//for (size_t i = 0; i < points_xoz.size(); i++)
	//{
	//	float pointSize = 0.5;
	//	QGraphicsEllipseItem *pointItem = new QGraphicsEllipseItem(-pointSize, -pointSize, 2 * pointSize, 2 * pointSize);
	//	pointItem->setFlags(QGraphicsItem::ItemIsFocusable | QGraphicsItem::ItemIsMovable |
	//		QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusScope);
	//	pointItem->setBrush(QBrush(Qt::black));
	//	//点的位置信息
	//	pointItem->setPos(points_xoz[i].x(), points_xoz[i].y());
	//	scene->addItem(pointItem);
	//	scene->clearSelection();
	//	pointItem->setSelected(true);
	//}

	//计算投影点云的平均密度
	double density = computeCloudMeanDis(project_cloud);

	//将投影的点云转为有序点云
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr  orderCloudXOZ(new pcl::PointCloud<pcl::PointXYZRGB>);
	orderSoetCloud(project_cloud, project_cloud->size() / 3, orderCloudXOZ);
	pcl::io::savePCDFileASCII("ordercloud_xoz.pcd", *orderCloudXOZ);

	//将有序化的点云用DP进行简化
	vector<DPPoint> points_DP_XOZ;
	for (size_t i = 0; i < orderCloudXOZ->size(); i++)
	{
		DPPoint point;
		point.x = orderCloudXOZ->points[i].x;
		point.y = orderCloudXOZ->points[i].y;
		point.z = orderCloudXOZ->points[i].z;
		point.ID = i;

		points_DP_XOZ.push_back(point);
	}

	computeDP(points_DP_XOZ, 0, points_DP_XOZ.size() - 1, density);		//DP简化

	//将DP点转为PCL点云
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_DP_XOZ(new pcl::PointCloud<pcl::PointXYZRGB>);
	for (size_t i = 0; i < points_DP_XOZ.size(); i++)
	{
		if (points_DP_XOZ[i].isRemoved == true)	continue;

		pcl::PointXYZRGB point;
		point.x = points_DP_XOZ.at(i).x;
		point.y = points_DP_XOZ.at(i).y;
		point.z = points_DP_XOZ.at(i).z;

		cloud_DP_XOZ->push_back(point);
	}

	if (!cloud_DP_XOZ->empty())		pcl::io::savePCDFileASCII("cloud_DP_YOZ.pcd", *cloud_DP_XOZ);
	qDebug() << "原始点数:" << orderCloudXOZ->size() << "\t" << "DP简化后点云数：" << "\t" << cloud_DP_XOZ->size();


	//将有序点云按照比例尺转为QT点集
	QVector<QPointF> points_xoz;
	PCLCloud2QTPoints(orderCloudXOZ, XOZ, points_xoz,QPointF(offset.x(), offset.y()));
	qDebug() << "points_xoz" << points_xoz.size();

	//将点云传入到重写的QGraphicsItem中绘制
	MyItem *itemXOZ = new MyItem(points_xoz, XOZ);
	scene->addItem(itemXOZ);
	scene->clearSelection();
	scene->selectedItems().clear();
	itemXOZ->setSelected(true);
	MyOrderCloudType xoz_myordercloud;
	xoz_myordercloud.orderCloud = cloud_DP_XOZ;
	xoz_myordercloud.project_type = XOZ;
	itemXOZ->setData(ItemName, "XOZ面投影");
	itemXOZ->setData(ItemCloud, QVariant::fromValue(xoz_myordercloud));
	emit sendStrFromGraphicView2Console(QString::asprintf("已生成XOZ面投影连线！"));
}

//画出YOZ面的投影
void MyGraphicsView::drawYOZ(QPointF offset)
{
	//二维点绘制到米格纸上
	//QVector<QPointF> _points_yoz;
	//PCLCloud2QTPoints(project_cloud, YOZ, _points_yoz);
	//for (size_t i = 0; i < _points_yoz.size(); i++)
	//{
	//	float pointSize = 0.5;
	//	QGraphicsEllipseItem *pointItem = new QGraphicsEllipseItem(-pointSize, -pointSize, 2 * pointSize, 2 * pointSize);
	//	pointItem->setFlags(QGraphicsItem::ItemIsFocusable | QGraphicsItem::ItemIsMovable |
	//		QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusScope);
	//	pointItem->setBrush(QBrush(Qt::black));
	//	//点的位置信息
	//	pointItem->setPos(_points_yoz[i].x(), _points_yoz[i].y());
	//	scene->addItem(pointItem);
	//	scene->clearSelection();
	//	pointItem->setSelected(true);
	//}

	//计算投影点云的平均密度
	double density = computeCloudMeanDis(project_cloud);

	//将投影的点云转为有序点云
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr  orderCloudYOZ(new pcl::PointCloud<pcl::PointXYZRGB>);
	orderSoetCloud(project_cloud, project_cloud->size() / 3, orderCloudYOZ);
	pcl::io::savePCDFileASCII("ordercloud_yoz.pcd", *orderCloudYOZ);

	//将有序化的点云用DP进行简化
	vector<DPPoint> points_DP_YOZ;
	for (size_t i = 0; i < orderCloudYOZ->size(); i++)
	{
		DPPoint point;
		point.x = orderCloudYOZ->points[i].x;
		point.y = orderCloudYOZ->points[i].y;
		point.z = orderCloudYOZ->points[i].z;
		point.ID = i;

		points_DP_YOZ.push_back(point);
	}

	computeDP(points_DP_YOZ, 0, points_DP_YOZ.size() - 1, density);		//DP简化

	//将DP点转为PCL点云
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_DP_YOZ(new pcl::PointCloud<pcl::PointXYZRGB>);
	for (size_t i = 0; i < points_DP_YOZ.size(); i++)
	{
		if (points_DP_YOZ[i].isRemoved == true)	continue;

		pcl::PointXYZRGB point;
		point.x = points_DP_YOZ.at(i).x;
		point.y = points_DP_YOZ.at(i).y;
		point.z = points_DP_YOZ.at(i).z;

		cloud_DP_YOZ->push_back(point);
	}

	if (!cloud_DP_YOZ->empty())		pcl::io::savePCDFileASCII("cloud_DP_YOZ.pcd", *cloud_DP_YOZ);
	qDebug() << "原始点数:" << orderCloudYOZ->size() << "\t" << "DP简化后点云数：" << "\t" << cloud_DP_YOZ->size();


	//将DP简化后的有序点云转为QT点集
	QVector<QPointF> points_yoz;
	PCLCloud2QTPoints(cloud_DP_YOZ, YOZ, points_yoz,QPointF(offset.x(), offset.y()));
	qDebug() << "points_yoz" << points_yoz.size();

	//将点云传入到重写的QGraphicsItem中绘制
	MyItem *itemYOZ = new MyItem(points_yoz, YOZ);
	scene->addItem(itemYOZ);
	scene->clearSelection();
	scene->selectedItems().clear();
	itemYOZ->setSelected(true);
	MyOrderCloudType yoz_myordercloud;
	yoz_myordercloud.orderCloud = cloud_DP_YOZ;
	yoz_myordercloud.project_type = YOZ;
	itemYOZ->setData(ItemName, "YOZ面投影");
	itemYOZ->setData(ItemCloud, QVariant::fromValue(yoz_myordercloud));
	emit sendStrFromGraphicView2Console(QString::asprintf("已生成YOZ面投影连线！"));
	
}

//画出XOY面的投影
void MyGraphicsView::drawXOY(QPointF offset)
{
	//计算投影点云的平均密度
	double density = computeCloudMeanDis(project_cloud);
	//qDebug() << "density" << density;

	//将提取的无序点云有序化
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr orderCloudPointsXOY(new pcl::PointCloud<pcl::PointXYZRGB>);
	orderSoetCloud(project_cloud, project_cloud->size() / 3, orderCloudPointsXOY);
	pcl::io::savePCDFileASCII("ordercloud_xoy.pcd", *orderCloudPointsXOY);

	//将有序化的点云用DP进行简化
	vector<DPPoint> points_DP_XOY;
	for (size_t i = 0; i < orderCloudPointsXOY->size(); i++)
	{
		DPPoint point;
		point.x = orderCloudPointsXOY->points[i].x;
		point.y = orderCloudPointsXOY->points[i].y;
		point.z = orderCloudPointsXOY->points[i].z;
		point.ID = i;

		points_DP_XOY.push_back(point);
	}

	computeDP(points_DP_XOY, 0, points_DP_XOY.size() - 1, density);		//DP简化

	//将DP点转为PCL点云
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_DP_XOY(new pcl::PointCloud<pcl::PointXYZRGB>);
	for (size_t i = 0; i < points_DP_XOY.size(); i++)
	{
		if (points_DP_XOY[i].isRemoved == true)	continue;

		pcl::PointXYZRGB point;
		point.x = points_DP_XOY.at(i).x;
		point.y = points_DP_XOY.at(i).y;
		point.z = points_DP_XOY.at(i).z;

		cloud_DP_XOY->push_back(point);
	}

	if (!cloud_DP_XOY->empty())		pcl::io::savePCDFileASCII("cloud_DP_XOY.pcd", *cloud_DP_XOY);
	qDebug() << "原始点数:" << orderCloudPointsXOY->size() << "\t" << "DP简化后点云数：" << "\t" << cloud_DP_XOY->size();

	//将DP简化后的有序点云转为QT点集
	QVector<QPointF> points_xoy;
	PCLCloud2QTPoints(cloud_DP_XOY, XOY, points_xoy, QPointF(offset.x(), offset.y()));

	//将点云传入到重写的QGraphicsItem中绘制
	MyItem *itemXOY = new MyItem(points_xoy, XOY);
	itemXOY->setSelected(true);
	scene->addItem(itemXOY);
	scene->clearSelection();
	scene->selectedItems().clear();
	itemXOY->setSelected(true);
	MyOrderCloudType xoy_myordercloud;
	xoy_myordercloud.orderCloud = cloud_DP_XOY;
	xoy_myordercloud.project_type = XOY;
	itemXOY->setData(ItemName, "XOY面投影");
	itemXOY->setData(ItemCloud, QVariant::fromValue(xoy_myordercloud));
	emit sendStrFromGraphicView2Console(QString::asprintf("已生成XOY面投影连线！"));

}

//画出俯视XOY面的投影
void MyGraphicsView::drawOverLookXOY(QPointF offset)
{
	//若将俯视投影的点云数过多，进行体素滤波
	float leaf = 0.01;
	while (project_cloud->size() > 10000)
	{
		pcl::VoxelGrid<pcl::PointXYZRGB> vog;
		vog.setInputCloud(project_cloud);
		vog.setLeafSize(leaf, leaf, leaf);
		vog.filter(*project_cloud);
		pcl::io::savePCDFileASCII("vog_cloud.pcd", *project_cloud);
		//qDebug() << "vog_cloud" << project_cloud->size();
		leaf += 0.01;
	}
	
	//计算投影点云的平均密度
	double density = computeCloudMeanDis(project_cloud);
	//qDebug() << "density" << density;

	//KNNAlphaShape提取边界
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr boundarypoints(new pcl::PointCloud<pcl::PointXYZRGB>);
	KNNAlphaShape(project_cloud, density*2.5, density*2.5,boundarypoints);	//以平均密度的2.0-3.0倍作为Alpha半径和KNN搜索半径


	if (boundarypoints->size() == 0)		QMessageBox::warning(this, "俯视投影到XOY面", "提取边界轮廓的点为0！");
	/*for (size_t i = 0; i < boundarypoints.size(); i++)
	{
		qDebug() << boundarypoints[i];
	}
*/


	//将提取的无序点云有序化
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr orderCloudPointsOLXOY(new pcl::PointCloud<pcl::PointXYZRGB>);
	orderSoetCloud(boundarypoints, boundarypoints->size() / 3, orderCloudPointsOLXOY);
	pcl::io::savePCDFileASCII("ordercloud_olxoy.pcd", *orderCloudPointsOLXOY);
	//qDebug() << "orderCloudPoints" << orderCloudPoints->size();

	//将有序点云的点绘制到米格纸上
	//QVector<QPointF> points_olxoy;
	//PCLCloud2QTPoints(orderCloudPoints, OLXOY, points_olxoy);
	//for (size_t i = 0; i < points_olxoy.size(); i++)
	//{
	//	float pointSize = 0.5;
	//	QGraphicsEllipseItem *pointItem = new QGraphicsEllipseItem(-pointSize, -pointSize, 2 * pointSize, 2 * pointSize);
	//	pointItem->setFlags(QGraphicsItem::ItemIsFocusable | QGraphicsItem::ItemIsMovable |
	//		QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusScope);
	//	pointItem->setBrush(QBrush(Qt::black));
	//	//点的位置信息
	//	//pointItem->setPos(orderCloudPoints->at(i).y*86.0, orderCloudPoints->at(i).x*86.0);
	//	pointItem->setPos(points_olxoy[i].x(), points_olxoy[i].y());
	//	scene->addItem(pointItem);
	//	scene->clearSelection();
	//	pointItem->setSelected(true);
	//}


	//将有序化的点云用DP进行简化
	vector<DPPoint> points_DP_OLXOY;
	for (size_t i = 0; i < orderCloudPointsOLXOY->size(); i++)
	{
		DPPoint point;
		point.x = orderCloudPointsOLXOY->points[i].x;
		point.y = orderCloudPointsOLXOY->points[i].y;
		point.z = orderCloudPointsOLXOY->points[i].z;
		point.ID = i;

		points_DP_OLXOY.push_back(point);
	}

	computeDP(points_DP_OLXOY, 0, points_DP_OLXOY.size() - 1, density);		//DP简化

	//将DP点转为PCL点云
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_DP_OLXOY(new pcl::PointCloud<pcl::PointXYZRGB>);
	for (size_t i = 0; i < points_DP_OLXOY.size(); i++)
	{
		if (points_DP_OLXOY[i].isRemoved == true)	continue;

		pcl::PointXYZRGB point;
		point.x = points_DP_OLXOY.at(i).x;
		point.y = points_DP_OLXOY.at(i).y;
		point.z = points_DP_OLXOY.at(i).z;

		cloud_DP_OLXOY->push_back(point);
	}

	if (!cloud_DP_OLXOY->empty())		pcl::io::savePCDFileASCII("cloud_DP_OLXOY.pcd", *cloud_DP_OLXOY);
	qDebug() << "原始点数:" << orderCloudPointsOLXOY->size() << "\t" << "DP简化后点云数：" << "\t" << cloud_DP_OLXOY->size();



	//将DP简化后的有序点云转为QT点集
	QVector<QPointF> points_olxoy;
	PCLCloud2QTPoints(cloud_DP_OLXOY, OLXOY, points_olxoy, QPointF(offset.x(), offset.y()));
	//qDebug() << "points_olxoy" << points_olxoy.size();

	//将点云传入到重写的QGraphicsItem中绘制
	MyItem *itemOLOXY = new MyItem(points_olxoy, OLXOY);
	itemOLOXY->setSelected(true);
	scene->addItem(itemOLOXY);
	scene->clearSelection();
	scene->selectedItems().clear();
	itemOLOXY->setSelected(true);
	MyOrderCloudType olxoy_myordercloud;
	olxoy_myordercloud.orderCloud = cloud_DP_OLXOY;
	olxoy_myordercloud.project_type = OLXOY;
	itemOLOXY->setData(ItemName, "俯视投影");
	itemOLOXY->setData(ItemCloud, QVariant::fromValue(olxoy_myordercloud));
	emit sendStrFromGraphicView2Console(QString::asprintf("已生成俯视投影面投影连线！"));
}

//画出XOY面的剖线
void MyGraphicsView::drawXOYLine(QPointF offset)
{
	//计算投影点云的平均密度
	double density = computeCloudMeanDis(project_cloud);
	//qDebug() << "density" << density;

	//将提取的无序点云有序化
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr orderCloudPointsXOYLine(new pcl::PointCloud<pcl::PointXYZRGB>);
	orderSoetCloud(project_cloud, project_cloud->size() / 3, orderCloudPointsXOYLine);
	pcl::io::savePCDFileASCII("ordercloud_xoyLine.pcd", *orderCloudPointsXOYLine);

	//提取第一个点和最后一个点
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr XOYLineCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
	XOYLineCloud->push_back(orderCloudPointsXOYLine->front());
	XOYLineCloud->push_back(orderCloudPointsXOYLine->back());

	//将有序点云转为QT点集
	QVector<QPointF> points_xoy;
	PCLCloud2QTPoints(XOYLineCloud, XOY, points_xoy, QPointF(offset.x(), offset.y()));
	
	//计算两个点的直线向量
	QVector2D line_dir = QVector2D(points_xoy.back().x() - points_xoy.front().x(), points_xoy.back().y() - points_xoy.front().y());
	line_dir.normalize();


	QPointF P1, P2, P3, P4;
	P2 = QPointF(points_xoy.front().x(), points_xoy.front().y());
	P3 = QPointF(points_xoy.back().x(), points_xoy.back().y());
	P1 = QPointF(P2.x() - line_dir.x() * 20, P2.y() - line_dir.y() * 20);
	P4 = QPointF(P3.x() + line_dir.x() * 20, P3.y() + line_dir.y() * 20);

	points_xoy.clear();
	points_xoy.push_back(P1);
	points_xoy.push_back(P2);
	points_xoy.push_back(P3);
	points_xoy.push_back(P4);


	//将点云传入到重写的QGraphicsItem中绘制
	MyItem *itemXOYLine = new MyItem(points_xoy, XOY);
	itemXOYLine->setSelected(true);
	scene->addItem(itemXOYLine);
	scene->clearSelection();
	scene->selectedItems().clear();
	itemXOYLine->setSelected(true);
	MyOrderCloudType xoyline_myordercloud;
	xoyline_myordercloud.orderCloud = XOYLineCloud;
	xoyline_myordercloud.project_type = XOY;
	itemXOYLine->setData(ItemName, "剖面线");
	itemXOYLine->setData(ItemCloud, QVariant::fromValue(xoyline_myordercloud));
	emit sendStrFromGraphicView2Console(QString::asprintf("已生成剖面连线！"));

}


/*
KNNAlphaShape提取边界
\* 输入点云
\*输入滚球法Alpha半径
\*输入KNN搜索半径
\*输出提取的点云
*/
void MyGraphicsView::KNNAlphaShape(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud, float Alpha, float neborRadius, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &outcloud)
{

	vector<QVector2D> qt_points;		//将PCL转为qt点集
	vector<int> KNNBoundaryPointIndex;		//KNNAlphaShape提取边界点的索引
	QVector<QVector2D> knn_boundaryPoints;		//提取的边界轮廓二维点

	//kd快速临近点搜索
	pcl::KdTreeFLANN<pcl::PointXYZRGB>	kdtree;
	kdtree.setInputCloud(cloud);

	//容器储存了每个点邻近点的索引
	vector<vector<int>> each_point_nebot;

	vector<int> nebor_index;
	vector<float> nebor_dis;

	each_point_nebot.resize(cloud->points.size(), nebor_index);

	//容器each_point_nebot获得每个点邻近点索引的容器
	for (size_t i = 0; i < cloud->points.size(); i++)
	{
		if (kdtree.radiusSearch(cloud->points[i], neborRadius, nebor_index, nebor_dis) > 0)
		{
			each_point_nebot[i].swap(nebor_index);		//该容器第一个索引为自身
		}
	}

	//将点云的点集转为Qvector2D
	for (size_t i = 0; i < cloud->size(); i++)
	{
		QVector2D point;
		point.setX(cloud->points[i].x);
		point.setY(cloud->points[i].y);

		qt_points.push_back(point);
	}

	//判断该点是否处理过函数
	vector<bool> process;
	process.resize(cloud->size(), false);


	for (size_t i = 0; i < qt_points.size(); i++)
	{

		//从该点的邻近点开始
		for (size_t k = 1; k < each_point_nebot[i].size(); k++)
		{

			//判断该点是否计算过
			if (process[each_point_nebot[i][k]] == true)	continue;
			//process[each_point_nebot[i][k]] = true;

			// 跳过距离大于直径的情况
			if (qt_points[i].distanceToPoint(qt_points[each_point_nebot[i][k]]) > 2 * Alpha)		continue;

			// 两个圆心
			QVector2D c1, c2;

			// 线段中点
			QVector2D center = 0.5*(qt_points[i] + qt_points[each_point_nebot[i][k]]);

			// 方向向量 P1P2 = (x,y)
			QVector2D dir = qt_points[i] - qt_points[each_point_nebot[i][k]];

			// 垂直向量 n = (a,b)  a*dir.x+b*dir.y = 0; a = -(b*dir.y/dir.x)
			QVector2D normal;
			normal.setY(5);			// 因为未知数有两个，随便给y附一个值5。

			if (dir.x() != 0)
			{
				normal.setX(-(normal.y()*dir.y()) / dir.x());
			}
			else
			{
				// 如果方向平行于y轴
				normal.setX(1);
				normal.setY(0);
			}

			normal.normalize();   // 法向量单位化

			//获得圆心到P1P2两点连线的距离
			float len = sqrt(Alpha*Alpha - (0.25*dir.length()*dir.length()));

			//两个圆心的坐标
			c1 = center + len * normal;
			c2 = center - len * normal;

			// b1、b2记录是否在圆C1、C2中找到其他点。
			bool b1 = false, b2 = false;
			for (size_t m = 0; m < qt_points.size(); m++)
			{
				if (m == i || m == each_point_nebot[i][k])	continue;

				if (b1 != true && qt_points[m].distanceToPoint(c1) < Alpha)	b1 = true;
				if (b2 != true && qt_points[m].distanceToPoint(c2) < Alpha)	b2 = true;

				// 如果都有内部点，不必再继续检查了
				if (b1 == true && b2 == true)   break;
			}


			if (b1 != true || b2 != true)
			{
				//将边界点的索引存入容器
				KNNBoundaryPointIndex.push_back(i);
				KNNBoundaryPointIndex.push_back(each_point_nebot[i][k]);
			}
		}

		process[i] = true;
	}

	//将重复的点索引删除
	sort(KNNBoundaryPointIndex.begin(), KNNBoundaryPointIndex.end());
	KNNBoundaryPointIndex.erase(unique(KNNBoundaryPointIndex.begin(), KNNBoundaryPointIndex.end()), KNNBoundaryPointIndex.end());

	pcl::PointIndices indices;
	indices.indices.swap(KNNBoundaryPointIndex);
	pcl::copyPointCloud(*cloud, indices, *outcloud);
	//qDebug() << alphaShapeCloud->size();
	
	if (outcloud->size() != 0)
	{
		for (size_t i = 0; i < outcloud->size(); i++)
		{
			QVector2D point;
			point.setX(outcloud->points[i].x);
			point.setY(outcloud->points[i].y);

			knn_boundaryPoints.push_back(point);
		}
	}
	
	if(outcloud->size() !=0)	  pcl::io::savePCDFileASCII("KNNAlphaShape.pcd", *outcloud);
}


//寻找点云中最远的两点的索引
void MyGraphicsView::findMaxDistancePointsofCloud(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & incloud, int headendPointIndex[2])
{
	//点云是否被处理过，若处理过打上标签
	vector<bool> flag;
	flag.resize(incloud->size(), false);

	vector<Point2PointDisIndex> pointsDisIndecx;
	for (size_t i = 0; i < incloud->size(); i++)
	{
		for (size_t k = 0; k < incloud->size(); k++)
		{
			if (flag[k] == true)	continue;

			float dist;
			Point2PointDisIndex tempPoint;
			dist = pcl::euclideanDistance(incloud->points[i], incloud->points[k]);		//计算两点的欧式距离
			if (dist == 0.0)		continue;
			tempPoint.distance = dist;
			tempPoint.index1 = i;
			tempPoint.index2 = k;
			pointsDisIndecx.push_back(tempPoint);
		}
		flag[i] = true;
	}
	//qDebug() << pointsDisIndecx.size();

	//对距离从大到小排序
	sort(pointsDisIndecx.begin(), pointsDisIndecx.end(), [=](Point2PointDisIndex temp1, Point2PointDisIndex temp2) { return (temp1.distance > temp2.distance); });
	/*for (size_t i = 0; i < pointsDisIndecx.size(); i++)
	{
		qDebug() << "distance" << pointsDisIndecx[i].distance<<"index"<< pointsDisIndecx[i].index1<< pointsDisIndecx[i].index2;
	}*/

	if (pointsDisIndecx[0].index1 < pointsDisIndecx[0].index2)
	{
		headendPointIndex[0] = pointsDisIndecx[0].index1;
		headendPointIndex[1] = pointsDisIndecx[0].index2;
	}
	else
	{
		headendPointIndex[0] = pointsDisIndecx[0].index2;
		headendPointIndex[1] = pointsDisIndecx[0].index1;
	}
}


/*
//将无序点云进行有序排列
*\输入无序点云
*\输入最近邻搜索K
*\输出有序点云
*/
void MyGraphicsView::orderSoetCloud(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & incloud, int neborNumbers, pcl::PointCloud<pcl::PointXYZRGB>::Ptr & outcloud)
{
	//每个点是否遍历过容器
	vector<bool> process;
	process.resize(incloud->size(), false);


	//kd快速临近点搜索
	pcl::KdTreeFLANN<pcl::PointXYZRGB>	kdtree;
	kdtree.setInputCloud(incloud);

	//容器储存了每个点邻近点的索引
	vector<vector<int>> each_point_nebot;

	vector<int> nebor_index;
	vector<float> nebor_dis;

	each_point_nebot.resize(incloud->points.size(), nebor_index);

	//容器each_point_nebot获得每个点邻近点索引的容器
	for (size_t i = 0; i < incloud->size(); i++)
	{
		if (kdtree.nearestKSearch(incloud->points[i], neborNumbers, nebor_index, nebor_dis) > 0)
		{
			each_point_nebot[i].swap(nebor_index);
		}
	}


	pcl::PointIndices pointIndex;
	queue<int> seed;		//种子点，以该点开始进行生长

	//寻找到端点的索引
	int headendPointIndex[2];
	findMaxDistancePointsofCloud(incloud, headendPointIndex);
	qDebug() << "headendPointIndex" << headendPointIndex[0] << headendPointIndex[1];

	seed.push(headendPointIndex[0]);
	pointIndex.indices.push_back(headendPointIndex[0]);
	process[headendPointIndex[0]] = true;

	while (!seed.empty())
	{
		int current_seed_index = seed.front();
		for (size_t i = 1; i < each_point_nebot[current_seed_index].size(); i++)
		{
			if (process[each_point_nebot[current_seed_index][i]] == true)	continue;
			seed.push(each_point_nebot[current_seed_index][i]);
			pointIndex.indices.push_back(each_point_nebot[current_seed_index][i]);
			process[each_point_nebot[current_seed_index][i]] = true;
			break;
		}
		seed.pop();
	}
	qDebug() << pointIndex.indices.size();
	for (size_t i = 0; i < pointIndex.indices.size(); i++)
	{
		outcloud->push_back(incloud->points[pointIndex.indices[i]]);
	}

	pcl::io::savePCDFileASCII("ordercloud.pcd", *outcloud);
}

//根据点云和投影类型将PCL按照不同比例尺转为QT点集
void MyGraphicsView::PCLCloud2QTPoints(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud, ProjectType protype, QVector<QPointF> &points, QPointF offset)
{
	//根据传入的点云和投影类型转为QT点
	//比例尺	默认 1:50


	switch (protype)
	{
	case(XOY):
	{
		for (size_t i = 0; i < incloud->size(); i++)
		{
			QPointF point;
			//point.setX(incloud->at(i).y*86.0);
			//point.setY(incloud->at(i).x*86.0);
			point.setX(incloud->at(i).y*offset.x());
			point.setY(incloud->at(i).x*offset.y());
			points.push_back(point);
		}
	}
	break;

	case(XOZ):
	{
		for (size_t i = 0; i < incloud->size(); i++)
		{
			QPointF point;
			/*point.setX(incloud->at(i).z*86.0 + YMaxMin * 86.0);
			point.setY(incloud->at(i).x*86.0);*/
			point.setX(incloud->at(i).z*offset.x() + YMaxMin * offset.x());
			point.setY(incloud->at(i).x*offset.y());
			points.push_back(point);
		}
	}
	break;

	case(YOZ):
	{
		for (size_t i = 0; i < incloud->size(); i++)
		{
			QPointF point;
			//point.setX(incloud->at(i).y*86.0);
			//point.setY(-incloud->at(i).z*86.0 - XMaxMin * 86.0/1.2);
			point.setX(incloud->at(i).y*offset.x());
			point.setY(-incloud->at(i).z*offset.y() - XMaxMin * offset.y() / 1.2);
			points.push_back(point);
		}
	}
	break;

	case(OLXOY):
	{
		for (size_t i = 0; i < incloud->size(); i++)
		{
			QPointF point;
			point.setX(incloud->at(i).y*offset.x());
			point.setY(incloud->at(i).x*offset.y());
			points.push_back(point);
		}
	}
	break;

	default:
		break;
	}

}

//比例尺改变后重新绘制
void MyGraphicsView::ReDraw(QPointF offset)
{
	QList<QGraphicsItem*> all_items = scene->items();
	
	qDebug()<< "items" << all_items.size();
	
	

	for (size_t i = 0; i < all_items.size(); i++)
	{
		if (all_items.at(i)->data(ItemCloud).isValid())
		{
			//获得scene中存在有效的item的有序点云和投影类型
			MyOrderCloudType item_ordercloud = all_items.at(i)->data(ItemCloud).value<MyOrderCloudType>();
			
			//将有序点云按照比例尺转为QT点集
			QVector<QPointF> item_points;
			PCLCloud2QTPoints(item_ordercloud.orderCloud, item_ordercloud.project_type, item_points, QPointF(offset.x(), offset.y()));

			//剖面线绘制
			if (item_points.size() < 4)
			{
				//计算两个点的直线向量
				QVector2D line_dir = QVector2D(item_points.back().x() - item_points.front().x(), item_points.back().y() - item_points.front().y());
				line_dir.normalize();

				QPointF P1, P2, P3, P4;
				P2 = QPointF(item_points.front().x(), item_points.front().y());
				P3 = QPointF(item_points.back().x(), item_points.back().y());
				P1 = QPointF(P2.x() - line_dir.x() * 20, P2.y() - line_dir.y() * 20);
				P4 = QPointF(P3.x() + line_dir.x() * 20, P3.y() + line_dir.y() * 20);

				item_points.clear();
				item_points.push_back(P1);
				item_points.push_back(P2);
				item_points.push_back(P3);
				item_points.push_back(P4);
			}

			//将点云传入到重写的QGraphicsItem中绘制
			MyItem *item = new MyItem(item_points, item_ordercloud.project_type);
			scene->addItem(item);
			scene->clearSelection();
			scene->selectedItems().clear();
			item->setSelected(true);
			MyOrderCloudType item_myordercloud;
			item_myordercloud.orderCloud = item_ordercloud.orderCloud;
			item_myordercloud.project_type = item_ordercloud.project_type;
			item->setData(ItemCloud, QVariant::fromValue(item_myordercloud));

			//将当前item删除
			scene->removeItem(all_items.at(i));
		}
	}
	
}

//弹出出图模板对话框
void MyGraphicsView::setDlgPlotTab()
{
	dlgPlotTab = new QDlgPlotTab(this);
	dlgPlotTab->setAttribute(Qt::WA_DeleteOnClose);
	Qt::WindowFlags flags = dlgPlotTab->windowFlags();
	dlgPlotTab->setWindowFlags(flags | Qt::WindowStaysOnTopHint);
	dlgPlotTab->show();
	//获得制图人，比例尺，日期文字
	//designerText = dlgPlotTab->getDesignerPTE();
	//scaleText = dlgPlotTab->getScalePTE();
	//dataText = dlgPlotTab->getDataPTE();
}

//绘制出图模板表格
void MyGraphicsView::drawFormTable()
{
	FormTabulationItem * item = new FormTabulationItem(designerText, dataText, scaleText, this);
	scene->addItem(item);
	scene->clearSelection();
	scene->selectedItems().clear();

	item->setSelected(true);
	emit sendStrFromGraphicView2Console("已添加制图信息！");
}

//获得制图人，比例尺，日期文字
void MyGraphicsView::getText()
{
	designerText = dlgPlotTab->getDesignerPTE();
	scaleText = dlgPlotTab->getScalePTE();
	dataText = dlgPlotTab->getDataPTE();
}

//设置出图表对话框为空指针
void MyGraphicsView::setDlgPlotTabNull()
{
	this->dlgPlotTab = NULL;
}

//计算点到线的距离
double MyGraphicsView::point2LineDist(DPPoint p1, DPPoint p2, DPPoint p3)
{
	double dist;
	Eigen::Vector4f line_dir = { p1.x - p2.x,p1.y - p2.y,p1.z - p2.z ,0.0 };
	Eigen::Vector4f line_pt = { p1.x,p1.y,p1.z,0.0 };
	Eigen::Vector4f point3 = { p3.x,p3.y,p3.z,0.0 };
	dist = pcl::sqrPointToLineDistance(point3, line_pt, line_dir);				//PCL点到线的距离

	return sqrt(dist);
}

//计算点集中的最大值
double MyGraphicsView::getMaxDist(vector<DPPoint>& Points, int begin, int end)
{
	vector<double> dists;
	double maxdist;
	for (int i = begin; i <= end; i++)
	{
		double dis = point2LineDist(Points[begin], Points[end], Points[i]);
		dists.push_back(dis);
	}
	auto max = max_element(dists.begin(), dists.end());
	//cout << "max_distance" << *max << endl;
	return *max;
}

//获得最大距离的索引
int MyGraphicsView::getMaxDistIndex(vector<DPPoint>& Points, int begin, int end)
{
	vector<double> dists;
	int index;
	for (int i = begin; i <= end; i++)
	{
		dists.push_back(point2LineDist(Points[begin], Points[end], Points[i]));
	}
	auto max = max_element(dists.begin(), dists.end());
	index = Points[begin].ID + distance(dists.begin(), max);
	return index;
}

/*//DP算法简化线条点
\*输入点集，计算后获得DP简化后的点集
\*输入起始点的索引
\*输入终点的索引
\*输入阈值D
*/
void MyGraphicsView::computeDP(vector<DPPoint>& Points, int begin, int end, double threshold)
{
	int mid;
	if (end - begin > 1)
	{
		if (getMaxDist(Points, begin, end) > threshold)
		{
			mid = getMaxDistIndex(Points, begin, end);
			computeDP(Points, begin, mid, threshold);
			computeDP(Points, mid, end, threshold);
		}
		else
		{
			for (int i = begin + 1; i < end; i++)
			{
				Points[i].isRemoved = true;
			}
		}
	}
	else
	{
		return;
	}
}


void MyGraphicsView::slotOn_actPoints_triggered()
{
	QGraphicsEllipseItem   *item = new QGraphicsEllipseItem(-50, -30, 100, 60);
	item->setFlags(QGraphicsItem::ItemIsFocusable | QGraphicsItem::ItemIsMovable |
		QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusScope);
	item->setBrush(QBrush(Qt::blue)); //填充颜色
	scene->addItem(item);
	scene->clearSelection();
	scene->selectedItems().clear();
	item->setSelected(true);
}

//切割的点云投影连线
void MyGraphicsView::slotOn_actProjLine_triggered()
{
	qDebug() << XMaxMin << YMaxMin << ZMaxMin;
	/*if (project_type == XOY)
	{
		qDebug() << "XOY";
	}
	else if (project_type == XOZ)
	{
		qDebug() << "XOZ";
		drawXOZ();
	}
	else if (project_type == YOZ)
	{
		qDebug() << "YOZ";
		drawYOZ();
	}
	else if (project_type == OLXOY)
	{
		qDebug() << "OLXOY";
		isOverLookProjLine = false;
	}
	else if (project_type == NONE)
	{
	}*/

	switch (project_type)
	{
	case(XOY):		//XOY面投影连线
	{
		qDebug() << "XOY";
		int isdrawLine = dlgDrawXOYLine();		//是否绘制剖面线
		if (isdrawLine == 1)
		{
			//qDebug() << "绘制剖面线";
			getScaleOffset(this->deltaOffset);
			drawXOYLine(deltaOffset);
			emit sendDlgClipVisible(true);		//将VTK的平面裁剪对话框显示
			emit sendStrFromGraphicView2Console("已绘制剖面线！");
		}
		else if(isdrawLine == 0)
		{
			//qDebug() << "不绘制剖面线";
			getScaleOffset(this->deltaOffset);
			drawXOY(deltaOffset);
			emit sendDlgClipVisible(true);	
			emit sendStrFromGraphicView2Console("投影到XOY面的点连线绘制完成！");
		}
		else if (isdrawLine == -1)
		{
			qDebug() << "取消";
			emit sendDlgClipVisible(true);		
			return;
		}
	}
	break;

	case(XOZ):		//XOZ面投影连线
	{
		qDebug() << "XOZ";
		getScaleOffset(this->deltaOffset);
		drawXOZ(deltaOffset);
	}
	break;

	case(YOZ):		//YOZ面投影连线
	{
		qDebug() << "YOZ";
		getScaleOffset(this->deltaOffset);
		drawYOZ(deltaOffset);
	}
	break;

	case(OLXOY):	//俯视XOY面投影连线
	{
		qDebug() << "OLXOY";
		clock_t start = clock();
		getScaleOffset(this->deltaOffset);
		drawOverLookXOY(deltaOffset);
		clock_t end = clock();
		emit sendStrFromGraphicView2Console(QString::asprintf("俯视投影到XOY面的点连线绘制完成！用时%d秒", (end - start) / CLOCKS_PER_SEC));
		isOverLookProjLine = false;
	}
	break;
	
	case(NONE):		//点云数太少,不能连线
	{
		qDebug() << "NONE";
		QMessageBox::warning(this, "投影连线", "点云数太少(<10)！");
	}
	default:
		break;
	}
}

//从VTK接受投影后的点云
void MyGraphicsView::getProjCloudFromVTK(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud)
{
	this->project_cloud->clear();
	for (auto iter = incloud->begin();iter != incloud->end();iter++)
	{
		pcl::PointXYZRGB point;
		point.x = (*iter).x;
		point.y = (*iter).y;
		point.z = (*iter).z;
		point.r = (*iter).r;
		point.g = (*iter).g;
		point.b = (*iter).b;

		this->project_cloud->push_back(point);
	}
	//this->project_cloud = incloud;
	
	 project_type = projectType(this->project_cloud);
	 cloudDensity =  computeCloudMeanDis(this->project_cloud);		//计算点云的密度
	//qDebug() << project_type;
	 //qDebug() << "incloud" << incloud->size();
	 //qDebug() << "this->project_cloud" << this->project_cloud->size();
	//qDebug() << "density" << cloudDensity;
}

//删除Item
void MyGraphicsView::slotOn_actDeleteItem_triggered()
{
	int selectItemCounts = scene->selectedItems().count();

	if (selectItemCounts == 0)		return;

	for (size_t i = 0; i < selectItemCounts; i++)
	{
		QGraphicsItem * item = scene->selectedItems().at(0);
		scene->removeItem(item);
		delete item;
	}
}

//清空
void MyGraphicsView::slotOn_actClearScene_triggered()
{
	scene->clear();
}

void MyGraphicsView::getAABBMaxFromVTK(float x, float y, float z)
{
	this->XMaxMin = x;
	this->YMaxMin = y;
	this->ZMaxMin = z;
}

//接受是否进行俯视XOY连线
void MyGraphicsView::getisOverLookProj(bool isproj)
{
	this->isOverLookProjLine = isproj;
}

//组合按钮
void MyGraphicsView::slotOn_actGroup_triggered()
{
	int selectedCounts = this->scene->selectedItems().count();	//Scene中选中的个数
	//qDebug() << "selectedCounts" << selectedCounts;

	if (selectedCounts>1)
	{
		QGraphicsItemGroup *group = new QGraphicsItemGroup;			//创建组合
		scene->addItem(group);	 //组合添加到场景中

		for (size_t i = 0; i < selectedCounts; i++)
		{
			
			MyItem* item = static_cast<MyItem*> (scene->selectedItems().at(0));
			item->setSelected(false); //清除选择虚线框
			item->clearFocus();
			group->addToGroup(item); //添加到组合
		}

		group->setFlags(QGraphicsItem::ItemIsMovable
			| QGraphicsItem::ItemIsSelectable
			| QGraphicsItem::ItemIsFocusable);

		group->setZValue(++frontZ);
		this->scene->clearSelection();
		group->setSelected(true);
	}
}

//打散按钮
void MyGraphicsView::slotOn_actBreak_triggered()
{
	int selectedCounts = this->scene->selectedItems().count();	//Scene中选中的个数
	if (selectedCounts ==1)
	{
		QGraphicsItemGroup  *group;
		group = (QGraphicsItemGroup*)scene->selectedItems().at(0);
		scene->destroyItemGroup(group);
	}
}

//向上移动
void MyGraphicsView::slotOn_actMoveUp_triggered()
{
	int selectedCounts = this->scene->selectedItems().count();	//Scene中选中的个数
	if (selectedCounts >= 1)
	{
		for (size_t i = 0; i < selectedCounts; i++)
		{
			MyItem * item = static_cast<MyItem *>(scene->selectedItems().at(i));
			QPointF itemPos = item->pos();
			qDebug() << "itemPos" << itemPos;
			item->setPos(itemPos.x(), itemPos.y() - 10);
		}
	}
}

//向下移动
void MyGraphicsView::slotOn_actMoveDown_triggered()
{
	int selectedCounts = this->scene->selectedItems().count();	//Scene中选中的个数
	if (selectedCounts >= 1)
	{
		for (size_t i = 0; i < selectedCounts; i++)
		{
			MyItem * item = static_cast<MyItem *>(scene->selectedItems().at(i));
			QPointF itemPos = item->pos();
			qDebug() << "itemPos" << itemPos;
			item->setPos(itemPos.x(), itemPos.y() + 10);
		}
	}
}

//向左移动
void MyGraphicsView::slotOn_actMoveLeft_triggered()
{
	int selectedCounts = this->scene->selectedItems().count();	//Scene中选中的个数
	if (selectedCounts >= 1)
	{
		for (size_t i = 0; i < selectedCounts; i++)
		{
			MyItem * item = static_cast<MyItem *>(scene->selectedItems().at(i));
			QPointF itemPos = item->pos();
			qDebug() << "itemPos" << itemPos;
			item->setPos(itemPos.x()-10, itemPos.y());
		}
	}
}

//向右移动
void MyGraphicsView::slotOn_actMoveRight_triggered()
{
	int selectedCounts = this->scene->selectedItems().count();	//Scene中选中的个数
	if (selectedCounts >= 1)
	{
		for (size_t i = 0; i < selectedCounts; i++)
		{
			MyItem * item = static_cast<MyItem *>(scene->selectedItems().at(i));
			QPointF itemPos = item->pos();
			qDebug() << "itemPos" << itemPos;
			item->setPos(itemPos.x() + 10, itemPos.y());
		}
	}
}

//前置按钮
void MyGraphicsView::slotOn_actEditFront_triggered()
{
	int cnt = scene->selectedItems().count();
	if (cnt>0)
	{
		QGraphicsItem * item = scene->selectedItems().at(0);
		item->setZValue(++frontZ);
		emit sendStrFromGraphicView2Console("已将当前选中图层前置成功！");
	}
}

//后置按钮
void MyGraphicsView::slotOn_actEditBack_triggered()
{
	int cnt = scene->selectedItems().count();
	if (cnt > 0)
	{
		QGraphicsItem * item = scene->selectedItems().at(0);
		item->setZValue(--backZ);
		emit sendStrFromGraphicView2Console("已将当前选中图层后置成功！");
	}
}

//保存按钮
void MyGraphicsView::slotOn_actSave_triggered()
{
	sendClosedXOYLabel(false);	//关闭坐标轴图标
	sendClosedScale(false);		//关闭比例尺
	this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);	//关闭滚条
	this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	
	QPixmap pix = this->grab();

	QString filePath = QFileDialog::getSaveFileName(this, "保存文件", QCoreApplication::applicationDirPath(), "(*bmp)");
	if (filePath.isEmpty())
	{
		sendClosedXOYLabel(true);	//显示坐标轴图标
		sendClosedScale(true);		//显示比例尺
		this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);	//显示滚条
		this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
		return;
	}

	QString dir = filePath.right(filePath.length() - filePath.lastIndexOf("/") - 1);	//文件名
	pix.save(filePath,nullptr,99);

	emit sendStrFromGraphicView2Console("保存图片到路径" + filePath + "成功！");


	sendClosedXOYLabel(true);	//显示坐标轴图标
	sendClosedScale(true);		//显示比例尺
	this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);	//显示滚条
	this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
}

//指北针按钮
void MyGraphicsView::slotOn_actCompass_triggered()
{
	//QImage * img_compass = new QImage();
	//img_compass->load(":/images/images/compassMap.bmp");

	CompassItem * item = new CompassItem();
	scene->addItem(item);
	scene->clearSelection();
	scene->selectedItems().clear();

	item->setSelected(true);
	emit sendStrFromGraphicView2Console("已添加指北针！");
}

//添加文字按钮
void MyGraphicsView::slotOn_actText_triggered()
{
	QString str = QInputDialog::getText(this, "添加文字", "请输入文字");
	if (str.isEmpty())		return;

	QGraphicsTextItem *item = new QGraphicsTextItem(str);

	QFont font = this->font();
	font.setPointSize(15);
	font.setBold(true);
	item->setFont(font);
	item->setFlags(QGraphicsItem::ItemIsMovable
		| QGraphicsItem::ItemIsSelectable
		| QGraphicsItem::ItemIsFocusable);

	item->setPos(140, 200);
	item->setData(ItemName, "文字");
	item->setCursor(Qt::SizeAllCursor);
	scene->addItem(item);
	scene->clearSelection();
	scene->selectedItems().clear();
	item->setSelected(true);
}

//接受ComBox发送过来的比例尺
void MyGraphicsView::getScaleComBoxCurrentIndexChanged(int index)
{
	//qDebug() << "Scale" << index;
	switch (index)
	{
	case(0):
	{
		//qDebug() << "1:10" ;
		scaleType = OneToTen;
		sendReDraw(scaleType);		//比例尺改变时重新作图
		//sendScaleToDlgPlotTap(scaleType);	//将比例尺发送到出图模板对话框
	}
	break;

	case(1):
	{
		//qDebug() << "1:20";
		scaleType = OneToTwenty;
		sendReDraw(scaleType);		//比例尺改变时重新作图
		//sendScaleToDlgPlotTap(scaleType);	//将比例尺发送到出图模板对话框
	}
	break;

	case(2):
	{
		//qDebug() << "1:50";
		scaleType = OneToFifty;
		sendReDraw(scaleType);		//比例尺改变时重新作图
		//sendScaleToDlgPlotTap(scaleType);	//将比例尺发送到出图模板对话框
	}
	break;

	case(3):
	{
		//qDebug() << "1:100";
		scaleType = OneToHundred;
		sendReDraw(scaleType);		//比例尺改变时重新作图
		//sendScaleToDlgPlotTap(scaleType);	//将比例尺发送到出图模板对话框
	}
	break;

	default:
		break;
	}
}



//接受改变比例尺时重新画图
void MyGraphicsView::getReDraw(ScaleType scale)
{
	//2K
	//1cm = 43pix

	switch (scale)
	{
	case(OneToTen):
	{
		qDebug() << "重新作图1:10";
		deltaOffset = QPointF(430.0, 430.0);
		emit sendScaleOffset(deltaOffset);
		ReDraw(deltaOffset);
	}
	break;

	case(OneToTwenty):
	{
		qDebug() << "重新作图1:20";
		deltaOffset = QPointF(215.0, 215.0);
		emit sendScaleOffset(deltaOffset);
		ReDraw(deltaOffset);
	}
	break;

	case(OneToFifty):
	{
		qDebug() << "重新作图1:50";
		deltaOffset = QPointF(86.0, 86.0);
		emit sendScaleOffset(deltaOffset);
		ReDraw(deltaOffset);
	}
	break;

	case(OneToHundred):
	{
		qDebug() << "重新作图1:100";
		deltaOffset = QPointF(43.0, 43.0);
		emit sendScaleOffset(deltaOffset);
		ReDraw(deltaOffset);
	}
	break;

	default:
		break;
	}

}

//得到比例尺变化后坐标的偏移量
void MyGraphicsView::getScaleOffset(QPointF offset)
{
	this->deltaOffset = offset;
}

//出图模板按钮
void MyGraphicsView::slotOn_actPlotTab_triggered()
{
	//sendDlgClipVisible(false);		//关闭裁剪对话框
	setDlgPlotTab();
	sendGetCurrentScale();
	//qDebug() << "scale" << currentScaleIndex;
	dlgPlotTab->getCurrentScaleIndex(currentScaleIndex);	//将当前比例尺发送给出图模板对话框
}





//显示米格纸按钮（1cm = 43pix，2K）
void MyGraphicsView::slotOn_actMiGe_triggered(bool checked)
{
	//方格纸起始坐标
	int startX = 0;
	int startY = 0;

	int WangGeCount = 100;

	QPixmap pix(43,43);
	QPainter painter(&pix);
	painter.setRenderHint(QPainter::Antialiasing, true);
	
	QPen pen(Qt::red);
	pix.fill(Qt::white);

	for (size_t i = 0; i <11; i++)
	{
		if (i == 0 || i==10)
		{
			pen.setWidthF(2.0f);
		}
		else if(i == 5)
		{
			pen.setWidthF(1.0f);
		}
		else
		{
			pen.setWidthF(0.5f);
		}
			painter.setPen(pen);
			if (checked)
			{
				painter.drawLine(QPointF(startX, startY + 4.3 * i), QPointF(startX + 4.3 * 10, startY + 4.3 * i));
				painter.drawLine(QPointF(startX + 4.3 * i, startY), QPointF(startX + 4.3 * i, startY + 4.3 * 10));
			}
			else if (!checked)
			{
				
			}
	}

	this->setBackgroundBrush(pix);
}

void MyGraphicsView::keyPressEvent(QKeyEvent *e)
{
	emit this->keyPress(e);
	QGraphicsView::keyPressEvent(e);
}

void MyGraphicsView::mousePressEvent(QMouseEvent *e)
{
	if (e->button() == Qt::LeftButton)
	{
		QPoint point = e->pos();
		emit this->mouseClicked(point);
	}
	QGraphicsView::mousePressEvent(e);
}

void MyGraphicsView::mouseMoveEvent(QMouseEvent *e)
{
	QPoint point = e->pos();
	gvScenePos = this->mapToScene(point);
	emit this->mouseMovePoint(point);

	return QGraphicsView::mouseMoveEvent(e);
}

void MyGraphicsView::mouseDoubleClickEvent(QMouseEvent * e)
{
	QPoint point = e->pos();
	QPointF pointScene = this->mapToScene(point);		//转换到Scene坐标

	QGraphicsItem  *item = NULL;
	item = scene->itemAt(pointScene, this->transform());	//获取光标下的绘图项

	if (item == NULL)	return;
	
	switch (item->type())
	{
	case(QGraphicsTextItem::Type):
	{
		QGraphicsTextItem * textItem = qgraphicsitem_cast<QGraphicsTextItem*>(item);

		QFont font = textItem->font();
		bool ok = false;
		font = QFontDialog::getFont(&ok, font, this, "设置字体");
		if (ok)	textItem->setFont(font);
	}
	break;

	default:
		break;
	}
}




///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

///**************************				重写QGraphicsItem类							***********************//

///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

MyItem::MyItem()
{

}

MyItem::MyItem(QVector<QPointF> &points, ProjectType project_type, QGraphicsItem* parentItem)
	:QGraphicsItem(parentItem)
{
	
	this->projectType = project_type;
	this->itemPoints = points;
	this->setFlags(QGraphicsItem::ItemIsFocusScope  | QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
	
	//计算AABB和边界
	getMinMaxQtPoints(this->itemPoints,minPT,maxPt);
	AABB = QRectF(minPT, maxPt);
	customRect = AABB.adjusted(-15, -15, 15, 15);
	
	drawRectBoundary = false;
	item_operator = t_none;


}

MyItem::~MyItem()
{
}



void MyItem::paint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
	drawCustomRect(painter);		//绘制出边界框
	mouseCursorShape();				//鼠标的样式
	drawLine(painter);				//绘制连线
}

QRectF MyItem::boundingRect() const
{
	QRectF rect = customRect;
	return rect;
}

QRectF MyItem::getCustomRect(void) const
{
	return customRect;
}

void MyItem::customPaint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
}

void MyItem::mousePressEvent(QGraphicsSceneMouseEvent * event)
{
	//获得鼠标坐标
	itemPos = event->pos();
	scenePos = event->scenePos();

	QRectF currentBoundary = this->getCustomRect();	//获得外边界框
	

	float distance;
	if ((getDistance(itemPos,currentBoundary.topLeft())<30)|| (getDistance(itemPos, currentBoundary.topRight()) < 30)
		|| (getDistance(itemPos, currentBoundary.bottomLeft()) < 30)|| (getDistance(itemPos, currentBoundary.bottomRight()) < 30))
	{
		qDebug() << "旋转";
		item_operator = t_rotate;
	}
	else if (currentBoundary.contains(itemPos))
	{
		qDebug() << "平移";
		item_operator = t_move;
	}

	press_pos = itemPos;		//记录鼠标按压的坐标

	return QGraphicsItem::mousePressEvent(event);
}

void MyItem::mouseMoveEvent(QGraphicsSceneMouseEvent * event)
{

	//获得鼠标坐标
	itemPos = event->pos();
	scenePos = event->scenePos();

	if (item_operator == t_move)
	{
		this->setFlag(QGraphicsItem::ItemIsMovable, true);
	}
	else if (item_operator == t_rotate)
	{
		this->setFlag(QGraphicsItem::ItemIsMovable, false);
		mouseMoveRotate(itemPos);
	}
	
	return QGraphicsItem::mouseMoveEvent(event);
}

void MyItem::mouseReleaseEvent(QGraphicsSceneMouseEvent * event)
{
	item_operator = t_none;
	return QGraphicsItem::mouseReleaseEvent(event);
}

//计算获得Qt点集的XY的最大最小值，计算AABB
void MyItem::getMinMaxQtPoints(const QVector<QPointF>& points, QPointF & minPt, QPointF & maxPt)
{
	//X,Y的集合容器
	vector<float>	xVec;
	vector<float>	yVec;

	for (size_t i = 0; i < points.size(); i++)
	{
		xVec.push_back(points[i].x());
		yVec.push_back(points[i].y());
	}

	//对容器从大到小排序
	sort(xVec.begin(), xVec.end(), [=](float p1, float p2) {return p1 > p2; });
	sort(yVec.begin(), yVec.end(), [=](float p1, float p2) {return p1 > p2; });

	minPt.setX(xVec.back());
	minPt.setY(yVec.back());
	maxPt.setX(xVec.front());
	maxPt.setY(yVec.front());

	//qDebug() << "maxPt" << maxPt;
	//qDebug() << "minPT" << minPt;
}

//计算两点之间的距离
float MyItem::getDistance(const QPointF & point1, const QPointF & point2)
{
	float a = qPow((point1.x() - point2.x()), 2);
	float b = qPow((point1.y() - point2.y()), 2);
	float distance = qSqrt(a + b);
	return distance;
}

//绘制出边界框
void MyItem::drawCustomRect(QPainter * painter)
{
	//设置抗锯齿等相关属性
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->setRenderHint(QPainter::TextAntialiasing, true);

	//自定义画笔
	QPen rectPen;
	rectPen.setWidthF(2.5);
	rectPen.setStyle(Qt::DashDotLine);
	rectPen.setCapStyle(Qt::RoundCap);
	rectPen.setJoinStyle(Qt::RoundJoin);

	//根据不同投影类型设置边界颜色
	switch (this->projectType)
	{
	case(XOZ):
	{
		rectPen.setColor(QColor(0, 255, 255));
	}
	break;

	case(XOY):
	{
		rectPen.setColor(QColor(255, 218, 185));
	}
	break;

	case(YOZ):
	{

		rectPen.setColor(QColor(34, 139, 34));
	}
		break;

	case(OLXOY):
	{
		rectPen.setColor(QColor(255, 228, 196));
	}
	break;

	default:
		break;
	}

	if (this->isSelected())
	{
		painter->setPen(rectPen);
	}
	else
	{
		painter->setPen(Qt::NoPen);
	}

	painter->drawRect(customRect);
	this->update();
}

//将点连线
void MyItem::drawLine(QPainter *painter)
{
	//设置抗锯齿等相关属性
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->setRenderHint(QPainter::TextAntialiasing, true);

	//自定义画笔
	QPen linePen;
	linePen.setWidthF(2.0);
	linePen.setStyle(Qt::SolidLine);
	linePen.setCapStyle(Qt::RoundCap);
	linePen.setJoinStyle(Qt::RoundJoin);

	//设置路径
	QPainterPath linePath;
	QPainterPath bSplinePath;

	//根据不同投影类型设置连线路径
	switch (this->projectType)
	{
	case(XOY):
	{
		
		//剖面线绘制
		if (this->itemPoints.size()<5)
		{
			linePath.moveTo(this->itemPoints[0]);
			linePath.lineTo(this->itemPoints[1]);
			linePath.moveTo(this->itemPoints[2]);
			linePath.lineTo(this->itemPoints[3]);
			
		}

		//XOY面投影点绘制
		else
		{
			//-------将点直接用直线连接-----------
		/*linePath.moveTo(this->itemPoints.front());
		for (size_t i = 0; i < this->itemPoints.size(); i++)
		{
			linePath.lineTo(this->itemPoints[i]);
		}
		*/

		//-------------BSpline----------
			QVector<QPointF> fittingPoints_XOY;
			fittingPoints_XOY = BSplineFitPoints(this->itemPoints, false, 0.1);
			//qDebug() << "B样条曲线拟合的点数" << fittingPoints_XOY.size();

			bSplinePath.moveTo(fittingPoints_XOY.front());
			for (size_t i = 0; i < fittingPoints_XOY.size(); i++)
			{
				bSplinePath.lineTo(fittingPoints_XOY[i]);
			}
		}
		
	}
	break;

	case(XOZ):
	{
		//-------将点直接用直线连接-----------
		/*linePath.moveTo(this->itemPoints.front());
		for (size_t i = 0; i < this->itemPoints.size(); i++)
		{
			linePath.lineTo(this->itemPoints[i]);
		}*/

		//-------------BSpline----------
		QVector<QPointF> fittingPoints_XOZ;
		fittingPoints_XOZ = BSplineFitPoints(this->itemPoints, false, 0.1);
		//qDebug() << "B样条曲线拟合的点数" << fittingPoints_XOZ.size();

		bSplinePath.moveTo(fittingPoints_XOZ.front());
		for (size_t i = 0; i < fittingPoints_XOZ.size(); i++)
		{
			bSplinePath.lineTo(fittingPoints_XOZ[i]);
		}
	}
	break;

	case(YOZ):
	{
		//-------将点直接用直线连接-----------
		/*linePath.moveTo(this->itemPoints.front());
		for (size_t i = 0; i < this->itemPoints.size(); i++)
		{
			linePath.lineTo(this->itemPoints[i]);
		}*/

		//-------------BSpline----------
		QVector<QPointF> fittingPoints_YOZ;
		fittingPoints_YOZ = BSplineFitPoints(this->itemPoints, false, 0.1);
		//qDebug() << "B样条曲线拟合的点数" << fittingPoints_YOZ.size();

		bSplinePath.moveTo(fittingPoints_YOZ.front());
		for (size_t i = 0; i < fittingPoints_YOZ.size(); i++)
		{
			bSplinePath.lineTo(fittingPoints_YOZ[i]);
		}

	}
	break;

	case(OLXOY):
	{
		//-------将点直接用直线连接-----------
		/*linePath.moveTo(this->itemPoints.front());
		for (size_t i = 0; i < this->itemPoints.size(); i++)
		{
			linePath.lineTo(this->itemPoints[i]);
		}
		linePath.lineTo(this->itemPoints.front());*/


		//-------------BSpline----------
		QVector<QPointF> fittingPoints_OLXOY;
		fittingPoints_OLXOY = BSplineFitPoints(this->itemPoints, true, 0.1);
		//qDebug() << "B样条曲线拟合的点数" << fittingPoints_OLXOY.size();

		bSplinePath.moveTo(fittingPoints_OLXOY.front());
		for (size_t i = 0; i < fittingPoints_OLXOY.size(); i++)
		{
			bSplinePath.lineTo(fittingPoints_OLXOY[i]);
		}

	}
	break;

	default:
		break;
	}

	painter->setPen(linePen);
	painter->drawPath(linePath);
	painter->drawPath(bSplinePath);

}

//鼠标的样式
void MyItem::mouseCursorShape()
{
	//qDebug() << "this->isSelected()" << this->isSelected();
	
	if (this->isSelected())
	{
		if (item_operator == t_rotate)
		{
			this->setCursor(Qt::ClosedHandCursor);
		}
		else if (item_operator == t_move)
		{
			this->setCursor(Qt::SizeAllCursor);
		}
		else
		{
			this->setCursor(Qt::OpenHandCursor);
		}
	}
	else
	{
		this->setCursor(Qt::CrossCursor);
	}

	this->update();
}

//处理旋转
void MyItem::mouseMoveRotate(const QPointF & movePos)
{
	QRectF currentBoundary = this->getCustomRect();	//获得外边界框
	QPointF centerPos = this->boundingRect().center();		//获得外边框的中心坐标
	//QGraphicsItem::setTransformOriginPoint(centerPos);		//设置旋转中心

	//获得夹角向量
	QVector2D startVec = QVector2D(press_pos.x() - centerPos.x(), press_pos.y() - centerPos.y());
	QVector2D endVec = QVector2D(movePos.x() - centerPos.x(), movePos.y() - centerPos.y());

	//单位化
	startVec.normalize();
	endVec.normalize();

	// 单位向量点乘，计算角度
	qreal dotValue = QVector2D::dotProduct(startVec, endVec);
	if (dotValue > 1.0f)	dotValue = 1.0f;
	else if(dotValue <-1.0f)	dotValue = -1.0f;

	qreal dotCosAngle = qAcos(dotValue);	//cos反函数求角度，该角度是弧度值

	qreal angle = dotCosAngle * (180.0f / PI);		//弧度转成角度

	 //向量叉乘获取方向
	QVector3D crossValue = QVector3D::crossProduct(QVector3D(startVec, 1.0), QVector3D(endVec, 1.0));

	//根据叉乘结果Z值得正负判断旋转是顺时针还是逆时针
	if (crossValue.z() < 0)		angle = -angle;
	
	itemRotateAngle += angle;

	// 设置旋转矩阵
	itemTransform.translate(centerPos.x(), centerPos.y());
	itemTransform.rotate(itemRotateAngle);
	itemTransform.translate(-centerPos.x(), -centerPos.y());
	this->setTransform(itemTransform);
	press_pos = movePos;
	this->update();
}

//通过离散点反求控制点
QVector<QPointF> MyItem::BSplineControlPoints(const QVector<QPointF>& points)
{
	QVector<QPointF> controls;
	int N = points.size();
	Eigen::MatrixXd weight(N, N);
	weight.setZero();
	for (size_t i = 0; i < weight.rows(); i++)
	{
		weight(i, i) = 1;
		weight(i, (i + 1) % weight.cols()) = 4;
		weight(i, (i + 2) % weight.cols()) = 1;
	}


	Eigen::MatrixXd V(N, 2);
	V.setZero();

	for (size_t i = 0; i < V.rows(); i++)
	{
		V(i, 0) = points[i].x();
		V(i, 1) = points[i].y();
	}

	V *= 6;

	Eigen::MatrixXd P(N, 2);

	P = weight.inverse()*V;

	//cout << V.rows() << endl;;
	///cout << V << endl;
	//cout << "\n\n\n" << P;

	for (size_t i = 0; i < P.rows(); i++)
	{
		QPointF point;
		point.setX(P(i, 0));
		point.setY(P(i, 1));

		controls.push_back(point);
	}


	return controls;
}


/*//求出B样条曲线拟合函数插值点
*\输入离散点
*\是否闭合
*\步长t[0,1]
*\输出BSpline拟合生成的点集
*/
QVector<QPointF> MyItem::BSplineFitPoints(const QVector<QPointF>& controlpoints, bool closed, double stride)
{
	QVector<QPointF> fittingPoints;
	int pointSize;		//拟合的型值点数
	pointSize = controlpoints.size();

	for (int i = 0; i < pointSize; i++)
	{
		QPointF xy[4];
		xy[0] = (controlpoints[i] + 4 * controlpoints[(i + 1) % controlpoints.size()] + controlpoints[(i + 2) % controlpoints.size()]) / 6;
		xy[1] = -(controlpoints[i] - controlpoints[(i + 2) % controlpoints.size()]) / 2;
		xy[2] = (controlpoints[i] - 2 * controlpoints[(i + 1) % controlpoints.size()] + controlpoints[(i + 2) % controlpoints.size()]) / 2;
		xy[3] = -(controlpoints[i] - 3 * controlpoints[(i + 1) % controlpoints.size()] + 3 * controlpoints[(i + 2) % controlpoints.size()] - controlpoints[(i + 3) % controlpoints.size()]) / 6;
		for (double t = 0; t <= 1; t += stride)
		{
			QPointF totalPoints = QPointF(0, 0);
			for (int j = 0; j < 4; j++)
			{
				totalPoints += xy[j] * pow(t, j);
			}
			fittingPoints.push_back(totalPoints);
		}
	}

	if (closed)			//	闭合
	{
		return fittingPoints;
	}
	else              //不闭合
	{
		//新的控制点点集和拟合点点集
		QVector<QPointF> new_points = controlpoints;
		QVector<QPointF> new_fittingPoints;
		QPointF Ps, Pe;		//在两端添加两个点Ps，Pn
		QPointF P1, P2, P3, P4;		//前两个点和最后两个点
		P1 = controlpoints.first();
		P2 = controlpoints.at(1);
		P3 = controlpoints.at(pointSize - 2);
		P4 = controlpoints.back();

		//计算直线向量
		QVector2D P12 = QVector2D(P1.x() - P2.x(), P1.y() - P2.y());
		QVector2D P34 = QVector2D(P4.x() - P3.x(), P4.y() - P3.y());

		//长度
		float P12_len = P12.length();
		float P34_len = P34.length();

		//单位化
		P12.normalize();
		P34.normalize();

		//求出添加的两点坐标
		Ps = QPointF(P1.x() + P12.x()*P12_len, P1.y() + P12.y()*P12_len);
		Pe = QPointF(P4.x() + P34.x()*P34_len, P4.y() + P34.y()*P34_len);

		new_points.push_front(Ps);
		new_points.push_back(Pe);

		pointSize = new_points.size();
		for (size_t i = 0; i < pointSize - 3; i++)
		{
			QPointF W[4];
			W[0] = (new_points[i] + 4 * new_points[(i + 1) % new_points.size()] + new_points[(i + 2) % new_points.size()]) / 6;
			W[1] = -(new_points[i] - new_points[(i + 2) % new_points.size()]) / 2;
			W[2] = (new_points[i] - 2 * new_points[(i + 1) % new_points.size()] + new_points[(i + 2) % new_points.size()]) / 2;
			W[3] = -(new_points[i] - 3 * new_points[(i + 1) % new_points.size()] + 3 * new_points[(i + 2) % new_points.size()] - new_points[(i + 3) % new_points.size()]) / 6;
			for (double t = 0; t <= 1; t += stride)
			{
				QPointF totalPoints = QPointF(0, 0);
				for (int j = 0; j < 4; j++)
				{
					totalPoints += W[j] * pow(t, j);
				}
				new_fittingPoints.push_back(totalPoints);
			}
		}
		return new_fittingPoints;
	}

}







///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

///**************************				重写QGraphicsItem,指北针						***********************//

///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//


CompassItem::CompassItem()
{
	this->setFlags(QGraphicsItem::ItemIsFocusScope | QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
	offset = QPointF(280, -340);		//离原点偏移的x,y量
	AABB = QRectF(QPointF(0, 0) + offset, QPointF(44, 104) + offset);
	customRect = AABB.adjusted(-10, -10, 10, 10);
	item_operator = t_none;

};


CompassItem::~CompassItem()
{
}

void CompassItem::paint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
	drawCustomRect(painter);	//绘制出边界框
	drawPicture(painter);		//绘制出compass
	mouseCursorShape();				//鼠标的样式
}

QRectF CompassItem::getCustomRect(void) const
{
	return	customRect;
}

QRectF CompassItem::boundingRect() const
{
	QRectF rect = customRect;
	return rect;
}

void CompassItem::customPaint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
}

void CompassItem::mousePressEvent(QGraphicsSceneMouseEvent * event)
{
	//获得鼠标坐标
	itemPos = event->pos();
	scenePos = event->scenePos();

	QRectF currentBoundary = this->getCustomRect();	//获得外边界框


	float distance;
	if ((getDistance(itemPos, currentBoundary.topLeft()) < 20) || (getDistance(itemPos, currentBoundary.topRight()) < 20)
		|| (getDistance(itemPos, currentBoundary.bottomLeft()) < 20) || (getDistance(itemPos, currentBoundary.bottomRight()) < 20))
	{
		qDebug() << "旋转";
		item_operator = t_rotate;
	}
	else if (currentBoundary.contains(itemPos))
	{
		qDebug() << "平移";
		item_operator = t_move;
	}

	press_pos = itemPos;		//记录鼠标按压的坐标
	return QGraphicsItem::mousePressEvent(event);
}

void CompassItem::mouseMoveEvent(QGraphicsSceneMouseEvent * event)
{
	//获得鼠标坐标
	itemPos = event->pos();
	scenePos = event->scenePos();

	if (item_operator == t_move)
	{
		this->setFlag(QGraphicsItem::ItemIsMovable, true);
	}
	else if (item_operator == t_rotate)
	{
		this->setFlag(QGraphicsItem::ItemIsMovable, false);
		mouseMoveRotate(itemPos);
	}
	return QGraphicsItem::mouseMoveEvent(event);
}

void CompassItem::mouseReleaseEvent(QGraphicsSceneMouseEvent * event)
{
	item_operator = t_none;
	return QGraphicsItem::mouseReleaseEvent(event);
}

//绘制出边界框
void CompassItem::drawCustomRect(QPainter * painter)
{
	//设置抗锯齿等相关属性
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->setRenderHint(QPainter::TextAntialiasing, true);
	//自定义画笔
	QPen rectPen;
	rectPen.setWidthF(2.5);
	rectPen.setStyle(Qt::DashDotDotLine);
	rectPen.setCapStyle(Qt::SquareCap);
	rectPen.setJoinStyle(Qt::MiterJoin);
	rectPen.setColor(QColor(0,199,140));

	if (this->isSelected())
	{
		painter->setPen(rectPen);
	}
	else
	{
		painter->setPen(Qt::NoPen);
	}

	painter->drawRect(customRect);
	painter->setPen(Qt::NoPen);
	this->update();
}

//绘制出compass
void CompassItem::drawPicture(QPainter * painter)
{
	//设置抗锯齿等相关属性
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->setRenderHint(QPainter::TextAntialiasing, true);

	//绘制实心三角形
	painter->setBrush(Qt::black);
	QPolygonF triangle;
	triangle.push_back(QPointF(22.0, 24.0) + offset);
	triangle.push_back(QPointF(30.0, 42.0) + offset);
	triangle.push_back(QPointF(14.0, 42.0) + offset);
	painter->drawConvexPolygon(triangle);

	//绘制线条
	QPen line_pen;
	line_pen.setColor(Qt::black);
	line_pen.setWidthF(2.0);
	painter->setPen(line_pen);
	painter->drawLine(QPointF(14.0, 62.0) + offset, QPointF(30.0, 62.0) + offset);
	painter->drawLine(QPointF(22.0, 42.0) + offset, QPointF(22.0, 98.0) + offset);

	//绘制字体N
	QFont N_font;
	N_font.setFamily("SimHei");
	N_font.setPointSizeF(12.0);
	painter->setFont(N_font);
	painter->drawText(QPointF(17, 22) + offset, tr("N"));
	
}

//计算两点之间的距离
float CompassItem::getDistance(const QPointF & point1, const QPointF & point2)
{
	float a = qPow((point1.x() - point2.x()), 2);
	float b = qPow((point1.y() - point2.y()), 2);
	float distance = qSqrt(a + b);
	return distance;
}

//处理旋转
void CompassItem::mouseMoveRotate(const QPointF & movePos)
{
	QRectF currentBoundary = this->getCustomRect();	//获得外边界框
	QPointF centerPos = this->boundingRect().center();		//获得外边框的中心坐标
	//QGraphicsItem::setTransformOriginPoint(centerPos);		//设置旋转中心

	//获得夹角向量
	QVector2D startVec = QVector2D(press_pos.x() - centerPos.x(), press_pos.y() - centerPos.y());
	QVector2D endVec = QVector2D(movePos.x() - centerPos.x(), movePos.y() - centerPos.y());

	//单位化
	startVec.normalize();
	endVec.normalize();

	// 单位向量点乘，计算角度
	qreal dotValue = QVector2D::dotProduct(startVec, endVec);
	if (dotValue > 1.0f)	dotValue = 1.0f;
	else if (dotValue < -1.0f)	dotValue = -1.0f;

	qreal dotCosAngle = qAcos(dotValue);	//cos反函数求角度，该角度是弧度值

	qreal angle = dotCosAngle * (180.0f / PI);		//弧度转成角度

	 //向量叉乘获取方向
	QVector3D crossValue = QVector3D::crossProduct(QVector3D(startVec, 1.0), QVector3D(endVec, 1.0));

	//根据叉乘结果Z值得正负判断旋转是顺时针还是逆时针
	if (crossValue.z() < 0)		angle = -angle;

	itemRotateAngle += angle;

	// 设置旋转矩阵
	itemTransform.translate(centerPos.x(), centerPos.y());
	itemTransform.rotate(itemRotateAngle);
	itemTransform.translate(-centerPos.x(), -centerPos.y());
	this->setTransform(itemTransform);
	press_pos = movePos;
	this->update();
}

//鼠标的样式
void CompassItem::mouseCursorShape()
{
	if (this->isSelected())
	{
		if (item_operator == t_rotate)
		{
			this->setCursor(Qt::ClosedHandCursor);
		}
		else if (item_operator == t_move)
		{
			this->setCursor(Qt::SizeAllCursor);
		}
		else
		{
			this->setCursor(Qt::OpenHandCursor);
		}
	}
	else
	{
		this->setCursor(Qt::CrossCursor);
	}

	this->update();
}




///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

///**************************				重写QGraphicsItem,出图模板						***********************//

///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//




FormTabulationItem::FormTabulationItem(QString designer, QString data, QString scale, QWidget *parent)
{
	this->setFlags(QGraphicsItem::ItemIsFocusScope | QGraphicsItem::ItemIsSelectable 
		| QGraphicsItem::ItemIsFocusable | QGraphicsItem::ItemIsMovable);

	offset = QPointF(140, 200);		//离原点偏移的x,y量
	AABB = QRectF(QPointF(0, 0) + offset, QPointF(300, 200) + offset);
	customRect = AABB.adjusted(-10, -10, 10, 10);

	currentDataText = data;
	currentDesignerText = designer;
	currentScaleText = scale;

	this->setCursor(Qt::SizeAllCursor);
	//qDebug() <<"ITEM"<< currentDataText << currentDesignerText << currentScaleText;
}

FormTabulationItem::~FormTabulationItem()
{


}

//绘制出图模板样式
void FormTabulationItem::drawPlotTab(QPainter * painter)
{
	//绘制出图模板表格
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->setRenderHint(QPainter::TextAntialiasing, true);

	//绘制线条
	QPen line_pen;
	line_pen.setColor(Qt::black);
	line_pen.setWidthF(2.0);
	painter->setPen(line_pen);
	//边框
	painter->drawLine(QPointF(2.0, 2.0) + offset, QPointF(2.0, 148.0) + offset);
	painter->drawLine(QPointF(2.0, 2.0) + offset, QPointF(298.0, 2.0) + offset);
	painter->drawLine(QPointF(2.0, 148.0) + offset, QPointF(298.0, 148.0) + offset);
	painter->drawLine(QPointF(298.0, 2.0) + offset, QPointF(298.0, 148.0) + offset);
	//内横线
	painter->drawLine(QPointF(2.0, 49.0) + offset, QPointF(298.0, 49.0) + offset);
	painter->drawLine(QPointF(2.0, 99.0) + offset, QPointF(298.0, 99.0) + offset);
	//内竖线
	painter->drawLine(QPointF(79.0, 2.0) + offset, QPointF(79.0, 148.0) + offset);

	//绘制制图人字体
	QFont designerfont;
	designerfont.setFamily("Adobe Devanagari");
	designerfont.setPointSizeF(15.0);
	painter->setFont(designerfont);
	painter->drawText(QPointF(2.0, 32.0) + offset, tr("制图人"));

	//绘制比例尺字体
	QFont scalefont;
	scalefont.setFamily("Adobe Devanagari");
	scalefont.setPointSizeF(15.0);
	painter->setFont(scalefont);
	painter->drawText(QPointF(2.0, 82.0) + offset, tr("比例尺"));

	//绘制日期字体
	QFont datafont;
	datafont.setFamily("Adobe Devanagari");
	datafont.setPointSizeF(15.0);
	painter->setFont(datafont);
	painter->drawText(QPointF(12.0, 132.0) + offset, tr("日期"));

	this->update();
}

//添加输入的文字
void FormTabulationItem::drawAddText(QPainter * painter, const QString & text, const QPointF &offset)
{
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->setRenderHint(QPainter::TextAntialiasing, true);


	QFont designerfont;
	designerfont.setFamily("Adobe Devanagari");
	designerfont.setPointSizeF(15.0);
	painter->setFont(designerfont);
	painter->drawText(offset, text);
}

void FormTabulationItem::paint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
	drawPlotTab(painter);
	
	//添加制图人信息
	if (currentDesignerText.length() >= 3)	drawAddText(painter, currentDesignerText, QPointF(160, 32)+offset);
	else drawAddText(painter, currentDesignerText, QPointF(160, 32) + offset);

	//添加比例尺信息
	drawAddText(painter, currentScaleText, QPointF(165, 82) + offset);

	//添加日期信息
	if (currentDataText.length() > 3)	drawAddText(painter, currentDataText, QPointF(110, 132) + offset);
	else drawAddText(painter, currentDataText, QPointF(160, 132) + offset);
	//drawAddText(painter, currentDataText, QPointF(85, 82) + offset);
}

QRectF FormTabulationItem::getCustomRect(void) const
{
	return QRectF();
}

QRectF FormTabulationItem::boundingRect() const
{
	QRectF rect = customRect;
	return rect;
}

void FormTabulationItem::customPaint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
}
