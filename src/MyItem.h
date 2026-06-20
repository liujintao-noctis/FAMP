/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: V1.0
 * Description: 曲线绘制 QGraphicsItem 子类 — B 样条曲线渲染
 *****************************************************************/

#pragma once

#include <QObject>
#include <QGraphicsItem>
#include <QVector>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QVector2D>
#include <QVector3D>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QGraphicsSceneMouseEvent>
#include <pcl/point_types.h>
#include <pcl/point_cloud.h>

// 投影的类型的枚举
typedef enum _ProjectType
{ XOY, XOZ, YOZ, OLXOY,XOYLine,NONE
} ProjectType;

inline constexpr double PI = 3.14159265358979;

///**********************************------------------------------------------*******************************//

///**************************               重写QGraphicsItem类                          ***********************//

///**********************************------------------------------------------*******************************//

class MyItem:public QObject, public QGraphicsItem
{
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)
public:
    MyItem(QVector<QPointF> &points, ProjectType project_type, QGraphicsItem* parentItem = nullptr);
    MyItem();
    ~MyItem();

public:
    enum ItemOperator       //选择要处理的模式
    {
        t_none,             //什么都不做
        t_move,             //平移
        t_rotate            //旋转
    };

    QRectF boundingRect()const;     //返回的边界
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
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr  itemCloud;      //全局变量点云
    QVector<QPointF> itemPoints;                        //全局变量QT点集
    ProjectType projectType;            //投影类型
    QPointF minPT;      //点集中x的最大最小值
    QPointF maxPt;      //点集中y的最大最小值

    QRectF AABB;        //AABB包围盒
    QRectF customRect;          //最初原始的边界
    QPointF scenePos;// 获得鼠标scene坐标
    QPointF itemPos;

    bool drawRectBoundary;  //是否已画出边框
    ItemOperator item_operator;     // 旋转的模式
    QPointF press_pos;          //鼠标按压的坐标
    qreal itemRotateAngle = 0.0f;       // 当前旋转角度
    QTransform itemTransform;       // 变换矩阵

private:
    void getMinMaxQtPoints(const QVector<QPointF> &points, QPointF &minPT, QPointF &maxPt);     //计算获得Qt点集的XY的最大最小值
    float getDistance(const QPointF &point1, const QPointF &point2);            //计算两点之间的距离
    void drawCustomRect(QPainter *painter);         //绘制出边界框
    void drawLine(QPainter *painter);       //将点连线
    void mouseCursorShape();        //鼠标的样式
    void mouseMoveRotate(const QPointF & movePos);          //处理旋转

    //------------3次B样条曲线-----------
    QVector<QPointF> BSplineControlPoints(const QVector<QPointF> & points);     //通过离散点反求控制点
    QVector<QPointF> BSplineFitPoints(const QVector<QPointF> & controlpoints, bool closed, double stride);  //求出B样条曲线拟合函数插值点

public slots:
    //void getSceneCoordinateFromGraphicsView(QPoint point);        //从GraphicsView追踪鼠标坐标

signals:
    void sendStrFromGraphicsView2MainWindow(QString str);       //  发送消息到Console控制台
};
