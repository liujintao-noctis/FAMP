/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 出图模板 QGraphicsItem 子类
 *****************************************************************/

#pragma once

#include <QObject>
#include <QGraphicsItem>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

///**************************               重写QGraphicsItem,出图模板                        ***********************//

///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

class FormTabulationItem :public QObject, public QGraphicsItem
{
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)
public:
    FormTabulationItem(QString designer, QString data, QString scale, QWidget *parent);
    ~FormTabulationItem();

private:
    QRectF AABB;        //AABB包围盒
    QRectF customRect;          //最初原始的边界
    QPointF offset;         //离原点偏移的x,y量

    QString currentDataText;
    QString currentDesignerText;
    QString currentScaleText;

    void drawPlotTab(QPainter *painter);    //绘制出图模板样式
    void drawAddText(QPainter *painter,const QString &text,const QPointF &offset);      //添加输入的文字

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

    // 获取自定义绘制所需要的矩形
    QRectF getCustomRect(void) const;

    QRectF boundingRect()const;     //返回的边界

    // 自定义元素绘制
    virtual void customPaint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
};
