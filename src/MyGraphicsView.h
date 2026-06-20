#pragma once

#include <QObject>
#include<qwidget.h>
#include <iostream>
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/io/pcd_io.h>
#include <pcl/kdtree/kdtree_flann.h>
#include<pcl/filters/uniform_sampling.h>
#include<pcl/filters/voxel_grid.h>
#include <pcl/search/kdtree.h>
#include <QtWidgets/QMainWindow>
#include<qlabel.h>
#include<qgraphicsscene.h>
#include<qgraphicsview.h>
#include<qstring.h>
#include<qcombobox.h>
#include<qgraphicsitem.h>
#include<qapplication.h>
#include<qrect.h>
#include<qdebug.h>
#include<qmath.h>
#include<qgraphicssceneevent.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include<pcl/common/common.h>
#include<pcl/common/distances.h>
#include<qpoint.h>
#include<qevent.h>
#include<qpen.h>
#include<qrect.h>
#include<qfiledialog.h>
#include"MyVTK.h"
#include<qitemselectionmodel.h>
#include<qstandarditemmodel.h>
#include <pcl/features/moment_of_inertia_estimation.h>
#include<vtkPlane.h>
#include<qvector3d.h>
#include<qfontdialog.h>
#include<qinputdialog.h>
#include<queue>
#include<time.h>
//#include"MyVTK.h"
#include"QDlgPlotTab.h"



// 投影的类型的枚举
typedef enum _ProjectType
{ XOY, XOZ, YOZ, OLXOY,XOYLine,NONE 
} ProjectType;	

//比例尺类型枚举
typedef enum _ScaleType
{
	OneToTen,OneToTwenty,OneToFifty,OneToHundred
}ScaleType;

//点云中距离最远的两点
struct Point2PointDisIndex
{
	float distance;
	int index1;
	int index2;
};

struct MyOrderCloudType
{
	ProjectType project_type;
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr  orderCloud;
};


struct DPPoint		//DP算法
{
	int ID;
	string Name;
	float x;
	float y;
	float z;
	bool isRemoved = false;
};

class MyGraphicsView : public QGraphicsView
{
	Q_OBJECT

public:
	MyGraphicsView(QWidget *parent);
	~MyGraphicsView();
	
public:
	QLabel *labelScene;		//显示坐标信息
	void getDBItemCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud);		//获得DBtree的点云
	QPointF gvScenePos;		//鼠标的Scene坐标
	int currentScaleIndex;	//当前比例尺索引

	QString dataText;
	QString designerText;
	QString scaleText;
	void setDlgPlotTabNull();			//设置出图表对话框为空指针
	void drawFormTable();			//绘制出图模板表格
	void getText();					//获得制图人，比例尺，日期文字


private:
	QGraphicsScene * scene;
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr project_cloud;	//VTK投影后的点云
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr currentItemCloud;	//获得DBtree的点云
	double computeCloudMeanDis(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud);		//计算点云的平均密度（点与点之间的平均距离）
	ProjectType projectType(pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud);			//判断投影的点云是投影到哪个面
	ProjectType project_type;	//投影的类型
	int dlgDrawXOYLine();		//是否绘制切割线对话框
	bool isOverLookProjLine;	//是否进行俯视XOY点云连线
	void drawXOZ(QPointF offset);		//画出XOZ面的投影
	void drawYOZ(QPointF offset);		//画出YOZ面的投影
	void drawXOY(QPointF offset);		//画出XOY面的投影
	void drawOverLookXOY(QPointF offset);		//画出俯视XOY面的投影
	void drawXOYLine(QPointF offset);		//画出XOY面的剖线

	void KNNAlphaShape(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr &cloud, float Alpha, float neborRadius, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &outcloud);	//KNNAlphaShape提取边界
	void findMaxDistancePointsofCloud(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & incloud, int headendPointIndex[2]);	//寻找点云中最远的两点的索引
	void orderSoetCloud(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & incloud, int neborNumbers,pcl::PointCloud<pcl::PointXYZRGB>::Ptr & outcloud);	//将无序点云进行有序排列

	//根据点云和投影类型将PCL按照不同比例尺转为QT点集
	void PCLCloud2QTPoints(pcl::PointCloud<pcl::PointXYZRGB>::Ptr  incloud, ProjectType protype, QVector<QPointF> &points,QPointF offset);

	//AABB边界框信息
	float XMaxMin;
	float YMaxMin;
	float ZMaxMin;
	double cloudDensity;	//传入的点云密度

	int backZ = 0;		//前置
	int frontZ = 0;		//后置

	static const int ItemName = 1;	//自定义Item名字
	static const int ItemCloud = 2;		//自定义储存点云
	ScaleType scaleType;	//当前比例尺
	QPointF deltaOffset;		//比例尺变换后x,y的偏移量
	void ReDraw(QPointF offset);	//比例尺改变后重新绘制


	QDlgPlotTab  *dlgPlotTab;
	void setDlgPlotTab();			//设置弹出出图模板对话框
	
	


	//----------DP算法------------
	double point2LineDist(DPPoint p1, DPPoint p2, DPPoint p3);		//计算点到线的距离
	double getMaxDist(vector<DPPoint> &Points, int begin, int end);		//计算点集中的最大值
	int getMaxDistIndex(vector<DPPoint> &Points, int begin, int end);		//获得最大距离的索引
	void computeDP(vector<DPPoint> &Points, int begin, int end, double threshold);	//DP算法简化线条点

protected:
	//鼠标键盘重写事件
	void keyPressEvent(QKeyEvent *e) Q_DECL_OVERRIDE;
	void mousePressEvent(QMouseEvent *e) Q_DECL_OVERRIDE;
	void mouseMoveEvent(QMouseEvent *e) Q_DECL_OVERRIDE;
	void mouseDoubleClickEvent(QMouseEvent *e);

public slots:
	void slotOn_actMiGe_triggered(bool checked);		//显示米格纸
	void slotOn_actPoints_triggered();
	void slotOn_actProjLine_triggered();			//投影连线按钮
	void getProjCloudFromVTK(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud);	//从VTK接受投影后的点云
	void slotOn_actDeleteItem_triggered();			//删除
	void slotOn_actClearScene_triggered();			//清空
	void getAABBMaxFromVTK(float x, float y, float z);		//接受VTK发送的AABB的边界框
	void getisOverLookProj(bool isproj);		//接受是否进行俯视XOY连线
	void slotOn_actGroup_triggered();			//组合按钮
	void slotOn_actBreak_triggered();			//打散按钮
	void slotOn_actMoveUp_triggered();			//向上移动
	void slotOn_actMoveDown_triggered();			//向下移动
	void slotOn_actMoveLeft_triggered();			//向左移动
	void slotOn_actMoveRight_triggered();			//向右移动
	void slotOn_actEditFront_triggered();			//前置按钮
	void slotOn_actEditBack_triggered();			//后置按钮
	void slotOn_actSave_triggered();			//保存按钮
	void slotOn_actCompass_triggered();			//指北针按钮
	void slotOn_actText_triggered();			//添加文字按钮
	void getScaleComBoxCurrentIndexChanged(int index);			//接受ComBox改变时发送过来的比例尺
	
	void getReDraw(ScaleType scale);				//接受改变比例尺时重新画图
	void getScaleOffset(QPointF offset);		//得到比例尺变化后坐标的偏移量
	void slotOn_actPlotTab_triggered();			//出图模板按钮

	
signals:
	void keyPress(QKeyEvent *e);
	void mouseClicked(QPoint point);
	void mouseMovePoint(QPoint point);
	void sendActor(vtkActor * actor);	//发送演员
	void sendStrFromGraphicView2Console(QString str);	//将消息发送到Console
	void sendClosedXOYLabel(bool enable);		//发送是否关闭XOY图标
	void sendClosedScale(bool enable);			//发送是否关闭比例尺
	void sendDlgClipVisible(bool enable);		//发送是否隐藏VTK的平面裁剪对话框
	void sendReDraw(ScaleType scale);			//发送改变比例尺时重新画图
	void sendScaleOffset(QPointF offset);		//比例尺变化后坐标的偏移量
	void sendGetCurrentScale();			//发送获得当前比例尺信号
	//void sendGetText();					//发送获得制图人，比例尺，日期的信号
};




///**********************************------------------------------------------*******************************//

///**************************				重写QGraphicsItem类							***********************//

///**********************************------------------------------------------*******************************//

//重写item绘制曲线
#define PI 3.14159265358979
class MyItem:public QObject, public QGraphicsItem
{
	Q_OBJECT

public:
	MyItem(QVector<QPointF> &points, ProjectType project_type, QGraphicsItem* parentItem = nullptr);
	MyItem();
	~MyItem();

public:
	enum ItemOperator		//选择要处理的模式
	{
		t_none,				//什么都不做
		t_move,				//平移
		t_rotate			//旋转
	};
	
	

	QRectF boundingRect()const;		//返回的边界
protected:
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
	

	// 获取自定义绘制所需要的矩形
	QRectF getCustomRect(void) const;

	// 自定义元素绘制
	virtual void customPaint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

	//重写鼠标事件
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr  itemCloud;		//全局变量点云
	QVector<QPointF> itemPoints;						//全局变量QT点集
	ProjectType projectType;			//投影类型
	QPointF minPT;		//点集中x的最大最小值
	QPointF maxPt;		//点集中y的最大最小值

	QRectF AABB;		//AABB包围盒
	QRectF customRect;			//最初原始的边界
	QPointF scenePos;//	获得鼠标scene坐标
	QPointF itemPos;

	bool drawRectBoundary;	//是否已画出边框
	ItemOperator item_operator;		// 旋转的模式
	QPointF press_pos;			//鼠标按压的坐标
	qreal itemRotateAngle = 0.0f;		// 当前旋转角度
	QTransform itemTransform;		// 变换矩阵

private:
	void getMinMaxQtPoints(const QVector<QPointF> &points, QPointF &minPT, QPointF &maxPt);		//计算获得Qt点集的XY的最大最小值
	float getDistance(const QPointF &point1, const QPointF &point2);			//计算两点之间的距离
	void drawCustomRect(QPainter *painter);			//绘制出边界框
	void drawLine(QPainter *painter);		//将点连线
	void mouseCursorShape();		//鼠标的样式
	void mouseMoveRotate(const QPointF & movePos);			//处理旋转


	//------------3次B样条曲线-----------
	QVector<QPointF> BSplineControlPoints(const QVector<QPointF> & points);		//通过离散点反求控制点
	QVector<QPointF> BSplineFitPoints(const QVector<QPointF> & controlpoints, bool closed, double stride);	//求出B样条曲线拟合函数插值点



public slots:
	//void getSceneCoordinateFromGraphicsView(QPoint point);		//从GraphicsView追踪鼠标坐标

signals:
	void sendStrFromGraphicsView2MainWindow(QString str);		//	发送消息到Console控制台
};









///**********************************------------------------------------------*******************************//

///**************************				重写QGraphicsItem，指北针							***********************//

///**********************************------------------------------------------*******************************//

class CompassItem :public QObject, public QGraphicsItem
{

	Q_OBJECT

public:
	CompassItem();
	~CompassItem();

	enum ItemOperator		//选择要处理的模式
	{
		t_none,				//什么都不做
		t_move,				//平移
		t_rotate			//旋转
	};

private:
	QRectF AABB;		//AABB包围盒
	QRectF customRect;			//最初原始的边界
	QPointF offset;			//离原点偏移的x,y量
	QPointF scenePos;//	获得鼠标scene坐标
	QPointF itemPos;
	ItemOperator item_operator;		// 旋转的模式
	QPointF press_pos;			//鼠标按压的坐标
	qreal itemRotateAngle = 0.0f;		// 当前旋转角度
	QTransform itemTransform;		// 变换矩阵

protected:
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);


	// 获取自定义绘制所需要的矩形
	QRectF getCustomRect(void) const;

	QRectF boundingRect()const;		//返回的边界

	// 自定义元素绘制
	virtual void customPaint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

	//重写鼠标事件
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
	void drawCustomRect(QPainter *painter);			//绘制出边界框
	void drawPicture(QPainter *painter);				//绘制出compass
	float getDistance(const QPointF &point1, const QPointF &point2);			//计算两点之间的距离
	void mouseMoveRotate(const QPointF & movePos);			//处理旋转
	void mouseCursorShape();		//鼠标的样式
};




///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

///**************************				重写QGraphicsItem,出图模板						***********************//

///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

class FormTabulationItem :public QObject, public QGraphicsItem
{
	Q_OBJECT
public:
	FormTabulationItem(QString designer, QString data, QString scale, QWidget *parent);
	~FormTabulationItem();

private:
	QRectF AABB;		//AABB包围盒
	QRectF customRect;			//最初原始的边界
	QPointF offset;			//离原点偏移的x,y量

	QString currentDataText;
	QString currentDesignerText;
	QString currentScaleText;

	void drawPlotTab(QPainter *painter);	//绘制出图模板样式
	void drawAddText(QPainter *painter,const QString &text,const QPointF &offset);		//添加输入的文字

protected:
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);


	// 获取自定义绘制所需要的矩形
	QRectF getCustomRect(void) const;

	QRectF boundingRect()const;		//返回的边界

	// 自定义元素绘制
	virtual void customPaint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
};
