/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 指北针 QGraphicsItem 子类
 *****************************************************************/

#pragma once

#include <QObject>
#include <QGraphicsItem>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QVector2D>
#include <QVector3D>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QGraphicsSceneMouseEvent>
#include "MyItem.h"

///**********************************------------------------------------------*******************************//

///**************************               重写QGraphicsItem，指北针                         ***********************//

///**********************************------------------------------------------*******************************//

class CompassItem :public QObject, public QGraphicsItem
{

    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)
public:
    CompassItem();
    ~CompassItem();

    enum ItemOperator       //选择要处理的模式
    {
        t_none,             //什么都不做
        t_move,             //平移
        t_rotate            //旋转
    };

private:
    QRectF AABB;        //AABB包围盒
    QRectF customRect;          //最初原始的边界
    QPointF offset;         //离原点偏移的x,y量
    QPointF scenePos;// 获得鼠标scene坐标
    QPointF itemPos;
    ItemOperator item_operator;     // 旋转的模式
    QPointF press_pos;          //鼠标按压的坐标
    qreal itemRotateAngle = 0.0f;       // 当前旋转角度
    QTransform itemTransform;       // 变换矩阵

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

    // 获取自定义绘制所需要的矩形
    QRectF getCustomRect(void) const;

    QRectF boundingRect()const;     //返回的边界

    // 自定义元素绘制
    virtual void customPaint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

    //重写鼠标事件
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    QVariant itemChange(GraphicsItemChange change,
                        const QVariant& value) override;

private:
    void drawCustomRect(QPainter *painter);         //绘制出边界框
    void drawPicture(QPainter *painter);                //绘制出compass
    float getDistance(const QPointF &point1, const QPointF &point2);            //计算两点之间的距离
    void mouseMoveRotate(const QPointF & movePos);          //处理旋转
    void mouseCursorShape();        //鼠标的样式
};
