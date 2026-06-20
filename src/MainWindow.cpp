#include "MainWindow.h"
Q_DECLARE_METATYPE(MyCloudList)

static int iCount = 0;		//记录点云的ID号
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);
	
	this->resize(1920, 1080);


	myCloud = NULL;
	inCloud = NULL;
	itemCloud = NULL;
	itemProject = NULL;
	dlgClip = new QDlgClip;
	myItem = NULL;

	isAABB = false;	//是否显示AABB


	model = new QStandardItemModel(ui.treeView);	//将tree放入标准模式中
	model->setHorizontalHeaderLabels(QStringList() << "");	//设置水平表头
	icon_1 = QIcon(":/images/images/dbHObjectSymbol.png");
	icon_2 = QIcon(":/images/images/dbCloudSymbol.png");
	ui.treeView->setModel(this->model);
	ui.treeView->setItemsExpandable(true);		//默认全部展开
	ui.treeView->expandAll();

	//允许嵌套dock
	setDockNestingEnabled(true);
	
	
	//DBTree
	ui.dockWidget1->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	addDockWidget(Qt::LeftDockWidgetArea, ui.dockWidget1);


	//GraphicsView
	ui.dockWidget2->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	this->resizeDocks({ ui.dockWidget2 }, {(this->width()-ui.dockWidget2->width())/2}, Qt::Horizontal);
	addDockWidget(Qt::RightDockWidgetArea, ui.dockWidget2);
	ui.dockWidget2->setWindowTitle("");

	//Console
	ui.dockWidget3->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
	this->resizeDocks({ ui.dockWidget3 }, {200}, Qt::Vertical);
	addDockWidget(Qt::BottomDockWidgetArea, ui.dockWidget3);


	//CenterView VTK
	centerDock = new QDockWidget(this);
	myVTK = new MyVTK(this);
	centerDock->setAllowedAreas(Qt::NoDockWidgetArea);
	centerDock->setFeatures(QDockWidget::DockWidgetClosable);
	centerDock->setWidget(myVTK);
	//centerDock->setWindowTitle("VTK");
	this->setCentralWidget(centerDock);
	
	//this->setCursor(Qt::WaitCursor);

	//ui.statusBar->addWidget(ui.graphicsView->labelScene);	//显示坐标信息
	addXOYLabel();					//在GraphicsView左上方添加XOY坐标

	//添加比例尺
	addScaleWidget();


	connect(ui.actDBTreeVisible, SIGNAL(triggered(bool)), this, SLOT(slotOn_actDBTreeVisible_triggered(bool)));//DBTree窗口可见
	connect(ui.dockWidget1, SIGNAL(visibilityChanged(bool)), this, SLOT(slotOn_actDBTreeVisible_visibilityChanged(bool)));//DBTree窗口可见按钮
	connect(ui.actDBTreeFloat, SIGNAL(triggered(bool)), this, SLOT(slotOn_actDBTreeFloat_triggered(bool)));//DBTree窗口浮动
	connect(ui.dockWidget1, SIGNAL(topLevelChanged(bool)), this, SLOT(slotOn_actDBTreeFloat_topLevelChanged(bool)));//DBTree浮动按钮
	connect(ui.acGraViewVisible, SIGNAL(triggered(bool)), this, SLOT(slotOn_actGraViewVisible_triggered(bool)));//GraphView窗口可见
	connect(ui.dockWidget2, SIGNAL(visibilityChanged(bool)), this, SLOT(slotOn_actGraViewVisible_visibilityChanged(bool)));//GraphView窗口可见按钮
	connect(ui.actGRaViewFloat, SIGNAL(triggered(bool)), this, SLOT(slotOn_actGraViewFloat_triggered(bool)));//GraphView窗口浮动
	connect(ui.dockWidget2, SIGNAL(topLevelChanged(bool)), this, SLOT(slotOn_actGraViewFloat_topLevelChanged(bool)));//GraphView浮动按钮
	connect(ui.actVTKVisible, SIGNAL(triggered(bool)), this, SLOT(slotOn_actVTKVisible_triggered(bool)));//VTK窗口可见
	connect(this->centerDock, SIGNAL(visibilityChanged(bool)), this, SLOT(slotOn_actVTKViewVisible_visibilityChanged(bool)));//VTK窗口可见按钮
	connect(ui.actConsoleVisible, SIGNAL(triggered(bool)), this, SLOT(slotOn_actConsoleVisible_triggered(bool)));//Console窗口可见
	connect(ui.dockWidget3, SIGNAL(visibilityChanged(bool)), this, SLOT(slotOn_actConsoleVisible_visibilityChanged(bool)));//Console窗口可见按钮
	connect(ui.actConsoleFloat, SIGNAL(triggered(bool)), this, SLOT(slotOn_actConsoleFloat_triggered(bool)));//Console窗口浮动
	connect(ui.dockWidget3, SIGNAL(topLevelChanged(bool)), this, SLOT(slotOn_actConsoleFloat_topLevelChanged(bool)));//Console浮动按钮
	connect(ui.actFullScreen, SIGNAL(triggered()), this, SLOT(slotFullScreen())); //全屏
	connect(ui.actFrontView, SIGNAL(triggered()), this, SLOT(slotFrontView()));	//正视
	connect(ui.actTopView, SIGNAL(triggered()), this, SLOT(slotTopView()));	//顶视
	connect(ui.actBackView, SIGNAL(triggered()), this, SLOT(slotBackView()));	//后视
	connect(ui.actLeftView, SIGNAL(triggered()), this, SLOT(slotLeftView()));	//左视
	connect(ui.actRightView, SIGNAL(triggered()), this, SLOT(slotRightView()));	//右视
	connect(ui.actBottomView, SIGNAL(triggered()), this, SLOT(slotBottomView()));	//底视
	connect(ui.actOpenCloud, SIGNAL(triggered()), this, SLOT(slotOpenCloud()));		//打开点云
	connect(this, SIGNAL(sendOrignalCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr)), myVTK, SLOT(getOrignalCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr)));	//将主窗口点云送至VTK
	connect(ui.treeView, SIGNAL(clicked(QModelIndex)), this, SLOT(slotOn_treeView_clicked(QModelIndex)));	 //DB Tree点击事件
	connect(model, SIGNAL(itemChanged(QStandardItem*)), this, SLOT(slotOn_treeItemChanged(QStandardItem*)));		//DB Tree  发生变化
	connect(ui.actDelete, SIGNAL(triggered()), this, SLOT(slotOn_actDelete_triggered()));		//DB Tree 删除项
	connect(ui.actAABB, SIGNAL(triggered(bool)), this, SLOT(slotOn_actAABB_triggered(bool)));		//开启AABB按钮
	//connect(this, SIGNAL(sendDBItemChanged()), myVTK, SLOT(AABBOrignalPosAxis()));			//以AABB最小的坐标创建坐标轴
	connect(this, SIGNAL(sendStr2Console(QString)), this, SLOT(slotGetStr2Console(QString)));		//Console接受消息
	connect(ui.actRandomPlane, SIGNAL(triggered(bool)), this, SLOT(slotOn_actRandomPlane_triggered(bool)));			//任意平面切割
	connect(ui.actVerticalPlane, SIGNAL(triggered()), this, SLOT(slotOn_actVerticalPlane_triggered()));		//垂直平面切割
	connect(ui.actHorizonalPlane, SIGNAL(triggered(bool)), this, SLOT(slotOn_actHorizonalPlane_triggered(bool)));	//水平平面切割
	connect(this, SIGNAL(sendClipPlane(vtkPlaneWidget*)), myVTK, SLOT(getClipPlane(vtkPlaneWidget*)));	//将切割平面发送给VTK进行单独的函数处理
	connect(myVTK, SIGNAL(sendGetDBItem()), this, SLOT(DBTreeSendVTKItemCloud()));		//将DBTree下的Item点云送到VTK
	//connect(ui.graphicsView, SIGNAL(sendGetDBItem()), this, SLOT(DBTreeSendGraphicViewItemCloud()));		//将DBTree下的Item点云送到GraphicView
	connect(ui.treeView, SIGNAL(clicked(QModelIndex)), this, SLOT(setClipButtonEnable(QModelIndex)));	 //设置DBTree item点云与切割按钮的禁用与否
	connect(ui.treeView, SIGNAL(clicked(QModelIndex)), this, SLOT(setDlgClipPbnEnable(QModelIndex)));	 //设置切割对话框中的切割按键禁用与否
	connect(this, SIGNAL(sendDlgClipPbnEnable(bool)), myVTK, SLOT(getPbnClipEnable(bool)));		//VTK接受DLg对话框的切割按钮的禁用与否
	connect(myVTK, SIGNAL(sendStrFromVTK2Console(QString)), this, SLOT(slotGetStr2Console(QString)));	//GraphicView发送消息给Console
	connect(ui.graphicsView, SIGNAL(sendStrFromGraphicView2Console(QString)), this, SLOT(slotGetStr2Console(QString)));	//VTK发送消息给Console
	connect(ui.actMiGe, SIGNAL(triggered(bool)), ui.graphicsView, SLOT(slotOn_actMiGe_triggered(bool)));		//米格纸绘制
	connect(ui.actpoints, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actPoints_triggered()));		//Graphview添加点
	//connect(myVTK, SIGNAL(sendClipCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr)), ui.graphicsView, SLOT(getClipCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr)));		//VTK将切割成功的点云发送到Graphview中
	connect(myVTK, SIGNAL(sendActProjLineEnable(bool)), this, SLOT(setActProjLineEnable(bool)));		//触发投影连线信号
	connect(myVTK, SIGNAL(sendActProjXOYEnable(bool)), this, SLOT(setActProjXOYEnable(bool)));
	connect(myVTK, SIGNAL(sendActProjXOZEnable(bool)), this, SLOT(setActProjXOZEnable(bool)));
	connect(myVTK, SIGNAL(sendActProjYOZEnable(bool)), this, SLOT(setActProjYOZEnable(bool)));
	connect(myVTK, SIGNAL(sendActOverLookProjEnable(bool)), this, SLOT(setActOverLookProjEnable(bool)));
	connect(ui.actProjLine, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actProjLine_triggered()));		//投影连线按钮
	connect(ui.graphicsView,SIGNAL(mouseMovePoint(QPoint)), this, SLOT(slotOn_mouseMove_SceneCoordinate(QPoint)));	//鼠标追踪获得GraphicScene坐标
	connect(ui.graphicsView, SIGNAL(sendActor(vtkActor*)), myVTK, SLOT(getActorFromGraphicView(vtkActor*)));
	connect(ui.actProjYOZ, SIGNAL(triggered()), myVTK, SLOT(slotActProjYOZ_triggered()));		//投影到YOZ面按钮
	connect(ui.actProjXOY, SIGNAL(triggered()), myVTK, SLOT(slotActProjXOY_triggered()));		//投影到XOY面按钮
	connect(ui.actProjXOZ, SIGNAL(triggered()), myVTK, SLOT(slotActProjXOZ_triggered()));		//投影到XOZ面按钮
	connect(ui.actOverLookProj, SIGNAL(triggered()), myVTK, SLOT(slotActOverLookProj_triggered()));		//俯视投影按钮
	connect(myVTK, SIGNAL(sendProjCloud2GraphicsView(pcl::PointCloud<pcl::PointXYZRGB>::Ptr)), ui.graphicsView, SLOT(getProjCloudFromVTK(pcl::PointCloud<pcl::PointXYZRGB>::Ptr)));		//将VTK投影后的点云发送到GraphicsView
	connect(ui.actDeleteItem, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actDeleteItem_triggered()));		//GraphicsView删除按钮
	connect(ui.actClearScene, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actClearScene_triggered()));		//GraphicsView清空按钮
	connect(myVTK, SIGNAL(sendAABBBoxXYZMAX(float, float, float)), ui.graphicsView, SLOT(getAABBMaxFromVTK(float, float, float)));	//VTK将AABB最大边框发送到GraphicsView
	connect(myVTK, SIGNAL(sendIsOverLookProj(bool)), ui.graphicsView, SLOT(getisOverLookProj(bool)));	//判断是否进行俯视XOY点云连线
	connect(ui.actGroup, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actGroup_triggered()));		//GraphicsView组合按钮
	connect(ui.actBreak, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actBreak_triggered()));		//GraphicsView打散按钮
	connect(ui.actMoveUp, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actMoveUp_triggered()));	//GraphicsView上移按钮
	connect(ui.actMoveDown, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actMoveDown_triggered()));	//GraphicsView下移按钮
	connect(ui.actMoveLeft, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actMoveLeft_triggered()));	//GraphicsView左移按钮
	connect(ui.actMoveRight, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actMoveRight_triggered()));	//GraphicsView右移按钮
	connect(ui.actEditFront, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actEditFront_triggered()));	//GraphicsView前置按钮
	connect(ui.acEditBack, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actEditBack_triggered()));	//GraphicsView后置按钮
	connect(ui.actSave, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actSave_triggered()));			//GraphicsView保存按钮
	connect(ui.actCompass, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actCompass_triggered()));			//GraphicsView指北针按钮
	connect(ui.actText, SIGNAL(triggered()), ui.graphicsView, SLOT(slotOn_actText_triggered()));		//添加文字按钮
	connect(ui.graphicsView, SIGNAL(sendClosedXOYLabel(bool)), this, SLOT(getClosedXOYLabel(bool)));		//是否显示Label图片
	connect(ui.graphicsView, SIGNAL(sendClosedScale(bool)), this, SLOT(getClosedScale(bool)));		//是否显示比例尺
	connect(ui.graphicsView, SIGNAL(sendDlgClipVisible(bool)), myVTK, SLOT(setDlgClipVisible(bool)));		//是否隐藏VTK的平面裁剪对话框
	connect(scaleCombox, SIGNAL(currentIndexChanged(int)), ui.graphicsView, SLOT(getScaleComBoxCurrentIndexChanged(int)));		//将当比例尺改变比例尺发送给GraphicsView
	connect(ui.graphicsView, SIGNAL(sendGetCurrentScale()), this, SLOT(sendCurrentScaleToGraphicView()));	//发送当前比例尺到GraphicView
	connect(ui.graphicsView, SIGNAL(sendReDraw(ScaleType)), ui.graphicsView, SLOT(getReDraw(ScaleType)));		//比例尺改变时重新作图
	connect(ui.graphicsView, SIGNAL(sendScaleOffset(QPointF)), ui.graphicsView, SLOT(getScaleOffset(QPointF)));		//比例尺变化坐标的偏移量
	connect(ui.actPlotTab, SIGNAL(triggered()), ui.graphicsView,SLOT(slotOn_actPlotTab_triggered()));		//GraphicsView出图模板按钮
	

}



void MainWindow::slotOn_actGraViewVisible_triggered(bool checked)
{
	ui.dockWidget2->setVisible(checked);
}

//添加XOY标签
void MainWindow::addXOYLabel()
{
	xoy_label = new QLabel(this);
	QImage * img = new QImage();
	img->load(":/images/images/xymap.bmp");
	xoy_label->setPixmap(QPixmap::fromImage(*img));
	layout = new QHBoxLayout(ui.graphicsView);

	//设置布局
	layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	layout->addWidget(xoy_label);
	setXOYLabelVisible(true);
}

void MainWindow::setXOYLabelVisible(bool enable)
{
	this->xoy_label->setVisible(enable);
}

//添加比例尺
void MainWindow::addScaleWidget()
{
	scaleCombox = new QComboBox(this);
	QStringList scaleList;
	scaleList << "1:10" << "1:20" << "1:50" << "1:100";
	scaleCombox->addItems(scaleList);
	scaleCombox->setCurrentIndex(2);
	scaleCombox->setCursor(Qt::ArrowCursor);

	scaleLabel = new QLabel("比例尺：");
	scaleLabel->adjustSize();

	//设置布局
	//layout->setAlignment(Qt::AlignRight | Qt::AlignTop);
	layout->addWidget(scaleLabel);
	layout->addWidget(scaleCombox);

}

//设置比例尺的可见性
void MainWindow::setScaleVisible(bool enable)
{
	this->scaleCombox->setVisible(enable);
	this->scaleLabel->setVisible(enable);
}

//将LAS格式转为PCD格式
void MainWindow::las2PCD(string path, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &outCloud)
{
	//-------------------------- 加载las点云 --------------------------
	LASreadOpener lasLoad;
	lasLoad.set_file_name((char*)path.data());
	LASreader* lasReader = lasLoad.open();
	if (lasReader == NULL) return;
	uint32_t ptCount = lasReader->header.number_of_point_records;

	//点云中心
	double center[3];
	center[0] = lasReader->get_min_x() + (lasReader->get_max_x() - lasReader->get_min_x()) / 2.0;
	center[1] = lasReader->get_min_y() + (lasReader->get_max_y() - lasReader->get_min_y()) / 2.0;
	center[2] = lasReader->get_min_z() + (lasReader->get_max_z() - lasReader->get_min_z()) / 2.0;
	
	//cout << setprecision(16) << "center" << center[0] << "	" << center[1] << "	" << center[2] << "	" << endl;
	//去中心化

	while (lasReader->read_point())
	{
		double x = lasReader->point.get_x() - center[0];
		double y = lasReader->point.get_y() - center[1];
		double z = lasReader->point.get_z() - center[2];

		pcl::PointXYZRGB point;
		point.x = x;
		point.y = y;
		point.z = z;
		point.r = lasReader->point.get_R() * 255 / 65535;
		point.g = lasReader->point.get_G() * 255 / 65535;
		point.b = lasReader->point.get_B() * 255 / 65535;

		//cout <<setprecision(16)<< x << "	" << y << "	" << z << endl;
		//cout << point << endl;
		outCloud->points.push_back(point);
	}

	pcl::io::savePCDFileBinary("PCD2Las.pcd", *outCloud);
}

void MainWindow::slotOn_actGraViewVisible_visibilityChanged(bool visible)
{
	ui.acGraViewVisible->setChecked(visible);
}

void MainWindow::slotOn_actGraViewFloat_triggered(bool checked)
{
	ui.dockWidget2->setFloating(checked);
}

void MainWindow::slotOn_actGraViewFloat_topLevelChanged(bool topLevel)
{
	ui.actGRaViewFloat->setChecked(topLevel);
}

void MainWindow::slotOn_actVTKVisible_triggered(bool checked)
{
	centerDock->setVisible(checked);
}

void MainWindow::slotOn_actVTKViewVisible_visibilityChanged(bool visible)
{
	ui.actVTKVisible->setChecked(visible);
}

void MainWindow::slotOn_actConsoleVisible_triggered(bool checked)
{
	ui.dockWidget3->setVisible(checked);
}

void MainWindow::slotOn_actConsoleVisible_visibilityChanged(bool visible)
{
	ui.actConsoleVisible->setChecked(visible);
}

void MainWindow::slotOn_actConsoleFloat_triggered(bool checked)
{
	ui.dockWidget3->setFloating(checked);
}

void MainWindow::slotOn_actConsoleFloat_topLevelChanged(bool topLevel)
{
	ui.actConsoleFloat->setChecked(topLevel);
}

void MainWindow::slotOn_actDBTreeVisible_triggered(bool checked)
{
	ui.dockWidget1->setVisible(checked);
}

void MainWindow::slotOn_actDBTreeVisible_visibilityChanged(bool visible)
{
	ui.actDBTreeVisible->setChecked(visible);
}

void MainWindow::slotOn_actDBTreeFloat_triggered(bool checked)
{
	ui.dockWidget1->setFloating(checked);
}

void MainWindow::slotOn_actDBTreeFloat_topLevelChanged(bool topLevel)
{
	ui.actDBTreeFloat->setChecked(topLevel);
}

void MainWindow::slotFullScreen()
{
	this->setWindowState(Qt::WindowMaximized);
}

void MainWindow::slotFrontView()
{
	myVTK->setFrontView();
	emit sendStr2Console("前视图");
}

void MainWindow::slotTopView()
{
	myVTK->setTopView();
	emit sendStr2Console("顶视图");
}

void MainWindow::slotBottomView()
{
	myVTK->setBottomView();
	emit sendStr2Console("底视图");
}

void MainWindow::slotLeftView()
{
	myVTK->setLeftView();
	emit sendStr2Console("左视图");
}

void MainWindow::slotRightView()
{
	myVTK->setRightView();
	emit sendStr2Console("右视图");
}

void MainWindow::slotBackView()
{
	myVTK->setBackView();
	emit sendStr2Console("后视图");
}

//打开文件
void MainWindow::slotOpenCloud()
{
	QString filter = "PCD(*.pcd);;LAS(*.las);;所有(*.*)";
	QString path = QFileDialog::getOpenFileName(this, "打开点云文件", QCoreApplication::applicationDirPath(), filter);	//文件路径
	QString dir = path.right(path.length() - path.lastIndexOf("/") - 1);	//文件名
	QFileInfo fileInfo = QFileInfo(path);		
	QString fileSuffix = fileInfo.suffix();	//文件后缀
	//qDebug() << "fileSuffix:" << fileSuffix;
	
	if (path.isEmpty())		return;

	inCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
	string pathPCD = qstr2str(path);	//将Qstring转为String
	
	
	
	//读取点云
	if (fileSuffix == "pcd")
	{
		pcl::io::loadPCDFile(pathPCD, *inCloud);
		myCloud = new Cloud(inCloud);
		inCloud = myCloud->computeDecentrationCloud();
	}
	else if (fileSuffix == "las")
	{
		las2PCD(pathPCD, inCloud);
	}

	

	emit sendOrignalCloud(inCloud);

	//pcl::io::savePCDFileASCII("inCloud.pcd", *inCloud);

	//存储在DB Tree 的data中
	MyCloudList pointCloud;

	//添加点云演员
	vtkActor * cloud_actor = vtkActor::New();
	cloud_actor = myVTK->appendCloudActor();		

	//添AABB云演员
	vtkActor * AABB_actor = vtkActor::New();
	AABB_actor = myVTK->appendAABBActor();

	//将各种数据储存到自定义结构体中
	pointCloud.input_cloud = inCloud;
	pointCloud.id = iCount;
	pointCloud.cloudactor = cloud_actor;
	pointCloud.AABBactor = AABB_actor;
	pointCloudList.push_back(pointCloud);
	
	//发送给Console消息
	emit sendStr2Console(QString::asprintf("打开点云  %s", path.toStdString().c_str()));


	/////-------------------DB tree---------------------
	QModelIndex index = ui.treeView->currentIndex();
	QStandardItem * currentItem = model->itemFromIndex(index);
	QString str = dir + "(" + path + ")";

	
	
	//点云文件夹
	itemProject = new QStandardItem(icon_1, str);
	itemProject->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsEditable );
	itemProject->setCheckState(Qt::Checked);
	itemProject->setData(QVariant(path), Qt::UserRole);
	itemProject->setData(QVariant::fromValue(pointCloud), Qt::UserRole + 2);
	model->appendRow(itemProject);
	//else(currentItem->parent()->appendRow(itemProject));


	//点云文件
	QStandardItem * itemChild = new QStandardItem(icon_2, dir);
	ui.treeView->expand(itemProject->index());		//默认展开该项
	itemChild->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
	itemChild->setCheckState(Qt::Checked);
	itemChild->setData(QVariant(path), Qt::UserRole);
	itemChild->setData(QVariant(dir), Qt::UserRole+1);
	itemChild->setData(QVariant::fromValue(pointCloud), Qt::UserRole + 2);
	itemProject->appendRow(itemChild);

	
	myVTK->display(pointCloudList.back().cloudactor);		//显示点云
	//myVTK->display(pointCloudList.back().AABBactor);	   //显示AABB
	myVTK->initCamera();
	myVTK->update();
	++iCount;



	//设置AABB是否启用
	ui.actAABB->setEnabled(true);
	ui.actAABB->setChecked(false);
	ui.actAABB->setText("关闭包围盒");
}

//点击DB Tree项目
void MainWindow::slotOn_treeView_clicked(const QModelIndex & index)
{
	QStandardItem * currentItem = model->itemFromIndex(index);
	//qDebug() << currentItem->data(Qt::UserRole).toString();
	MyCloudList cloudData = currentItem->data(Qt::UserRole + 2).value<MyCloudList>();
	vtkActor * cloudDataActor = cloudData.cloudactor;
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr  itemCloud = cloudData.input_cloud;
	//qDebug() <<"cloudID"<< cloudData.id;

	//qDebug() << currentItem->text();

	//AABB按钮变化情况
	ui.actDelete->setEnabled(true);			//设置删除按钮是否启用
	if (currentItem->isCheckable())
	{
		if (currentItem->checkState() == Qt::Checked)
		{
			if (currentItem->hasChildren())
			{
				ui.actAABB->setEnabled(false);
				ui.actAABB->setText("关闭包围盒");
				ui.actAABB->setChecked(false);
			}
			else if (!currentItem->hasChildren())
			{
				if (isAABB)
				{
					ui.actAABB->setEnabled(true);
					ui.actAABB->setChecked(true);
					ui.actAABB->setText("显示包围盒");
				}
				else if (!isAABB)
				{
					ui.actAABB->setEnabled(true);
					ui.actAABB->setChecked(false);
					ui.actAABB->setText("关闭包围盒");
				}
			}
		}
		else if (currentItem->checkState() == Qt::Unchecked)
		{
			ui.actAABB->setEnabled(false);
			ui.actAABB->setText("关闭包围盒");
			ui.actAABB->setChecked(false);
		}
		
	}


	//DBTree check框变化
	if (currentItem->checkState() == Qt::Checked)
	{
		
		if (!currentItem->hasChildren())	//没有子成员
		{
			if (currentItem->parent() != 0)		//有父亲
			{
				myVTK->display(cloudDataActor);	//显示点云演员;
				

				if (isAABB)
				{
					ui.actAABB->setEnabled(true);
					ui.actAABB->setChecked(true);
					ui.actAABB->setText("显示包围盒");
					myVTK->display(cloudData.AABBactor);
				}
				else if(!isAABB)
				{
					ui.actAABB->setEnabled(true);
					ui.actAABB->setChecked(false);
					ui.actAABB->setText("关闭包围盒");
					myVTK->removeAABBDisplay(cloudData.AABBactor);
				}
			}
				
		}
		else                                 //有子成员
		{
			MyCloudList childData = currentItem->child(0)->data(Qt::UserRole + 2).value<MyCloudList>();
			myVTK->display(childData.cloudactor);		//显示子成员点云演员

			/*if (isAABB)
			{
				ui.actAABB->setEnabled(true);
				ui.actAABB->setChecked(true);
				ui.actAABB->setText("显示AABB");
				myVTK->display(childData.AABBactor);
			}
			else if (!isAABB)
			{
				ui.actAABB->setEnabled(true);
				ui.actAABB->setChecked(false);
				ui.actAABB->setText("关闭AABB");
				myVTK->removeAABBDisplay(childData.AABBactor);
			}*/
		}
	}
	else if (currentItem->checkState() == Qt::Unchecked)
	{

		if (!currentItem->hasChildren())	//没有子成员
		{
			if (currentItem->parent() != 0)		//有父亲
			{
				myVTK->removeCloudDisplay(cloudDataActor);	//移除点云演员


				ui.actAABB->setEnabled(false);
				ui.actAABB->setChecked(false);
				ui.actAABB->setText("关闭包围盒");
				isAABB = false;
				myVTK->removeAABBDisplay(cloudData.AABBactor);

			}
			
		}
		else                                //有子成员
		{
			MyCloudList childData = currentItem->child(0)->data(Qt::UserRole + 2).value<MyCloudList>();
			myVTK->removeCloudDisplay(childData.cloudactor);

			ui.actAABB->setEnabled(false);
			ui.actAABB->setChecked(false);
			ui.actAABB->setText("关闭包围盒");
			isAABB = false;
			myVTK->removeAABBDisplay(childData.AABBactor);

		}
	}


	//以AABB最小的坐标创建坐标轴
		if (currentItem->isCheckable())
		{
			if (!currentItem->hasChildren())
			{
				if (currentItem->checkState() == Qt::Checked)
				{
					myVTK->AABBOrignalPosAxis(itemCloud);	//将DBTree有效的点云发送给VTK
					if(myVTK->isActiveDlgClip)		myVTK->displayAABBOrignalPosAxis(true);		//是否显示AABB最小的坐标创建坐标轴
					
				}
				else if (currentItem->checkState() == Qt::Unchecked)
				{
					myVTK->displayAABBOrignalPosAxis(false);		//是否显示AABB最小的坐标创建坐标轴
				}
			}

			else if (currentItem->hasChildren())
			{
				if (currentItem->checkState() == Qt::Checked)
				{
					QStandardItem * childItem = currentItem->child(0);
					MyCloudList cloudData = childItem->data(Qt::UserRole + 2).value<MyCloudList>();
					myVTK->AABBOrignalPosAxis(cloudData.input_cloud);	//将DBTree有效的点云发送给VTK
					//myVTK->displayAABBOrignalPosAxis(true);		//是否显示AABB最小的坐标创建坐标轴
				}
				else if (currentItem->checkState() == Qt::Unchecked)
				{
					myVTK->displayAABBOrignalPosAxis(false);		//是否显示AABB最小的坐标创建坐标轴
				}
			}
		}
	
	else if(!myVTK->isActiveDlgClip)
	{
		myVTK->displayAABBOrignalPosAxis(false);		//是否显示AABB最小的坐标创建坐标轴
	}
}

//DB Tree  发生变化
void MainWindow::slotOn_treeItemChanged(QStandardItem * item)
{
	
	if (item == nullptr)		return;

	QModelIndex index = ui.treeView->currentIndex();
	QStandardItem * currentItem = model->itemFromIndex(index);
	//qDebug() <<" currentItem->type"<< currentItem->type();


	if (item->isCheckable())
	{
		if (item->hasChildren())
		{
			if (item->checkState() == Qt::Checked)
			{
				

				for (int i = 0; i < item->rowCount(); i++)
				{
					item->child(i)->setCheckState(Qt::Checked);
				}
			}
			else if (item->checkState() == Qt::Unchecked)
			{
				for (int i = 0; i < item->rowCount(); i++)
				{
					item->child(i)->setCheckState(Qt::Unchecked);
				}
			}

		}
		else if (!item->hasChildren())
		{
			QStandardItem * parentItem = new QStandardItem();
			parentItem = item->parent();
			if (parentItem == nullptr)	return;

			if (parentItem->isCheckable())
			{
				//if (parentItem->checkState() == Qt::Unchecked)		parentItem->setCheckState(Qt::PartiallyChecked);
				int isAllTure = 0;
				int isAllFalse = 0;


				for (size_t i = 0; i < parentItem->rowCount(); i++)
				{
					if (parentItem->child(i)->checkState() == Qt::Checked)	isAllTure++;
					if (parentItem->child(i)->checkState() == Qt::Unchecked)	isAllFalse++;
				}

				if (isAllTure != 0)
				{
					if (isAllTure == parentItem->rowCount())	parentItem->setCheckState(Qt::Checked);
					else(parentItem->setCheckState(Qt::PartiallyChecked));
				}
				if (isAllFalse == parentItem->rowCount())
				{
					parentItem->setCheckState(Qt::Unchecked);
				}
			}
		}
	}

	
}

//DB Tree 删除项
void MainWindow::slotOn_actDelete_triggered()
{
	QModelIndex index = ui.treeView->currentIndex();
	QStandardItem * currentItem = model->itemFromIndex(index);
	
	

	//if (parentItem == nullptr)	return;
	if (!model->hasChildren())
	{
		return;
	}
	else if (!currentItem->hasChildren() && currentItem->parent() == 0)
	{
		model->removeRow(currentItem->index().row());

		
	}
	else if (currentItem->hasChildren())		//删除点云文件夹
	{

		MyCloudList childData = currentItem->child(0)->data(Qt::UserRole + 2).value<MyCloudList>();
		myVTK->removeCloudDisplay(childData.cloudactor);		//移除点云演员
		myVTK->removeCloudDisplay(childData.AABBactor);
		
		while (currentItem->rowCount() != 0)
		{
			currentItem->removeRow(0);
		}
		model->removeRow(currentItem->index().row());
		

	}
	else if (!currentItem->parent() == 0)		//删除点云
	{
		
		MyCloudList myCloudData = currentItem->data(Qt::UserRole + 2).value<MyCloudList>();
		myVTK->removeCloudDisplay(myCloudData.cloudactor);		//移除点云演员
		myVTK->removeCloudDisplay(myCloudData.AABBactor);

		QStandardItem * parentItem = new QStandardItem();
		parentItem = currentItem->parent();
		parentItem->removeRow(currentItem->row());

		
		
		
	}

	if (!model->hasChildren())		//判断删除后DB Tree是否有其他项目
	{
		ui.actDelete->setEnabled(false);
		ui.actAABB->setEnabled(false);
		ui.actAABB->setText("关闭包围盒");

		ui.actRandomPlane->setEnabled(false);
		ui.actVerticalPlane->setEnabled(false);
		ui.actHorizonalPlane->setEnabled(false);

		isAABB = false;
	}
}

//开启AABB按钮
void MainWindow::slotOn_actAABB_triggered(bool checked)
{
	QModelIndex index = ui.treeView->currentIndex();
	QStandardItem * currentItem = model->itemFromIndex(index);
	MyCloudList cloudData = currentItem->data(Qt::UserRole + 2).value<MyCloudList>();

	if (checked)
	{
		ui.actAABB->setText("显示包围盒");
		isAABB = true;
		qDebug() << "显示包围盒";

		if (currentItem->isCheckable())
		{
			if (currentItem->checkState() == Qt::Checked)
			{
				if (!currentItem->hasChildren())		myVTK->display(cloudData.AABBactor);
				else 
				{
					//MyCloudList childData = currentItem->child(0)->data(Qt::UserRole + 2).value<MyCloudList>();
					//myVTK->display(childData.AABBactor);
					//ui.actAABB->setEnabled(false);
				}
			}
			else if (currentItem->checkState() == Qt::Unchecked)
			{
				if (!currentItem->hasChildren())		myVTK->removeAABBDisplay(cloudData.AABBactor);
			}
			
		}
	}
	else if(!checked)
	{
		ui.actAABB->setText("关闭包围盒");
		isAABB = false;
		qDebug() << "关闭包围盒";

		if (currentItem->isCheckable())
		{
			if (!currentItem->hasChildren())
			{
				myVTK->removeAABBDisplay(cloudData.AABBactor);
			}
			else
			{
				//MyCloudList childData = currentItem->child(0)->data(Qt::UserRole + 2).value<MyCloudList>();
				//myVTK->display(childData.AABBactor);
				//ui.actAABB->setEnabled(false);
			}
		}
	}

}

//接受消息到Console
void MainWindow::slotGetStr2Console(QString text)
{
	//获取系统时间
	QTime current_time = QTime::currentTime();
	QString time = QString::asprintf("[%d:%d:%d]", current_time.hour(), current_time.minute(), current_time.second());

	ui.listWidget->addItem(time + "\t" + text);
	ui.listWidget->setCurrentRow(ui.listWidget->count() - 1);
}

//随机平面
void MainWindow::slotOn_actRandomPlane_triggered(bool checked)
{
	//将切割按钮设置为禁止开启
	ui.actHorizonalPlane->setEnabled(false);
	ui.actRandomPlane->setEnabled(false);
	ui.actVerticalPlane->setEnabled(false);
	ui.actDelete->setEnabled(false);

	//获得随机平面
	vtkPlaneWidget * randomPlane;
	randomPlane = myVTK->DisplayRandomPlane();
	randomPlane->On();
	emit sendClipPlane(randomPlane);


	//弹出平面裁剪对话框
	myVTK->setDlgClip();
	emit sendStr2Console("已加载任意切割面");
	myVTK->displayAABBOrignalPosAxis(true);		//显示AABB最小的坐标创建坐标轴
	myVTK->update();
}

//垂直平面
void MainWindow::slotOn_actVerticalPlane_triggered()
{
	//将切割按钮设置为禁止开启
	ui.actHorizonalPlane->setEnabled(false);
	ui.actRandomPlane->setEnabled(false);
	ui.actVerticalPlane->setEnabled(false);
	ui.actDelete->setEnabled(false);

	//获得垂直平面
	vtkPlaneWidget * verticalPlane;
	verticalPlane = myVTK->DisplayVerticalPlane();
	verticalPlane->On();
	emit sendClipPlane(verticalPlane);


	//弹出平面裁剪对话框
	myVTK->setDlgClip();
	emit sendStr2Console("已加载竖直切割面");
	myVTK->displayAABBOrignalPosAxis(true);		//显示AABB最小的坐标创建坐标轴
	myVTK->update();
}

//水平平面
void MainWindow::slotOn_actHorizonalPlane_triggered(bool checked)
{
	// 将切割按钮设置为禁止开启
	ui.actHorizonalPlane->setEnabled(false);
	ui.actRandomPlane->setEnabled(false);
	ui.actVerticalPlane->setEnabled(false);
	ui.actDelete->setEnabled(false);

	//获得水平平面
	vtkPlaneWidget * horizonalPlane; 
	horizonalPlane = myVTK->DisplayHorizonalPlane();
	horizonalPlane->On();
	emit sendClipPlane(horizonalPlane);

	//弹出平面裁剪对话框
	myVTK->setDlgClip();
	emit sendStr2Console("已加载水平切割面");
	myVTK->displayAABBOrignalPosAxis(true);		//显示AABB最小的坐标创建坐标轴
	myVTK->update();
}

//将DBTree下的Item点云送到VTK
void MainWindow::DBTreeSendVTKItemCloud()
{
	QModelIndex index = ui.treeView->currentIndex();
	QStandardItem * currentItem = model->itemFromIndex(index);
	if (currentItem == nullptr	|| currentItem->hasChildren())		return;
	MyCloudList cloudData = currentItem->data(Qt::UserRole + 2).value<MyCloudList>();
	myVTK->getDBItemCloud(cloudData.input_cloud); 
	
}

//将DBTree下的Item点云送到GraphicView
void MainWindow::DBTreeSendGraphicViewItemCloud()
{
	QModelIndex index = ui.treeView->currentIndex();
	QStandardItem * currentItem = model->itemFromIndex(index);
	if (currentItem == nullptr || currentItem->hasChildren())		return;
	MyCloudList cloudData = currentItem->data(Qt::UserRole + 2).value<MyCloudList>();
	ui.graphicsView->getDBItemCloud(cloudData.input_cloud);
}

//设置DBTree item点云与切割按钮的禁用与否
void MainWindow::setClipButtonEnable(const QModelIndex & index)
{
	QStandardItem * currentItem = model->itemFromIndex(index);

	//判断当前Item是否能进行切割
	if (currentItem == nullptr)		return;

	if (currentItem->checkState() == Qt::Checked)
	{
		if (myVTK->isActiveDlgClip)
		{
			ui.actHorizonalPlane->setEnabled(false);
			ui.actRandomPlane->setEnabled(false);
			ui.actVerticalPlane->setEnabled(false);
		}
		else if (!myVTK->isActiveDlgClip)
		{
			if (!currentItem->hasChildren())
			{
				//dlgClip->setClipButtonEnable(true);
				ui.actHorizonalPlane->setEnabled(true);
				ui.actRandomPlane->setEnabled(true);
				ui.actVerticalPlane->setEnabled(true);
			}
			else if (currentItem->hasChildren())
			{
				//dlgClip->setClipButtonEnable(false);
				ui.actHorizonalPlane->setEnabled(false);
				ui.actRandomPlane->setEnabled(false);
				ui.actVerticalPlane->setEnabled(false);
			}
		}
	}
	else if (currentItem->checkState() == Qt::Unchecked)
	{
		ui.actHorizonalPlane->setEnabled(false);
		ui.actRandomPlane->setEnabled(false);
		ui.actVerticalPlane->setEnabled(false);
	}
}

//设置切割对话框中的切割按键禁用与否
void MainWindow::setDlgClipPbnEnable(const QModelIndex & index)
{
	//qDebug() << "myVTK->isActiveDlgClip" << myVTK->isActiveDlgClip;
	QStandardItem * currentItem = model->itemFromIndex(index);

	if (currentItem == nullptr)		return;
	if (currentItem->checkState() == Qt::Checked)
	{
		if (myVTK->isActiveDlgClip)
		{
			if (!currentItem->hasChildren())
			{
				
				emit sendDlgClipPbnEnable(true);
			}
			else
			{
				emit sendDlgClipPbnEnable(false);
			}
		}
		
	}
	else if (currentItem->checkState() == Qt::Unchecked)
	{
		if (myVTK->isActiveDlgClip)		emit sendDlgClipPbnEnable(false);
		
	}

	if (myVTK->isActiveDlgClip)
	{
		ui.actDelete->setEnabled(false);
	}
}

//设置投影连线按钮能否获得
void MainWindow::setActProjLineEnable(bool enable)
{
	ui.actProjLine->setEnabled(enable);
}

//设置投影到XOY按钮能否获得
void MainWindow::setActProjXOYEnable(bool enable)
{
	ui.actProjXOY->setEnabled(enable);
}

//设置投影到XOZ按钮能否获得
void MainWindow::setActProjXOZEnable(bool enable)
{
	ui.actProjXOZ->setEnabled(enable);
}

//设置投影到YOZ按钮能否获得
void MainWindow::setActProjYOZEnable(bool enable)
{
	ui.actProjYOZ->setEnabled(enable);
}

// 设置俯视投影按钮能否获得
void MainWindow::setActOverLookProjEnable(bool enable)
{
	ui.actOverLookProj->setEnabled(enable);
}

//鼠标追踪获得GraphicScene坐标
void MainWindow::slotOn_mouseMove_SceneCoordinate(QPoint point)
{
	QPointF pointScene = ui.graphicsView->mapToScene(point);
	ui.graphicsView->labelScene->setText(QString::asprintf("Scene坐标:%.1f,%.1f", pointScene.x(), pointScene.y()));
}

//接受是否关闭XOY图标
void MainWindow::getClosedXOYLabel(bool enable)
{
	setXOYLabelVisible(enable);
}

//接受是否关闭比例尺
void MainWindow::getClosedScale(bool enable)
{
	setScaleVisible(enable);
}

//发送当前比例尺到GraphicView
void MainWindow::sendCurrentScaleToGraphicView()
{
	ui.graphicsView->currentScaleIndex = this->scaleCombox->currentIndex();
}




