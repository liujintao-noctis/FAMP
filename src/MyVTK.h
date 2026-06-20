#pragma once

#include <QObject>
#include <QtWidgets/QMainWindow>
#include<vtkAutoInit.h>
#include<vtkVersion.h>
#include<qdebug.h>
#if VTK_MAJOR_VERSION >= 9
#include<QVTKOpenGLNativeWidget.h>
#define FAMP_QVTK_WIDGET QVTKOpenGLNativeWidget
#else
#include<QVTKWidget.h>
#define FAMP_QVTK_WIDGET QVTKWidget
#endif
#include<pcl/visualization/cloud_viewer.h>
#include<pcl/visualization/boost.h>
#include<vtkGenericOpenGLRenderWindow.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include<pcl/common/common.h>
#include<pcl/common/centroid.h>
#include <vtkActor.h>
#include <vtkSmartPointer.h>
#include <vtkCameraActor.h>
#include <vtkCellArray.h>
#include <vtkNamedColors.h>
#include <vtkSphereSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkCamera.h>
#include <vtkPlanes.h>
#include<vtkPlane.h>
#include <vtkMapper.h>
#include <vtkProperty.h>
#include<vtkVertexGlyphFilter.h>
#include<vtkClipPolyData.h>
#include<vtkImplicitPlaneWidget2.h>
#include<vtkImplicitPlaneRepresentation.h>
#include<pcl/visualization/pcl_visualizer.h>
#include<vtkLineSource.h>
#include<vtkOutputWindow.h>
#include<vtkLookupTable.h>
#include<vtkFloatArray.h>
#include<vtkPolyVertex.h>
#include<vtkUnstructuredGrid.h>
#include<vtkDataSetMapper.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkInteractorStyleTrackballActor.h>
#include <vtkAxesActor.h>
#include <vtkOrientationMarkerWidget.h>
#include<vtkPlaneWidget.h>
#include<vtkPlane.h>
#include<vtkMath.h>
#include<vtkCamera.h>
#include<vtkOutlineFilter.h>
#include <vtkOBBTree.h>
#include<vtkSliderRepresentation2D.h>
#include<vtkSliderWidget.h>
#include<vtkCommand.h>
#include <vtkSphereSource.h>
#include<vtkPointSource.h>
#include<vtkIdFilter.h>
#include<vtkDataSetSurfaceFilter.h>
#include<vtkPlaneSource.h>
#include<vtkPointsProjectedHull.h>
#include<vtkRenderer.h>
#include<vtkRenderWindow.h>
#include<vtkRenderWindowInteractor.h>
#include<qmessagebox.h>
#include<cmath>
#include<vector>
#include<qvector3d.h>
#include<qfiledialog.h>
#include"QstringAndStringConvert.h"
#include"QDlgClip.h"
#pragma execution_character_set("utf-8")
class MyVTK : public FAMP_QVTK_WIDGET
{
	Q_OBJECT

public:
	MyVTK(QWidget *parent);
	~MyVTK();

private:
	//渲染器、交互器、渲染窗口、颜色
	vtkRenderer * renderer;
	vtkGenericOpenGLRenderWindow * renderWindow;
	vtkRenderWindowInteractor * renderWindowInteractor;
	vtkNamedColors *colors;

	//显示点云
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr orignalCloud;	//从主窗口获得的点云
	vtkCamera * camera;
	vtkPolyData * AABB_Polydata;


private:
	void Init();//初始化渲染器交互器
	void mixBackGround();	//混色背景
	void setWidgetAxes();	//坐标轴
	float getAABBCoordinateMax(pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud);	//获得AABB最大值最小值之差
	
	vtkPlaneWidget * randomPlane;		//随机平面
	vtkPlaneWidget * horizonalPlane;	//水平平面
	vtkPlaneWidget * verticalPlane;		//垂直平面
	vtkPlaneWidget * clipPlane;		//切割平面
	QDlgClip * dlgClip;				//切割平面对话框
	QDlgClip * vtkDlgClip;
	
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr currentItemCloud;		//获得DB Tree下的点云

	//切割的点显示的VTK数据
	vtkPolyData * selectedPolyData;
	vtkPoints * selectedPoints;		//切割选中的点
	vtkIdType selectedidtype;
	vtkCellArray *cellSelected;
	vtkPolyDataMapper* selectedmapper;
	vtkVertexGlyphFilter* selectedGlfilter;
	vtkActor * selectedActor;

	// 计算AABB包围盒和相关参数
	void computeCurrentCloudAABB(pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud);					//计算选中的点云AABB包围盒
	pcl::PointXYZRGB min_point_AABB;
	pcl::PointXYZRGB max_point_AABB;
	pcl::PointXYZRGB P1;
	pcl::PointXYZRGB P2;
	pcl::PointXYZRGB P3;
	pcl::PointXYZRGB P4;
	float XMaxMin;
	float YMaxMin;
	float ZMaxMin;
	QVector3D XOYNormal;
	QVector3D XOZNormal;
	QVector3D YOZNormal;


	//-----切割的点云投影到包围盒面
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_cut_projection;	//将切割的点云储存用于投影
	
	//投影到平面的点
	vtkPlane * planeProject;
	vtkPolyData * projectPointPolyData;
	vtkPoints * projectPoints;
	vtkCellArray *projectPointCell;
	vtkPolyDataMapper* projectPointmapper;
	vtkVertexGlyphFilter* projectPointGlfilter;
	vtkActor * projectPointActor;
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr projectPointCloud;	//储存切割点云在投影后的坐标

	
	//---------创建以AABB最小的坐标创建坐标轴
	vtkLineSource * lineScourceX;
	vtkLineSource * lineScourceY;
	vtkLineSource * lineScourceZ;
	vtkPolyDataMapper * mapperX;
	vtkPolyDataMapper * mapperY;
	vtkPolyDataMapper * mapperZ;
	vtkActor * actorX;
	vtkActor * actorY;
	vtkActor * actorZ;




public:
	void setFrontView();	//正视图
	void setTopView();		//顶视图
	void setLeftView();		//左视图
	void setRightView();	//右视图
	void setBottomView();	//底视图
	void setBackView();	//底视图
	vtkActor * appendCloudActor();		//添加点云演员
	vtkActor * appendAABBActor();		//添加AABB包围盒演员
	void display(vtkActor * actor);		//显示点云
	void removeCloudDisplay(vtkActor * actor);		//移除点云演员显示
	void removeAABBDisplay(vtkActor * actor);		//移除AABB演员显示
	void initCamera();		//初始化相机
	vtkPlaneWidget * DisplayVerticalPlane();		//显示相机垂直面
	vtkPlaneWidget * DisplayHorizonalPlane();		//显示相机垂直面
	vtkPlaneWidget * DisplayRandomPlane();			//显示随机面
	void beginClipPlane();							//开始进行切割
	void  setDlgClip();						//弹出切割平面对话框
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_cut_plane;		//在切割面上的点云
	void setQDlgClipNULL();					//将QDLG对话框设置空指针
	void getDBItemCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud);		//获得DBTree下的点云
	void endCutRemoveActors();					//切割结束后关闭掉切割平面和移除掉显示的选中的演员
	bool isActiveDlgClip;			//切割平面对话框是否激活
	void saveDlgCloudFile();			//保存点云文件
	bool isClipSucessed;			//判断是否已经切割成功生成点
	void triggeredSignalProj();		//触发sendActProjLineEnable(bool enable)信号
	bool isProjectionSuccess;		//是否投影完成开始在米格纸上绘图
	void triggeredSignalXOYProj();	//触发发送投影到XOY按钮的信号
	void triggeredSignalXOZProj();	//触发发送投影到XOZ按钮的信号
	void triggeredSignalYOZProj();	//触发发送投影到YOZ按钮的信号
	void triggeredSignalOverLookProj();	//触发发送俯视投影按钮的信号
	void AABBOrignalPosAxis(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud);		//以AABB最小的坐标创建坐标轴
	void displayAABBOrignalPosAxis(bool enable);			//显示AABB最小的坐标创建坐标轴

public slots:
	void getOrignalCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud);//从主窗口获得点云
	void getAABBPolydata(vtkPolyData * polyData);				//获得AABBPolydata
	void getClipPlane(vtkPlaneWidget *plane);					//接受主窗口发送过来的切割面
	void getPbnClipEnable(bool enable);			//从主窗口获得切割按钮禁用与否
	void getActorFromGraphicView(vtkActor *actor);		//接受GraphicsView发送过来的演员
	void slotActProjYOZ_triggered();		//投影到YOZ面按钮
	void slotActProjXOZ_triggered();	//投影到XOZ面按钮
	void slotActProjXOY_triggered();	//投影到XOY面按钮
	void slotActOverLookProj_triggered();		//俯视投影按钮
	void setDlgClipVisible(bool enable);		//设置弹出的对话框是否隐藏
	

signals:
	void sendAABBPolydata(vtkPolyData * polyData);				//发送AABBPolydata
	void sendGetDBItem();		//获得当前点击下DB Tree下的点云
	void sendStrFromVTK2Console(QString str);
	void sendClipCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud);		//将裁剪成功生成的点云发送到米格纸界面进行投影
	void sendActProjLineEnable(bool enable);		//发送投影按钮能否获得
	void sendActProjXOYEnable(bool enable);		//发送投影到XOY按钮能否获得
	void sendActProjXOZEnable(bool enable);		//发送投影到XOZ按钮能否获得
	void sendActProjYOZEnable(bool enable);		//发送投影到YOZ按钮能否获得
	void sendActOverLookProjEnable(bool enable);		//发送俯视投影按钮能否获得
	void sendIsOverLookProj(bool isproj);		//发送判断是否进行俯视投影
	void sendProjCloud2GraphicsView(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud);	//将投影后的点云发送到GraphicsView
	void sendAABBBoxXYZMAX(float x, float y, float z);		//将AABB的边界框发送到GraphicsView
};	
