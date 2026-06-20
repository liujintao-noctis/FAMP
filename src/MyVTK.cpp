#include "MyVTK.h"

MyVTK::MyVTK(QWidget *parent)
	: FAMP_QVTK_WIDGET(parent)
{
	
	Init(); //初始化
	colors = vtkNamedColors::New();	//颜色
	mixBackGround();	//混色背景
	setWidgetAxes();	//坐标轴


	orignalCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);	//传入的点云初始化
	currentItemCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
	//points = vtkPoints::New();		//vtk点集初始化
	
	AABB_Polydata = NULL;
	randomPlane = NULL;
	verticalPlane = NULL;
	horizonalPlane = NULL;
	clipPlane = NULL;
	dlgClip = NULL;
	vtkDlgClip = NULL;
	cloud_cut_plane = NULL;
	 
	isActiveDlgClip = false;
	isClipSucessed = false;
	isProjectionSuccess = false;

	//切割的点显示的VTK数据的初始化
	selectedPolyData = vtkPolyData::New();
	selectedPoints = vtkPoints::New();
	cellSelected = vtkCellArray::New();
	selectedmapper = vtkPolyDataMapper::New();
	selectedActor = vtkActor::New();
	selectedGlfilter = vtkVertexGlyphFilter::New();


	camera = vtkCamera::New();	//初始化相机
	initCamera();		//相机初始化


	//切割平面初始化
	randomPlane = vtkPlaneWidget::New();
	horizonalPlane = vtkPlaneWidget::New();
	verticalPlane = vtkPlaneWidget::New();


	//切割的点云投影到面
	cloud_cut_projection.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
	//投影到平面的点
	planeProject = vtkPlane::New();
	projectPointPolyData = vtkPolyData::New();
	projectPoints = vtkPoints::New();
	projectPointCell = vtkCellArray::New();
	projectPointmapper = vtkPolyDataMapper::New();
	projectPointGlfilter = vtkVertexGlyphFilter::New();;
	projectPointActor = vtkActor::New();
	projectPointCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
	projectPointActor->VisibilityOff();
	renderer->AddActor(projectPointActor);


	//---------创建以AABB最小的坐标创建坐标轴
	lineScourceX = vtkLineSource::New();
	lineScourceY = vtkLineSource::New();
	lineScourceZ = vtkLineSource::New();
	mapperX = vtkPolyDataMapper::New();
	mapperY = vtkPolyDataMapper::New();
	mapperZ = vtkPolyDataMapper::New();
	actorX = vtkActor::New();
	actorY = vtkActor::New();
	actorZ = vtkActor::New();

	
	




	//鼠标交互
	vtkInteractorStyleTrackballCamera *style = vtkInteractorStyleTrackballCamera::New();
	//vtkInteractorStyleTrackballActor * style = vtkInteractorStyleTrackballActor::New();
	renderWindowInteractor->SetInteractorStyle(style);


	connect(this, SIGNAL(sendAABBPolydata(vtkPolyData *)), this, SLOT(getAABBPolydata(vtkPolyData *)));	//传送AABBPolyData
}

MyVTK::~MyVTK()
{
}

//初始化渲染器交互器
void MyVTK::Init()
{

	renderer = vtkRenderer::New();
	renderWindow = vtkGenericOpenGLRenderWindow::New();
	
	renderWindow->AddRenderer(renderer);
#if VTK_MAJOR_VERSION >= 9
	this->setRenderWindow(renderWindow);
	renderWindowInteractor = this->interactor();
#else
	renderWindowInteractor = vtkRenderWindowInteractor::New();
	renderWindowInteractor->SetRenderWindow(renderWindow);
	this->SetRenderWindow(renderWindow);
	renderWindowInteractor->Initialize();
#endif
}

void MyVTK::mixBackGround()
{
	renderer->GradientBackgroundOn();
	renderer->SetBackground(colors->GetColor3d("SlateGray").GetData());
	renderer->SetBackground2(colors->GetColor3d("Wheat").GetData());
}

void MyVTK::setWidgetAxes()
{
	vtkAxesActor* axesActor = vtkAxesActor::New();
	vtkOrientationMarkerWidget* widgetAxes = vtkOrientationMarkerWidget::New();
	widgetAxes->SetOrientationMarker(axesActor);
	widgetAxes->SetInteractor(renderWindowInteractor);
	widgetAxes->SetEnabled(1);
	widgetAxes->SetInteractive(0);
}

float MyVTK::getAABBCoordinateMax(pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud)
{
	//获取包围盒
	pcl::PointXYZRGB AABB_Max, AABB_min;
	if (inCloud->size() == 0)	return 1.0;
	pcl::getMinMax3D(*inCloud, AABB_min, AABB_Max);		//计算各轴的最大值最小值

	std::vector<float> vec;
	vec.push_back(AABB_Max.x);
	vec.push_back(AABB_Max.y);
	vec.push_back(AABB_Max.z);
	vec.push_back(AABB_min.x);
	vec.push_back(AABB_min.y);
	vec.push_back(AABB_min.z);

	std::sort(vec.begin(), vec.end(), [](float p1, float p2) {return p1 < p2; });	//升序排列

	float coordinateM = (vec.at(5) - vec.at(0))*0.75;
	return static_cast<float>(std::fabs(coordinateM));
}

void MyVTK::initCamera()
{
	
	float pos =getAABBCoordinateMax(orignalCloud);
	camera->SetPosition(0, 0, pos);
	camera->SetFocalPoint(0, 0, 0);
	camera->SetViewUp(0, 1, 0);
	camera->ParallelProjectionOn();
	renderer->SetActiveCamera(camera);
	renderer->ResetCamera();
	this->update();
}

//显示相机垂直面
vtkPlaneWidget * MyVTK::DisplayVerticalPlane()
{

	double *viewUp;
	double *posistion;
	double *facalPosition;
	double *lookat;
	double distance;

	viewUp = camera->GetViewUp();
	posistion = camera->GetPosition();
	facalPosition = camera->GetFocalPoint();
	lookat = camera->GetDirectionOfProjection();
	distance = camera->GetDistance();

	/*qDebug() << "相机上方" << viewUp[0] << viewUp[1] << viewUp[2];
	qDebug() << "Lookat" << lookat[0] << lookat[1] << lookat[2];
	qDebug() << "焦距" << distance;*/

	double  viewright[3];
	vtkMath::Cross(viewUp, lookat, viewright);
	//qDebug() << "相机右向量" << viewright[0] << viewright[1] << viewright[2];

	
	verticalPlane->SetInteractor(renderWindowInteractor);	//与交互器关联
	//camera_plane->SetOrigin(0, 0, 0);
	verticalPlane->SetNormal(viewright);
	verticalPlane->SetResolution(100);//即：设置网格数
	verticalPlane->GetPlaneProperty()->SetColor(0.5, .8, 0.5);//设置颜色
	verticalPlane->GetPlaneProperty()->SetOpacity(0.5);//设置透明度
	verticalPlane->GetHandleProperty()->SetColor(0, .4, .7);//设置平面顶点颜色;
	verticalPlane->GetHandleProperty()->SetLineWidth(3.0);//设置平面线宽
	verticalPlane->GetHandleProperty()->SetPointSize(8.0);	//设置平面顶点大小;
	//camera_plane->SetRepresentationToWireframe();//平面显示为网格属性
	verticalPlane->SetRepresentationToSurface();
	//camera_plane->SetCenter();//设置平面坐标
	verticalPlane->PlaceWidget();//放置平面
	//verticalPlane->On();//显示平面
	return  verticalPlane;
}

//显示相机水平面
vtkPlaneWidget * MyVTK::DisplayHorizonalPlane()
{
	double * viewUp;
	viewUp = camera->GetViewUp();

	horizonalPlane->SetInteractor(renderWindowInteractor);	//与交互器关联
	//camera_plane->SetOrigin(0, 0, 0);
	horizonalPlane->SetNormal(viewUp);
	horizonalPlane->SetResolution(100);//即：设置网格数
	horizonalPlane->GetPlaneProperty()->SetColor(0.4, 0.3, 0.8);//设置颜色
	horizonalPlane->GetPlaneProperty()->SetOpacity(0.7);//设置透明度
	horizonalPlane->GetHandleProperty()->SetColor(0, .4, .7);//设置平面顶点颜色;
	horizonalPlane->GetHandleProperty()->SetLineWidth(3.0);//设置平面线宽
	verticalPlane->GetHandleProperty()->SetPointSize(8.0);	//设置平面顶点大小;
	horizonalPlane->SetRepresentationToSurface();
	horizonalPlane->PlaceWidget();//放置平面
	return  horizonalPlane;
}

//显示随机面
vtkPlaneWidget * MyVTK::DisplayRandomPlane()
{
	
	//double * screenBt = camera->GetScreenBottomLeft();
	qDebug() << "camera->GetDistance" << camera->GetDistance();
	
	randomPlane->SetInteractor(renderWindowInteractor);	//与交互器关联
	//camera_plane->SetOrigin(0, 0, 0);
	randomPlane->SetResolution(100);//即：设置网格数
	randomPlane->GetPlaneProperty()->SetColor(0.5, 0.2, 0.6);//设置颜色
	randomPlane->GetPlaneProperty()->SetOpacity(0.6);//设置透明度
	randomPlane->GetHandleProperty()->SetColor(0.6 ,0.2, 0.3);//设置平面顶点颜色;
	randomPlane->GetHandleProperty()->SetPointSize(8.0);	//设置平面顶点大小;
	randomPlane->GetHandleProperty()->SetLineWidth(3.0);//设置平面线宽
	//randomPlane->GetHandleProperty()->SetEdgeVisibility(1);
	//randomPlane->GetHandleProperty()->SetEdgeColor(0.2, 0.5, 0.6);
	randomPlane->SetRepresentationToSurface();
	randomPlane->SetNormalToYAxis(1.0);
	//randomPlane->PlaceWidget();//放置平面
	return  randomPlane;
}

//开始进行切割
void MyVTK::beginClipPlane()
{
	//点法式，从切割面获得原点和法线方程
	double *origin = clipPlane->GetOrigin();
	double *normal = clipPlane->GetNormal();
	//qDebug() << "origin" << origin[0] << origin[1] << origin[2];
	//qDebug() << "normal" << normal[0] << normal[1] << normal[2];

	cloud_cut_plane.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
	cloud_cut_plane->clear();
	selectedPoints->Reset();

	//获得当前DB Tree下的点云
	emit sendGetDBItem();
	if (currentItemCloud->empty())		return;
	//qDebug() << "currentItemCloud" << currentItemCloud->size();

	for (size_t i = 0; i < currentItemCloud->size(); i++)
	{
		double Point[3];
		Point[0] = currentItemCloud->points[i].x;
		Point[1] = currentItemCloud->points[i].y;
		Point[2] = currentItemCloud->points[i].z;


		// 计算点到切割面的距离
		double distance = vtkPlane::DistanceToPlane(Point, normal, origin);


		//将切割的点保存到pcl中
		pcl::PointXYZRGB pointSelected;

		//设置距离阈值
		double threshold;
		this->vtkDlgClip->getSpinBoxValue(threshold);
		if(distance < threshold)
		{
			//qDebug() << distance;
			pointSelected.x = Point[0];
			pointSelected.y = Point[1];
			pointSelected.z = Point[2];
			cloud_cut_plane->push_back(pointSelected);
		}
	}

	//qDebug() << "共有" << cloud_cut_plane->size() << "在平面上";
	emit sendStrFromVTK2Console(QString::asprintf("共有%d个点在切割面上", cloud_cut_plane->size()));


	////---------------将切割生成的点云发送到GraphicView
	//if (cloud_cut_plane->size() != 0)
	//{
	//	isClipSucessed = true;
	//	triggeredSignalProj();
	//	emit sendClipCloud(cloud_cut_plane);
	//}
	//else if (cloud_cut_plane->size() == 0)
	//{
	//	isClipSucessed = false;
	//	triggeredSignalProj();
	//}


	//---------------将切割生成的点云进行投影-----------------
	if (cloud_cut_plane->size() != 0)
	{
		this->cloud_cut_projection = cloud_cut_plane;
		isClipSucessed = true;
		triggeredSignalXOYProj();
		triggeredSignalXOZProj();
		triggeredSignalYOZProj();
		triggeredSignalOverLookProj();
		
	}

	else if (cloud_cut_plane->size() == 0)
	{
		isClipSucessed = false;
		triggeredSignalXOYProj();
		triggeredSignalXOZProj();
		triggeredSignalYOZProj();
		triggeredSignalOverLookProj();
	}

	computeCurrentCloudAABB(currentItemCloud);
	emit sendAABBBoxXYZMAX(XMaxMin, YMaxMin, ZMaxMin);


	/*for (size_t i = 0; i < cloud_cut_plane->size(); i++)
	{
		qDebug() << cloud_cut_plane->points[i].x << cloud_cut_plane->points[i].y << cloud_cut_plane->points[i].z;
	}*/

	//将切割的点在VTK中显示
	if (!cloud_cut_plane->points.empty())
	{

		//pcl::io::savePCDFileASCII("cloud_cut_plane.pcd", *cloud_cut_plane);
		for (size_t i = 0; i < cloud_cut_plane->size(); i++)
		{
			selectedidtype = selectedPoints->InsertNextPoint(cloud_cut_plane->points[i].x, cloud_cut_plane->points[i].y, cloud_cut_plane->points[i].z);
			cellSelected->InsertNextCell(1, &selectedidtype);
		}
		selectedPolyData->SetPoints(selectedPoints);
		selectedPolyData->SetVerts(cellSelected);
		selectedGlfilter->SetInputData(selectedPolyData);
		selectedGlfilter->Update();
		selectedmapper->SetInputData(selectedGlfilter->GetOutput());
		selectedmapper->ScalarVisibilityOn();

		selectedActor->SetMapper(selectedmapper);
		selectedActor->GetProperty()->SetColor(colors->GetColor3d("Blue").GetData());
		selectedActor->GetProperty()->SetPointSize(5);

		renderer->AddActor(selectedActor);

	}

	if (cloud_cut_plane->points.empty())
	{
		renderer->RemoveActor(selectedActor);
	}

	if (cloud_cut_plane->points.empty())
	{
		renderer->RemoveActor(selectedActor);
	}

	this->update();
}

//弹出切割平面对话框
void  MyVTK::setDlgClip()
{
	dlgClip = new QDlgClip(this);
	this->vtkDlgClip = dlgClip;
	isActiveDlgClip = true;
	this->vtkDlgClip->setAttribute(Qt::WA_DeleteOnClose);
	Qt::WindowFlags flags = dlgClip->windowFlags();
	this->vtkDlgClip->setWindowFlags(flags | Qt::WindowStaysOnTopHint);
	this->vtkDlgClip->setClipButtonEnable(true);
	this->vtkDlgClip->show();
	
}
//设置弹出的对话框是否隐藏
void MyVTK::setDlgClipVisible(bool enable)
{
	this->vtkDlgClip->setVisible(enable);
}



void MyVTK::setQDlgClipNULL()
{
	this->dlgClip = NULL;
}

void MyVTK::getDBItemCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud)
{
	this->currentItemCloud = Cloud;
}

//切割结束后关闭掉切割平面和移除掉显示的选中的演员
void MyVTK::endCutRemoveActors()
{
	renderer->RemoveActor(selectedActor);
	verticalPlane->Off();
	randomPlane->Off();
	horizonalPlane->Off();
	projectPointActor->VisibilityOff();
}

//保存点云文件
void MyVTK::saveDlgCloudFile()
{
	if (cloud_cut_plane == NULL || cloud_cut_plane->empty())
	{
		QString dlgTitle = "警告";
		QString strInfo = "未进行切割或切割点云数为0！";
		QMessageBox::warning(dlgClip, dlgTitle, strInfo);
		return;
	}

	QString filePath = QFileDialog::getSaveFileName(dlgClip, "保存文件", QCoreApplication::applicationDirPath(), "(*pcd);;所有文件(*.*)");
	if (filePath.isEmpty())		return;

	QString dir = filePath.right(filePath.length() - filePath.lastIndexOf("/") - 1);	//文件名
	std::string pathPCD = qstr2str(filePath);
	pcl::io::savePCDFileASCII(pathPCD, *cloud_cut_plane);
	emit sendStrFromVTK2Console("保存切割点云到路径" + filePath + "成功！");
}

//触发sendActProjLineEnable(bool enable)信号
void MyVTK::triggeredSignalProj()
{
	emit sendActProjLineEnable(isProjectionSuccess);
}

//触发发送投影到XOY按钮的信号
void MyVTK::triggeredSignalXOYProj()
{
	emit sendActProjXOYEnable(isClipSucessed);
}

//触发发送投影到XOZ按钮的信号
void MyVTK::triggeredSignalXOZProj()
{
	emit sendActProjXOZEnable(isClipSucessed);
}

//触发发送投影到YOZ按钮的信号
void MyVTK::triggeredSignalYOZProj()
{
	emit sendActProjYOZEnable(isClipSucessed);
}

//触发发送俯视投影按钮的信号
void MyVTK::triggeredSignalOverLookProj()
{
	sendActOverLookProjEnable(isClipSucessed);
}

void MyVTK::getAABBPolydata(vtkPolyData * polyData)
{
	this->AABB_Polydata = vtkPolyData::New();
	this->AABB_Polydata = polyData;
}

void MyVTK::getClipPlane(vtkPlaneWidget * plane)
{
	this->clipPlane = plane;
}

//从主窗口获得切割按钮禁用与否
void MyVTK::getPbnClipEnable(bool enable)
{
	this->vtkDlgClip->setClipButtonEnable(enable);
	//qDebug() <<"vtkDlgClip->setClipButtonEnable"<< this->vtkDlgClip->setClipButtonEnable();
}

//接受GraphicsView发送过来的演员
void MyVTK::getActorFromGraphicView(vtkActor * actor)
{
	renderer->AddActor(actor);
	this->update();
}

//投影到YOZ面按钮
void MyVTK::slotActProjYOZ_triggered()
{
	//设置投影面，点法式
	vtkPlaneSource * project_plane = vtkPlaneSource::New();
	project_plane->SetCenter(P1.x, P1.y, P1.z);
	project_plane->SetNormal(YOZNormal.x(), YOZNormal.y(), YOZNormal.z());
	project_plane->Update();

	//将切割的点投影到YOZ平面
	for (size_t i = 0; i < cloud_cut_projection->size(); i++)
	{
		double projectPoint[3];	//投影后的点
		double point[3];	//投影前的点
		pcl::PointXYZRGB point_project;

		point[0] = cloud_cut_projection->points[i].x;
		point[1] = cloud_cut_projection->points[i].y;
		point[2] = cloud_cut_projection->points[i].z;

		//将点投影到指定面上
		planeProject->ProjectPoint(point, project_plane->GetCenter(), project_plane->GetNormal(), projectPoint);

		point_project.x = projectPoint[0];
		point_project.y = projectPoint[1];
		point_project.z = projectPoint[2];

		projectPointCloud->push_back(point_project);
	}
	pcl::io::savePCDFileASCII("projectPointCloudYOZ.pcd", *projectPointCloud);

	//投影完成，触发米格纸投影连线按钮
	isProjectionSuccess = true;
	emit sendActProjLineEnable(isProjectionSuccess);
	emit sendProjCloud2GraphicsView(projectPointCloud);		//将投影后的点云发送到GraphicsView
	emit sendStrFromVTK2Console(QString::asprintf("YOZ面投影完成！共有%d个点在YOZ面上", projectPointCloud->size()));

	projectPoints->Reset();
	vtkIdType projectidtype;

	for (size_t i = 0; i < projectPointCloud->size(); i++)
	{
		projectidtype = projectPoints->InsertNextPoint(projectPointCloud->points[i].x, projectPointCloud->points[i].y, projectPointCloud->points[i].z);
		projectPointCell->InsertNextCell(1, &projectidtype);
	}

	projectPointPolyData->SetPoints(projectPoints);
	projectPointPolyData->SetVerts(projectPointCell);
	projectPointGlfilter->SetInputData(projectPointPolyData);
	projectPointGlfilter->Update();
	projectPointmapper->SetInputData(projectPointGlfilter->GetOutput());
	projectPointmapper->ScalarVisibilityOn();
	projectPointmapper->Update();

	projectPointActor->SetMapper(projectPointmapper);
	projectPointActor->GetProperty()->SetColor(colors->GetColor3d("SeaGreen").GetData());
	projectPointActor->GetProperty()->SetPointSize(3);
	projectPointActor->VisibilityOn();

	projectPointCloud->clear();
	
	this->update();
}

//投影到XOZ面按钮
void MyVTK::slotActProjXOZ_triggered()
{
	//设置投影面，点法式
	vtkPlaneSource * project_plane = vtkPlaneSource::New();
	project_plane->SetCenter(P1.x, P1.y, P1.z);
	project_plane->SetNormal(XOZNormal.x(), XOZNormal.y(), XOZNormal.z());
	project_plane->Update();

	//将切割的点投影到XOZ平面
	for (size_t i = 0; i < cloud_cut_projection->size(); i++)
	{
		double projectPoint[3];	//投影后的点
		double point[3];	//投影前的点
		pcl::PointXYZRGB point_project;

		point[0] = cloud_cut_projection->points[i].x;
		point[1] = cloud_cut_projection->points[i].y;
		point[2] = cloud_cut_projection->points[i].z;

		//将点投影到指定面上
		planeProject->ProjectPoint(point, project_plane->GetCenter(), project_plane->GetNormal(), projectPoint);

		point_project.x = projectPoint[0];
		point_project.y = projectPoint[1];
		point_project.z = projectPoint[2];

		projectPointCloud->push_back(point_project);
	}

	pcl::io::savePCDFileASCII("projectPointCloudXOZ.pcd", *projectPointCloud);
	//投影完成，触发米格纸投影连线按钮
	isProjectionSuccess = true;
	emit sendActProjLineEnable(isProjectionSuccess);
	emit sendProjCloud2GraphicsView(projectPointCloud);		//将投影后的点云发送到GraphicsView
	emit sendStrFromVTK2Console(QString::asprintf("XOZ面投影完成！共有%d个点在XOZ面上", projectPointCloud->size()));

	projectPoints->Reset();
	vtkIdType projectidtype;

	for (size_t i = 0; i < projectPointCloud->size(); i++)
	{
		projectidtype = projectPoints->InsertNextPoint(projectPointCloud->points[i].x, projectPointCloud->points[i].y, projectPointCloud->points[i].z);
		projectPointCell->InsertNextCell(1, &projectidtype);
	}

	projectPointPolyData->SetPoints(projectPoints);
	projectPointPolyData->SetVerts(projectPointCell);
	projectPointGlfilter->SetInputData(projectPointPolyData);
	projectPointGlfilter->Update();
	projectPointmapper->SetInputData(projectPointGlfilter->GetOutput());
	projectPointmapper->ScalarVisibilityOn();
	projectPointmapper->Update();

	projectPointActor->SetMapper(projectPointmapper);
	projectPointActor->GetProperty()->SetColor(colors->GetColor3d("DeepSkyBlue").GetData());
	projectPointActor->GetProperty()->SetPointSize(3);
	projectPointActor->VisibilityOn();

	projectPointCloud->clear();
	//renderer->AddActor(projectPointActor);

	this->update();
}

//投影到XOY面按钮
void MyVTK::slotActProjXOY_triggered()
{
	//设置投影面，点法式
	vtkPlaneSource * project_plane = vtkPlaneSource::New();
	project_plane->SetCenter(P1.x, P1.y, P1.z);
	project_plane->SetNormal(XOYNormal.x(), XOYNormal.y(), XOYNormal.z());
	project_plane->Update();

	//将切割的点投影到XOY平面
	for (size_t i = 0; i < cloud_cut_projection->size(); i++)
	{
		double projectPoint[3];	//投影后的点
		double point[3];	//投影前的点
		pcl::PointXYZRGB point_project;

		point[0] = cloud_cut_projection->points[i].x;
		point[1] = cloud_cut_projection->points[i].y;
		point[2] = cloud_cut_projection->points[i].z;

		//将点投影到指定面上
		planeProject->ProjectPoint(point, project_plane->GetCenter(), project_plane->GetNormal(), projectPoint);

		point_project.x = projectPoint[0];
		point_project.y = projectPoint[1];
		point_project.z = projectPoint[2];

		projectPointCloud->push_back(point_project);
	}
	pcl::io::savePCDFileASCII("projectPointCloudXOY.pcd", *projectPointCloud);
	//投影完成，触发米格纸投影连线按钮
	isProjectionSuccess = true;
	emit sendActProjLineEnable(isProjectionSuccess);
	emit sendProjCloud2GraphicsView(projectPointCloud);		//将投影后的点云发送到GraphicsView
	emit sendStrFromVTK2Console(QString::asprintf("XOY面投影完成！共有%d个点在XOY面上", projectPointCloud->size()));

	projectPoints->Reset();
	vtkIdType projectidtype;

	for (size_t i = 0; i < projectPointCloud->size(); i++)
	{
		projectidtype = projectPoints->InsertNextPoint(projectPointCloud->points[i].x, projectPointCloud->points[i].y, projectPointCloud->points[i].z);
		projectPointCell->InsertNextCell(1, &projectidtype);
	}

	projectPointPolyData->SetPoints(projectPoints);
	projectPointPolyData->SetVerts(projectPointCell);
	projectPointGlfilter->SetInputData(projectPointPolyData);
	projectPointGlfilter->Update();
	projectPointmapper->SetInputData(projectPointGlfilter->GetOutput());
	projectPointmapper->ScalarVisibilityOn();
	projectPointmapper->Update();

	projectPointActor->SetMapper(projectPointmapper);
	projectPointActor->GetProperty()->SetColor(colors->GetColor3d("LightPink").GetData());
	projectPointActor->GetProperty()->SetPointSize(3);
	projectPointActor->VisibilityOn();

	projectPointCloud->clear();
	//renderer->AddActor(projectPointActor);

	this->update();
}

//俯视投影按钮
void MyVTK::slotActOverLookProj_triggered()
{
	//设置投影面，点法式
	vtkPlaneSource * project_plane = vtkPlaneSource::New();
	project_plane->SetCenter(P1.x, P1.y, P1.z);
	project_plane->SetNormal(XOYNormal.x(), XOYNormal.y(), XOYNormal.z());
	project_plane->Update();

	//将点云全部投影到XOY面上
	//获得当前DB Tree下的点云
	emit sendGetDBItem();
	if (currentItemCloud->empty())		return;
	for (size_t i = 0; i < currentItemCloud->size(); i++)
	{
		double projectPoint[3];	//投影后的点
		double point[3];	//投影前的点
		pcl::PointXYZRGB point_project;

		point[0] = currentItemCloud->points[i].x;
		point[1] = currentItemCloud->points[i].y;
		point[2] = currentItemCloud->points[i].z;

		//将点投影到指定面上
		planeProject->ProjectPoint(point, project_plane->GetCenter(), project_plane->GetNormal(), projectPoint);

		point_project.x = projectPoint[0];
		point_project.y = projectPoint[1];
		point_project.z = projectPoint[2];

		projectPointCloud->push_back(point_project);
	}
	pcl::io::savePCDFileASCII("projectPointCloudOverLook.pcd", *projectPointCloud);
	//投影完成，触发米格纸投影连线按钮
	isProjectionSuccess = true;
	emit sendActProjLineEnable(isProjectionSuccess);
	emit sendIsOverLookProj(true);
	emit sendProjCloud2GraphicsView(projectPointCloud);		//将投影后的点云发送到GraphicsView
	emit sendStrFromVTK2Console(QString::asprintf("俯视投影完成！共有%d个点在XOY面上", projectPointCloud->size()));

	projectPoints->Reset();
	vtkIdType projectidtype;

	for (size_t i = 0; i < projectPointCloud->size(); i++)
	{
		projectidtype = projectPoints->InsertNextPoint(projectPointCloud->points[i].x, projectPointCloud->points[i].y, projectPointCloud->points[i].z);
		projectPointCell->InsertNextCell(1, &projectidtype);
	}

	projectPointPolyData->SetPoints(projectPoints);
	projectPointPolyData->SetVerts(projectPointCell);
	projectPointGlfilter->SetInputData(projectPointPolyData);
	projectPointGlfilter->Update();
	projectPointmapper->SetInputData(projectPointGlfilter->GetOutput());
	projectPointmapper->ScalarVisibilityOn();
	projectPointmapper->Update();

	projectPointActor->SetMapper(projectPointmapper);
	projectPointActor->GetProperty()->SetColor(colors->GetColor3d("LightPink").GetData());
	projectPointActor->GetProperty()->SetPointSize(2);
	projectPointActor->VisibilityOn();

	projectPointCloud->clear();
	//renderer->AddActor(projectPointActor);

	this->update();
	
}

// 计算AABB包围盒和相关参数
void MyVTK::computeCurrentCloudAABB(pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud)
{
	pcl::getMinMax3D(*inCloud, min_point_AABB, max_point_AABB);

	//P1为坐标原点，14为Z轴，12为X轴，13为Y轴

	P1.x = min_point_AABB.x;
	P1.y = min_point_AABB.y;
	P1.z = min_point_AABB.z;

	P2.x = max_point_AABB.x;
	P2.y = min_point_AABB.y;
	P2.z = min_point_AABB.z;

	P3.x = min_point_AABB.x;
	P3.y = max_point_AABB.y;
	P3.z = min_point_AABB.z;

	P4.x = min_point_AABB.x;
	P4.y = min_point_AABB.y;
	P4.z = max_point_AABB.z;

	//XOY面法线
	XOYNormal = QVector3D(P4.x - P1.x, P4.y - P1.y, P4.z - P1.z);

	//XOZ面法线
	XOZNormal = QVector3D(P3.x - P1.x, P3.y - P1.y, P3.z - P3.z);

	//YOZ面法线
	YOZNormal = QVector3D(P2.x - P1.x, P2.y - P1.y, P2.z - P1.z);


	//X，Y,Z的最值差
	XMaxMin = P2.x - P1.x;
	YMaxMin = P3.y - P1.y;
	ZMaxMin = P4.z - P1.z;

	//qDebug() << XOYNormal;
	//qDebug() << XOZNormal;
	//qDebug() << YOZNormal;
	//qDebug() << XMaxMin << YMaxMin << ZMaxMin;

}

//以AABB最小的坐标创建坐标轴
void MyVTK::AABBOrignalPosAxis(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud)
{
	pcl::PointXYZRGB min_point;
	pcl::PointXYZRGB max_point;
	pcl::getMinMax3D(*incloud, min_point, max_point);

	//qDebug() << "min_point" << min_point.x << min_point.y << min_point.z;

	//P1为坐标原点，14为Z轴，12为X轴，13为Y轴
	QVector3D P1,P2,P3,P4;

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

	//向量
	//XYZ轴向量
	QVector3D X_Normal, Z_Normal, Y_Normal;
	X_Normal = QVector3D(P2.x() - P1.x(), P2.y() - P1.y(), P2.z() - P1.z());
	Y_Normal = QVector3D(P3.x() - P1.x(), P3.y() - P1.y(), P3.z() - P3.z());
	Z_Normal = QVector3D(P4.x() - P1.x(), P4.y() - P1.y(), P4.z() - P1.z());

	X_Normal.normalize();
	Y_Normal.normalize();
	Z_Normal.normalize();


	
	//各个方向的最大值
	float XMax = max_point.x - min_point.x;
	float YMax = max_point.y - min_point.y;
	float ZMax = max_point.z - min_point.z;
	vector<float> max;
	max.push_back(XMax);
	max.push_back(YMax);
	max.push_back(ZMax);
	sort(max.begin(), max.end(), [=](float temp1, float temp2) {return temp1 > temp2; });

	float max_element = max.front();

	//设置坐标轴的长度
	float line_length = max_element/6.0;



	//X轴
	lineScourceX->SetPoint1(min_point.x, min_point.y, min_point.z);
	lineScourceX->SetPoint2(min_point.x+ X_Normal.x()*line_length, min_point.y+X_Normal.y()*line_length, min_point.z+X_Normal.z()*line_length);
	lineScourceX->Update();
	mapperX->SetInputConnection(lineScourceX->GetOutputPort());
	actorX->SetMapper(mapperX);
	actorX->GetProperty()->SetColor(1, 0, 0);
	actorX->GetProperty()->SetLineWidth(5.0);

	//Y轴
	lineScourceY->SetPoint1(min_point.x, min_point.y, min_point.z);
	lineScourceY->SetPoint2(min_point.x + Y_Normal.x()*line_length, min_point.y + Y_Normal.y()*line_length, min_point.z + Y_Normal.z()*line_length);
	lineScourceY->Update();
	mapperY->SetInputConnection(lineScourceY->GetOutputPort());
	actorY->SetMapper(mapperY);
	actorY->GetProperty()->SetColor(0, 1, 0);
	actorY->GetProperty()->SetLineWidth(5.0);
	
	//Z轴
	lineScourceZ->SetPoint1(min_point.x, min_point.y, min_point.z);
	lineScourceZ->SetPoint2(min_point.x + Z_Normal.x()*line_length, min_point.y + Z_Normal.y()*line_length, min_point.z + Z_Normal.z()*line_length);
	lineScourceZ->Update();
	mapperZ->SetInputConnection(lineScourceZ->GetOutputPort());
	actorZ->SetMapper(mapperZ);
	actorZ->GetProperty()->SetColor(0, 0, 1);
	actorZ->GetProperty()->SetLineWidth(5.0);


	actorX->SetVisibility(false);
	actorY->SetVisibility(false);
	actorZ->SetVisibility(false);

	renderer->AddActor(actorX);
	renderer->AddActor(actorY);
	renderer->AddActor(actorZ);
}

//显示AABB最小的坐标创建坐标轴
void MyVTK::displayAABBOrignalPosAxis(bool enable)
{
	actorX->SetVisibility(enable);
	actorY->SetVisibility(enable);
	actorZ->SetVisibility(enable);
}

void MyVTK::setFrontView()
{
	float pos = getAABBCoordinateMax(orignalCloud);
	camera->SetViewUp(0, 0, 1);
	camera->SetFocalPoint(0, 0, 0);
	camera->SetPosition(pos, 0, 0);
	camera->ParallelProjectionOn();
	renderer->SetActiveCamera(camera);
	renderer->ResetCamera();
	this->update();
}

void MyVTK::setTopView()
{
	float pos = getAABBCoordinateMax(orignalCloud);
	camera->SetViewUp(-1, 0, 0);
	camera->SetFocalPoint(0, 0, 0);
	camera->SetPosition(0, 0, pos);
	camera->ParallelProjectionOn();
	renderer->SetActiveCamera(camera);
	renderer->ResetCamera();
	this->update();
}

void MyVTK::setLeftView()
{
	float pos = getAABBCoordinateMax(orignalCloud);
	camera->SetViewUp(0, 1, 0);
	camera->SetFocalPoint(0, 0, 0);
	camera->SetPosition(0, -pos, 0);
	camera->ParallelProjectionOn();
	renderer->SetActiveCamera(camera);
	renderer->ResetCamera();
	this->update();
}

void MyVTK::setRightView()
{
	float pos = getAABBCoordinateMax(orignalCloud);
	camera->SetViewUp(0, 1, 0);
	camera->SetFocalPoint(0, 0, 0);
	camera->SetPosition(0, pos, 0);
	camera->ParallelProjectionOn();
	renderer->SetActiveCamera(camera);
	renderer->ResetCamera();
	this->update();
}

void MyVTK::setBottomView()
{
	float pos = getAABBCoordinateMax(orignalCloud);
	camera->SetViewUp(1, 0, 0);
	camera->SetFocalPoint(0, 0, 0);
	camera->SetPosition(0,0,-pos);
	camera->ParallelProjectionOn();
	renderer->SetActiveCamera(camera);
	renderer->ResetCamera();
	this->update();
}

void MyVTK::setBackView()
{
	float pos = getAABBCoordinateMax(orignalCloud);
	camera->SetViewUp(0, 0, 1);
	camera->SetFocalPoint(0, 0, 0);
	camera->SetPosition(-pos, 0, 0);
	camera->ParallelProjectionOn();
	renderer->SetActiveCamera(camera);
	renderer->ResetCamera();
	this->update();
}

vtkActor * MyVTK::appendCloudActor()
{
	
	getOrignalCloud(orignalCloud);		//传入主窗口的点云
	//qDebug() << orignalCloud->size();
	vtkPoints * points = vtkPoints::New();
	//points->Reset();
	vtkCellArray * cell = vtkCellArray::New();
	vtkLookupTable *lookup = vtkLookupTable::New();
	vtkPolyData * polyData = vtkPolyData::New();
	vtkFloatArray * myscalar = vtkFloatArray::New();
	vtkPolyVertex * polyVertex = vtkPolyVertex::New();
	vtkFloatArray * pointsScalars = vtkFloatArray::New();
	vtkActor * cloudActor = vtkActor::New();

	lookup->SetNumberOfTableValues(orignalCloud->size());
	lookup->Build();
		
	//设置点坐标和标量
	vtkIdType idtype;
	for (size_t i = 0; i < orignalCloud->points.size(); i++)
	{
		idtype = points->InsertNextPoint(orignalCloud->points[i].x, orignalCloud->points[i].y, orignalCloud->points[i].z);
		myscalar->InsertNextTuple1(1);
		lookup->SetTableValue(i, (double)orignalCloud->points[i].r / 255.0, (double)orignalCloud->points[i].g / 255.0, (double)orignalCloud->points[i].b / 255.0, 1);
		cell->InsertNextCell(1, &idtype);
	}

	//设置polyData
	polyData->SetPoints(points);
	polyData->SetVerts(cell);
	polyData->GetPointData()->SetScalars(myscalar);
	sendAABBPolydata(polyData);					//将AABBPolydata发送出去

	//设置顶点和标量
	polyVertex->GetPointIds()->SetNumberOfIds(orignalCloud->size());
	pointsScalars->SetNumberOfTuples(orignalCloud->size());
	for (size_t i = 0; i < orignalCloud->size(); i++)
	{
		polyVertex->GetPointIds()->SetId(i, i);
		pointsScalars->InsertValue(i, i);
	}

	//设置Grid
	vtkUnstructuredGrid *cloudGrid = vtkUnstructuredGrid::New();
	cloudGrid->Allocate(1, 1);
	cloudGrid->SetPoints(points);
	cloudGrid->GetPointData()->SetScalars(pointsScalars);
	cloudGrid->InsertNextCell(polyVertex->GetCellType(), polyVertex->GetPointIds());

	//设置mapper
	vtkDataSetMapper * cloudMapper = vtkDataSetMapper::New();
	cloudMapper->SetInputData(cloudGrid);
	cloudMapper->ScalarVisibilityOn();
	cloudMapper->SetScalarRange(0, orignalCloud->size() - 1);
	cloudMapper->SetLookupTable(lookup);

	//设置filter
	vtkPolyData * input = vtkPolyData::New();
	vtkIdFilter * idFilter = vtkIdFilter::New();
	idFilter->SetInputData(polyData);
#if VTK_MAJOR_VERSION >= 9
	idFilter->SetPointIdsArrayName("OriginalIds");
	idFilter->SetCellIdsArrayName("OriginalIds");
#else
	idFilter->SetIdsArrayName("OriginalIds");
#endif
	vtkDataSetSurfaceFilter * sufaceFilter = vtkDataSetSurfaceFilter::New();
	sufaceFilter->SetInputConnection(idFilter->GetOutputPort());
	sufaceFilter->Update();
	input = sufaceFilter->GetOutput();

	//设置mapper
	/*vtkPolyDataMapper* mapper = vtkPolyDataMapper::New();
	mapper->SetInputData(input);
	mapper->SetLookupTable(lookup);
	mapper->ScalarVisibilityOn();*/

	//设置演员
	 
	cloudActor->SetMapper(cloudMapper);
	cloudActor->GetProperty()->SetPointSize(2);
	cloudActor->GetProperty()->SetRepresentationToPoints();
	renderer->AddActor(cloudActor);
	
	return cloudActor;
	this->update();
}

//添加AABB包围盒演员
vtkActor * MyVTK::appendAABBActor()
{
	double bound[6];
	qDebug() << "OBB Size:\t"
		<< bound[1] - bound[0] << ", " << bound[3] - bound[2] << ", " << bound[5] - bound[4];

	//设置Filter
	vtkOutlineFilter *AABBOutlineData = vtkOutlineFilter::New();
	AABBOutlineData->SetInputData(AABB_Polydata);
	AABBOutlineData->Update();

	//设置Mapper
	vtkPolyDataMapper * mapOutline = vtkPolyDataMapper::New();
	mapOutline->SetInputConnection(AABBOutlineData->GetOutputPort());

	vtkActor *AABBActor = vtkActor::New();
	AABBActor->SetMapper(mapOutline);
	AABBActor->GetProperty()->SetColor(1, 0, 0);

	return AABBActor;
}

void MyVTK::display(vtkActor * actor)
{
	this->renderer->AddActor(actor);
	this->update();
}

void MyVTK::removeCloudDisplay(vtkActor * actor)
{
	this->renderer->RemoveActor(actor);
	this->update();
}

void MyVTK::removeAABBDisplay(vtkActor * actor)
{
	this->renderer->RemoveActor(actor);
	this->update();
}

void MyVTK::getOrignalCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud)
{
	this->orignalCloud = incloud;
}
