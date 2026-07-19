/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 出图模板 QGraphicsItem 子类
 *****************************************************************/

#include "FormTabulationItem.h"

#include <QPen>
#include <QColor>
#include <QFont>
#include <QDebug>

///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

///**************************               重写QGraphicsItem,出图模板                        ***********************//

///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

FormTabulationItem::FormTabulationItem(QString designer, QString data, QString scale, QWidget *parent)
{
    this->setFlags(QGraphicsItem::ItemIsFocusScope | QGraphicsItem::ItemIsSelectable
        | QGraphicsItem::ItemIsFocusable | QGraphicsItem::ItemIsMovable);

    offset = QPointF(140, 200);     //离原点偏移的x,y量
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
    designerfont.setFamily("Sans Serif");
    designerfont.setPointSizeF(15.0);
    painter->setFont(designerfont);
    painter->drawText(QPointF(2.0, 32.0) + offset, tr("制图人"));

    //绘制比例尺字体
    QFont scalefont;
    scalefont.setFamily("Sans Serif");
    scalefont.setPointSizeF(15.0);
    painter->setFont(scalefont);
    painter->drawText(QPointF(2.0, 82.0) + offset, tr("比例尺"));

    //绘制日期字体
    QFont datafont;
    datafont.setFamily("Sans Serif");
    datafont.setPointSizeF(15.0);
    painter->setFont(datafont);
    painter->drawText(QPointF(12.0, 132.0) + offset, tr("日期"));

}

//添加输入的文字
void FormTabulationItem::drawAddText(QPainter * painter, const QString & text, const QPointF &offset)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    QFont designerfont;
    designerfont.setFamily("Sans Serif");
    designerfont.setPointSizeF(15.0);
    painter->setFont(designerfont);
    painter->drawText(offset, text);
}

void FormTabulationItem::paint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
    drawPlotTab(painter);

    //添加制图人信息
    if (currentDesignerText.length() >= 3)  drawAddText(painter, currentDesignerText, QPointF(160, 32)+offset);
    else drawAddText(painter, currentDesignerText, QPointF(160, 32) + offset);

    //添加比例尺信息
    drawAddText(painter, currentScaleText, QPointF(165, 82) + offset);

    //添加日期信息
    if (currentDataText.length() > 3)   drawAddText(painter, currentDataText, QPointF(110, 132) + offset);
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
