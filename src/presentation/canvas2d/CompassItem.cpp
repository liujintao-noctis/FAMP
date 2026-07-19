/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 指北针 QGraphicsItem 子类
 *****************************************************************/

#include "CompassItem.h"

#include <QPainterPath>
#include <QPen>
#include <QColor>
#include <QPolygonF>
#include <QFont>
#include <QDebug>
#include <QtMath>

///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

///**************************               重写QGraphicsItem,指北针                       ***********************//

///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

CompassItem::CompassItem()
{
    this->setFlags(QGraphicsItem::ItemIsFocusScope | QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    offset = QPointF(280, -340);        //离原点偏移的x,y量
    AABB = QRectF(QPointF(0, 0) + offset, QPointF(44, 104) + offset);
    customRect = AABB.adjusted(-10, -10, 10, 10);
    item_operator = t_none;
    setCursor(Qt::CrossCursor);

};

CompassItem::~CompassItem()
{
}

void CompassItem::paint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
    drawCustomRect(painter);    //绘制出边界框
    drawPicture(painter);       //绘制出compass
}

QRectF CompassItem::getCustomRect(void) const
{
    return  customRect;
}

QRectF CompassItem::boundingRect() const
{
    return customRect.adjusted(-3.0, -3.0, 3.0, 3.0);
}

void CompassItem::customPaint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
}

void CompassItem::mousePressEvent(QGraphicsSceneMouseEvent * event)
{
    //获得鼠标坐标
    itemPos = event->pos();
    scenePos = event->scenePos();

    QRectF currentBoundary = this->getCustomRect(); //获得外边界框

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

    press_pos = itemPos;        //记录鼠标按压的坐标
    mouseCursorShape();
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
    QGraphicsItem::mouseReleaseEvent(event);
    mouseCursorShape();
}

QVariant CompassItem::itemChange(GraphicsItemChange change,
                                 const QVariant& value)
{
    const QVariant result = QGraphicsItem::itemChange(change, value);
    if (change == QGraphicsItem::ItemSelectedHasChanged)
        mouseCursorShape();
    return result;
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
#ifdef Q_OS_WIN
    N_font.setFamily("SimHei");
#else
    N_font.setFamily("WenQuanYi Micro Hei");
#endif
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
    QRectF currentBoundary = this->getCustomRect(); //获得外边界框
    QPointF centerPos = this->boundingRect().center();      //获得外边框的中心坐标
    //QGraphicsItem::setTransformOriginPoint(centerPos);        //设置旋转中心

    //获得夹角向量
    QVector2D startVec = QVector2D(press_pos.x() - centerPos.x(), press_pos.y() - centerPos.y());
    QVector2D endVec = QVector2D(movePos.x() - centerPos.x(), movePos.y() - centerPos.y());

    //单位化
    startVec.normalize();
    endVec.normalize();

    // 单位向量点乘，计算角度
    qreal dotValue = QVector2D::dotProduct(startVec, endVec);
    if (dotValue > 1.0f)    dotValue = 1.0f;
    else if (dotValue < -1.0f)  dotValue = -1.0f;

    qreal dotCosAngle = qAcos(dotValue);    //cos反函数求角度，该角度是弧度值

    qreal angle = dotCosAngle * (180.0f / PI);      //弧度转成角度

     //向量叉乘获取方向
    QVector3D crossValue = QVector3D::crossProduct(QVector3D(startVec, 1.0), QVector3D(endVec, 1.0));

    //根据叉乘结果Z值得正负判断旋转是顺时针还是逆时针
    if (crossValue.z() < 0)     angle = -angle;

    itemRotateAngle += angle;

    // 设置旋转矩阵 — rebuild from absolute angle to avoid floating-point drift
    itemTransform = QTransform().translate(centerPos.x(), centerPos.y()).rotate(itemRotateAngle).translate(-centerPos.x(), -centerPos.y());
    this->setTransform(itemTransform);
    press_pos = movePos;
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

}
