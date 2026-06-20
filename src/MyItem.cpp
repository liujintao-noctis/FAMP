/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: V1.0
 * Description: 曲线绘制 QGraphicsItem 子类 — B 样条曲线渲染
 *****************************************************************/

#include "MyItem.h"

#include <QPainterPath>
#include <QPen>
#include <QColor>
#include <QDebug>
#include <QtMath>

#include <Eigen/Dense>

#include <algorithm>

///**********************************------------------------------------------*******************************//

///**********************************------------------------------------------*******************************//

///**************************               重写QGraphicsItem类                          ***********************//

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
    drawCustomRect(painter);        //绘制出边界框
    mouseCursorShape();             //鼠标的样式
    drawLine(painter);              //绘制连线
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

    QRectF currentBoundary = this->getCustomRect(); //获得外边界框

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

    press_pos = itemPos;        //记录鼠标按压的坐标

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
    std::vector<float>  xVec;
    std::vector<float>  yVec;

    for (size_t i = 0; i < points.size(); i++)
    {
        xVec.push_back(points[i].x());
        yVec.push_back(points[i].y());
    }

    //对容器从大到小排序
    std::sort(xVec.begin(), xVec.end(), [=](float p1, float p2) {return p1 > p2; });
    std::sort(yVec.begin(), yVec.end(), [=](float p1, float p2) {return p1 > p2; });

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
    else if(dotValue <-1.0f)    dotValue = -1.0f;

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
    int pointSize;      //拟合的型值点数
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

    if (closed)         //  闭合
    {
        return fittingPoints;
    }
    else              //不闭合
    {
        //新的控制点点集和拟合点点集
        QVector<QPointF> new_points = controlpoints;
        QVector<QPointF> new_fittingPoints;
        QPointF Ps, Pe;     //在两端添加两个点Ps，Pn
        QPointF P1, P2, P3, P4;     //前两个点和最后两个点
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
