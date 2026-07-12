/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 2D 米格纸视图 — DP 简化、B 样条拟合、KNN Alpha Shape
 *****************************************************************/

#include "MyGraphicsView.h"
#include "FileIO.h"
#include "GraphicsExport.h"
#include "GraphicsItemTransform.h"
#include "MainWindow.h"
#include "MetricScale.h"
#include "Version.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QPushButton>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QShowEvent>
#include <QUndoStack>
#include <QWindow>

#include <chrono>
#include <algorithm>
#include <cmath>
#include <stack>
Q_DECLARE_METATYPE(MyOrderCloudType)

namespace
{
bool fuzzyPointCompare(const QPointF &left, const QPointF &right)
{
    return qFuzzyCompare(left.x() + 1.0, right.x() + 1.0)
        && qFuzzyCompare(left.y() + 1.0, right.y() + 1.0);
}

int gridLinePosition(qint64 index)
{
    return static_cast<int>((index % 10 + 10) % 10);
}
}

MyGraphicsView::MyGraphicsView(QWidget *parent)
    : QGraphicsView(parent)
{
    //初始化指针
    project_cloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
    currentItemCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);

    dlgPlotTab = NULL;

    labelScene = new QLabel("Scene坐标");
    labelScene->setMinimumWidth(200);
    labelScene->adjustSize();

    isOverLookProjLine = false; //是否进行俯视XOY点云连线

    this->setCursor(Qt::CrossCursor);   //设置鼠标样式
    this->setMouseTracking(true);       //设置鼠标追踪
    this->setDragMode(QGraphicsView::RubberBandDrag);   //选中鼠标框选内容

    scaleType = OneToFifty;         //比例尺默认1:50
    metricPixelsPerMillimeter = QPointF(
        famp::metric::deviceIndependentPixelsPerMillimeter(
            famp::metric::DefaultDotsPerInch),
        famp::metric::deviceIndependentPixelsPerMillimeter(
            famp::metric::DefaultDotsPerInch));
    deltaOffset = scaleOffsetFor(scaleType);

    scene = new QGraphicsScene(-1500, -1500, 3000, 3000, this);
    history = new QUndoStack(this);
    history->setUndoLimit(100);
    this->setScene(scene);
    connect(scene, &QGraphicsScene::selectionChanged, this, [this]() {
        emit selectionAvailabilityChanged(!scene->selectedItems().isEmpty());
    });

    setMetricScreen(QGuiApplication::primaryScreen());

}

MyGraphicsView::~MyGraphicsView()
{
    mousePressItemStates.clear();
    history->clear();
    itemHandles.clear();
    scene->clear();
}

QUndoStack* MyGraphicsView::commandStack() const
{
    return history;
}

famp::graphics::ItemHandle MyGraphicsView::handleForItem(QGraphicsItem* item)
{
    if (!item)
        return {};

    auto existing = itemHandles.value(item).lock();
    if (existing)
        return existing;

    auto handle = std::make_shared<famp::graphics::ItemLifetime>(item);
    itemHandles.insert(item, handle);
    return handle;
}

QVector<famp::graphics::ItemHandle> MyGraphicsView::handlesForItems(
    const QList<QGraphicsItem*>& items)
{
    QVector<famp::graphics::ItemHandle> handles;
    handles.reserve(items.size());
    for (QGraphicsItem* item : items)
    {
        if (item && std::none_of(handles.cbegin(), handles.cend(),
                                [item](const auto& handle) {
                                    return handle && handle->item == item;
                                }))
        {
            handles.push_back(handleForItem(item));
        }
    }
    return handles;
}

QVector<famp::graphics::ItemState> MyGraphicsView::selectedItemStates()
{
    return famp::graphics::captureItemStates(
        handlesForItems(scene->selectedItems()));
}

void MyGraphicsView::pushTransformChange(
    const QVector<famp::graphics::ItemState>& before,
    const QString& text)
{
    QVector<famp::graphics::ItemHandle> handles;
    handles.reserve(before.size());
    for (const auto& state : before)
        handles.push_back(state.handle);

    const auto after = famp::graphics::captureItemStates(handles);
    if (!famp::graphics::itemStatesEqual(before, after))
    {
        history->push(famp::graphics::makeTransformCommand(
            before, after, text));
    }
}

void MyGraphicsView::addItemWithHistory(QGraphicsItem* item,
                                        const QString& text)
{
    if (!item)
        return;
    history->push(famp::graphics::makeAddItemCommand(
        scene, handleForItem(item), text));
}

void MyGraphicsView::invalidateHistory(const QString& reason)
{
    mousePressItemStates.clear();
    if (history->count() == 0)
        return;
    history->clear();
    emit sendStrFromGraphicView2Console(
        QStringLiteral("因%1已重置撤销历史。").arg(reason));
}

void MyGraphicsView::clearSceneAndHistory()
{
    mousePressItemStates.clear();
    history->clear();
    itemHandles.clear();
    scene->clear();
}

void MyGraphicsView::moveSelectedItemsBy(const QPointF& delta,
                                         const QString& text)
{
    const auto before = selectedItemStates();
    for (const auto& state : before)
    {
        if (state.handle && state.handle->item)
            state.handle->item->moveBy(delta.x(), delta.y());
    }
    pushTransformChange(before, text);
}

void MyGraphicsView::getDBItemCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud)
{
    this->currentItemCloud = Cloud;
}

//判断投影的点云是投影到哪个面
ProjectType MyGraphicsView::projectType(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& Cloud)
{
    ProjectType str;
    std::vector<float> vecX;
    std::vector<float> vecY;
    std::vector<float> vecZ;
    if (Cloud->size() < 10)  return NONE;   //切割投影的点云数过少，无法连线！

    //选取10个点的索引进行检测判断
    std::vector<int> pointindexs;
    pointindexs.reserve(10);

    int stripe = Cloud->size() / 10;
    for (size_t i = 0; i < 10; i++)
    {
        int index = stripe * i;
        //qDebug() << i << "    " << index << endl;
        pointindexs.push_back(index);
    }

    vecX.reserve(pointindexs.size());
    vecY.reserve(pointindexs.size());
    vecZ.reserve(pointindexs.size());
    for (int i = 0; i < pointindexs.size(); i++)
    {
        //qDebug() << i;
        vecX.push_back(Cloud->points[pointindexs[i]].x);
        vecY.push_back(Cloud->points[pointindexs[i]].y);
        vecZ.push_back(Cloud->points[pointindexs[i]].z);
    }

    for (size_t i = 0; i < 10; ++i)
    {
        // 使用容差比较替代精确浮点比较，避免投影后的微小误差导致误判
        const float eps = 1e-6f;
        if (std::fabs(vecX[i] - vecX[0]) > eps)
        {
            if (std::fabs(vecY[i] - vecY[0]) > eps)
            {
                if (i == 9 && isOverLookProjLine) { str = OLXOY; }
                else if (i == 9 && !isOverLookProjLine) { str = XOY; }
            }
            else
            {
                if (i == 9) { str = XOZ; }
            }
        }
        else
        {
            if (i == 9) { str = YOZ; }
        }
    }

    return str;
}

//是否绘制切割线对话框
int MyGraphicsView::dlgDrawXOYLine()
{
    QString dlgTitle = "XOY面投影线绘制";
    QString strInfo = "是否生成剖面切割线？";

    emit sendDlgClipVisible(false);     //将VTK的平面裁剪对话框隐藏
    QMessageBox msg;
    msg.setText(strInfo);
    msg.setWindowTitle(dlgTitle);
    msg.setStandardButtons(QMessageBox::Yes | QMessageBox::No| QMessageBox::Cancel);
    msg.setDefaultButton(QMessageBox::Cancel);
    int result = msg.exec();

    switch (result)
    {
    case(QMessageBox::Yes):
    {
        return 1;       //绘制剖面线
    }
    break;

    case(QMessageBox::No):
    {
        return 0;   //不绘制剖面线
    }
    break;

    case(QMessageBox::Cancel):
    {
        return -1;          //取消
    }
    break;

    default:
        break;
    }
    return -1;      // 默认取消
}

//计算点云的平均密度（点与点之间的平均距离）
double MyGraphicsView::computeCloudMeanDis(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud)
{
    double meanDis = 0.0;   //平均距离
    int numberOfPoints = 0;     //有效的点云数
    std::vector<int> indices;
    std::vector<float> squareDistances;

    pcl::search::KdTree< pcl::PointXYZRGB> kdtree;
    kdtree.setInputCloud(cloud);

    for (size_t i = 0; i < cloud->size(); i++)
    {
        if (!pcl::isFinite(cloud->points[i]))       continue;       //检查该值是否为正常值（有限）

        if (kdtree.nearestKSearch(i, 2, indices, squareDistances) > 0)
        {
            meanDis += sqrt(squareDistances[1]);
            ++numberOfPoints;
        }
    }

    if (numberOfPoints != 0)
    {
        meanDis /= numberOfPoints;
    }
    return meanDis;
}

//画出XOZ面的投影
//统一的投影绘制方法，消除4个draw方法的代码重复
void MyGraphicsView::drawProjection(const DrawConfig& config, QPointF offset)
{
    //若需要体素滤波降采样（俯视XOY投影专用）
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloudToOrder = project_cloud;
    if (config.useVoxelDownsample)
    {
        float leaf = 0.01;
        while (cloudToOrder->size() > 10000)
        {
            pcl::VoxelGrid<pcl::PointXYZRGB> vog;
            vog.setInputCloud(cloudToOrder);
            vog.setLeafSize(leaf, leaf, leaf);
            vog.filter(*cloudToOrder);
            //不再在循环内写磁盘 — 见Task B
            leaf += 0.01;
        }

        //计算投影点云的平均密度
        double densityOL = computeCloudMeanDis(cloudToOrder);

        //KNNAlphaShape提取边界
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr boundarypoints(new pcl::PointCloud<pcl::PointXYZRGB>);
        KNNAlphaShape(cloudToOrder, densityOL*2.5, densityOL*2.5, boundarypoints);  //以平均密度的2.0-3.0倍作为Alpha半径和KNN搜索半径

        if (boundarypoints->size() == 0)
            QMessageBox::warning(this, "俯视投影到XOY面", "提取边界轮廓的点为0！");

        cloudToOrder = boundarypoints;
    }

    //计算投影点云的平均密度
    double density = computeCloudMeanDis(cloudToOrder);

    //将投影/边界的点云转为有序点云
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr orderCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    orderSoetCloud(cloudToOrder, std::min(static_cast<int>(cloudToOrder->size() - 1), 30), orderCloud);
#ifdef FAMP_DEBUG
    // 调试：保存有序点云
    // pcl::io::savePCDFileASCII(config.debugOrderFile, *orderCloud);
#endif

    //将有序化的点云用DP进行简化
    std::vector<DPPoint> points_DP;
    for (size_t i = 0; i < orderCloud->size(); i++)
    {
        DPPoint point;
        point.x = orderCloud->points[i].x;
        point.y = orderCloud->points[i].y;
        point.z = orderCloud->points[i].z;
        point.ID = i;

        points_DP.push_back(point);
    }

    computeDP(points_DP, 0, points_DP.size() - 1, density);     //DP简化

    //将DP点转为PCL点云
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_DP(new pcl::PointCloud<pcl::PointXYZRGB>);
    for (size_t i = 0; i < points_DP.size(); i++)
    {
        if (points_DP[i].isRemoved == true) continue;

        pcl::PointXYZRGB point;
        point.x = points_DP.at(i).x;
        point.y = points_DP.at(i).y;
        point.z = points_DP.at(i).z;

        cloud_DP->push_back(point);
    }

#ifdef FAMP_DEBUG
    if (!cloud_DP->empty())
        // 调试：保存 DP 简化结果
        // pcl::io::savePCDFileASCII(config.debugDPFile, *cloud_DP);
#endif
    qDebug() << "原始点数:" << orderCloud->size() << "\t" << "DP简化后点云数：" << "\t" << cloud_DP->size();

    //将点云转为QT点集（XOZ面使用DP前的有序点云，其余面使用DP简化后的点云）
    QVector<QPointF> qtPoints;
    PCLCloud2QTPoints(config.projType == XOZ ? orderCloud : cloud_DP, config.projType, qtPoints, QPointF(offset.x(), offset.y()));

    //将点云传入到重写的QGraphicsItem中绘制
    MyItem *item = new MyItem(qtPoints, config.projType);
    MyOrderCloudType myordercloud;
    myordercloud.orderCloud = cloud_DP;
    myordercloud.project_type = config.projType;
    item->setData(ItemName, config.labelText);
    item->setData(ItemCloud, QVariant::fromValue(myordercloud));
    addItemWithHistory(item, tr("添加投影轮廓"));
    scene->clearSelection();
    item->setSelected(true);
    emit sendStrFromGraphicView2Console(QString(config.consoleMsg));
}

void MyGraphicsView::drawXOZ(QPointF offset)
{
    DrawConfig cfg = { XOZ, "XOZ面投影", false, false, "ordercloud_xoz.pcd", "cloud_DP_XOZ.pcd", "已生成XOZ面投影连线！" };
    drawProjection(cfg, offset);
}

//画出YOZ面的投影
void MyGraphicsView::drawYOZ(QPointF offset)
{
    DrawConfig cfg = { YOZ, "YOZ面投影", false, false, "ordercloud_yoz.pcd", "cloud_DP_YOZ.pcd", "已生成YOZ面投影连线！" };
    drawProjection(cfg, offset);
}

//画出XOY面的投影
void MyGraphicsView::drawXOY(QPointF offset)
{
    DrawConfig cfg = { XOY, "XOY面投影", false, false, "ordercloud_xoy.pcd", "cloud_DP_XOY.pcd", "已生成XOY面投影连线！" };
    drawProjection(cfg, offset);
}

//画出俯视XOY面的投影
void MyGraphicsView::drawOverLookXOY(QPointF offset)
{
    DrawConfig cfg = { OLXOY, "俯视投影", true, true, "ordercloud_olxoy.pcd", "cloud_DP_OLXOY.pcd", "已生成俯视投影面投影连线！" };
    drawProjection(cfg, offset);
}

//画出XOY面的剖线
void MyGraphicsView::drawXOYLine(QPointF offset)
{
    //计算投影点云的平均密度
    double density = computeCloudMeanDis(project_cloud);
    //qDebug() << "density" << density;

    //将提取的无序点云有序化
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr orderCloudPointsXOYLine(new pcl::PointCloud<pcl::PointXYZRGB>);
    orderSoetCloud(project_cloud, std::min(static_cast<int>(project_cloud->size() - 1), 30), orderCloudPointsXOYLine);
    // 调试：保存 XOY 剖线有序点云
    // pcl::io::savePCDFileASCII("ordercloud_xoyLine.pcd", *orderCloudPointsXOYLine);

    //提取第一个点和最后一个点
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr XOYLineCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    XOYLineCloud->push_back(orderCloudPointsXOYLine->front());
    XOYLineCloud->push_back(orderCloudPointsXOYLine->back());

    //将有序点云转为QT点集
    QVector<QPointF> points_xoy;
    PCLCloud2QTPoints(XOYLineCloud, XOY, points_xoy, QPointF(offset.x(), offset.y()));

    //计算两个点的直线向量
    QVector2D line_dir = QVector2D(points_xoy.back().x() - points_xoy.front().x(), points_xoy.back().y() - points_xoy.front().y());
    line_dir.normalize();

    QPointF P1, P2, P3, P4;
    P2 = QPointF(points_xoy.front().x(), points_xoy.front().y());
    P3 = QPointF(points_xoy.back().x(), points_xoy.back().y());
    P1 = QPointF(P2.x() - line_dir.x() * 20, P2.y() - line_dir.y() * 20);
    P4 = QPointF(P3.x() + line_dir.x() * 20, P3.y() + line_dir.y() * 20);

    points_xoy.clear();
    points_xoy.push_back(P1);
    points_xoy.push_back(P2);
    points_xoy.push_back(P3);
    points_xoy.push_back(P4);

    //将点云传入到重写的QGraphicsItem中绘制
    MyItem *itemXOYLine = new MyItem(points_xoy, XOY);
    MyOrderCloudType xoyline_myordercloud;
    xoyline_myordercloud.orderCloud = XOYLineCloud;
    xoyline_myordercloud.project_type = XOY;
    itemXOYLine->setData(ItemName, "剖面线");
    itemXOYLine->setData(ItemCloud, QVariant::fromValue(xoyline_myordercloud));
    addItemWithHistory(itemXOYLine, tr("添加剖面线"));
    scene->clearSelection();
    itemXOYLine->setSelected(true);
    emit sendStrFromGraphicView2Console(QString::asprintf("已生成剖面连线！"));

}

/*
KNNAlphaShape提取边界
\* 输入点云
\*输入滚球法Alpha半径
\*输入KNN搜索半径
\*输出提取的点云
*/
void MyGraphicsView::KNNAlphaShape(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud, float Alpha, float neborRadius, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &outcloud)
{

    std::vector<QVector2D> qt_points;       //将PCL转为qt点集
    std::vector<int> KNNBoundaryPointIndex;     //KNNAlphaShape提取边界点的索引
    QVector<QVector2D> knn_boundaryPoints;      //提取的边界轮廓二维点

    //kd快速临近点搜索
    pcl::KdTreeFLANN<pcl::PointXYZRGB>  kdtree;
    kdtree.setInputCloud(cloud);

    //容器储存了每个点邻近点的索引
    std::vector<std::vector<int>> each_point_nebot;

    std::vector<int> nebor_index;
    std::vector<float> nebor_dis;

    each_point_nebot.resize(cloud->points.size(), nebor_index);

    //容器each_point_nebot获得每个点邻近点索引的容器
    for (size_t i = 0; i < cloud->points.size(); i++)
    {
        if (kdtree.radiusSearch(cloud->points[i], neborRadius, nebor_index, nebor_dis) > 0)
        {
            each_point_nebot[i].swap(nebor_index);      //该容器第一个索引为自身
        }
    }

    //将点云的点集转为Qvector2D
    for (size_t i = 0; i < cloud->size(); i++)
    {
        QVector2D point;
        point.setX(cloud->points[i].x);
        point.setY(cloud->points[i].y);

        qt_points.push_back(point);
    }

    //判断该点是否处理过函数
    std::vector<bool> process;
    process.resize(cloud->size(), false);

    for (size_t i = 0; i < qt_points.size(); i++)
    {

        //从该点的邻近点开始
        for (size_t k = 1; k < each_point_nebot[i].size(); k++)
        {

            //判断该点是否计算过
            if (process[each_point_nebot[i][k]] == true)    continue;
            //process[each_point_nebot[i][k]] = true;

            // 跳过距离大于直径的情况
            if (qt_points[i].distanceToPoint(qt_points[each_point_nebot[i][k]]) > 2 * Alpha)        continue;

            // 两个圆心
            QVector2D c1, c2;

            // 线段中点
            QVector2D center = 0.5*(qt_points[i] + qt_points[each_point_nebot[i][k]]);

            // 方向向量 P1P2 = (x,y)
            QVector2D dir = qt_points[i] - qt_points[each_point_nebot[i][k]];

            // 垂直向量 n = (a,b)  a*dir.x+b*dir.y = 0; a = -(b*dir.y/dir.x)
            QVector2D normal;
            normal.setY(5);         // 因为未知数有两个，随便给y附一个值5。

            if (dir.x() != 0)
            {
                normal.setX(-(normal.y()*dir.y()) / dir.x());
            }
            else
            {
                // 如果方向平行于y轴
                normal.setX(1);
                normal.setY(0);
            }

            normal.normalize();   // 法向量单位化

            //获得圆心到P1P2两点连线的距离
            float len = sqrt(Alpha*Alpha - (0.25*dir.length()*dir.length()));

            //两个圆心的坐标
            c1 = center + len * normal;
            c2 = center - len * normal;

            // b1、b2记录是否在圆C1、C2中找到其他点。
            bool b1 = false, b2 = false;
            for (size_t m = 0; m < qt_points.size(); m++)
            {
                if (m == i || m == each_point_nebot[i][k])  continue;

                if (b1 != true && qt_points[m].distanceToPoint(c1) < Alpha) b1 = true;
                if (b2 != true && qt_points[m].distanceToPoint(c2) < Alpha) b2 = true;

                // 如果都有内部点，不必再继续检查了
                if (b1 == true && b2 == true)   break;
            }

            if (b1 != true || b2 != true)
            {
                //将边界点的索引存入容器
                KNNBoundaryPointIndex.push_back(i);
                KNNBoundaryPointIndex.push_back(each_point_nebot[i][k]);
            }
        }

        process[i] = true;
    }

    //将重复的点索引删除
    std::sort(KNNBoundaryPointIndex.begin(), KNNBoundaryPointIndex.end());
    KNNBoundaryPointIndex.erase(std::unique(KNNBoundaryPointIndex.begin(), KNNBoundaryPointIndex.end()), KNNBoundaryPointIndex.end());

    pcl::PointIndices indices;
    indices.indices.swap(KNNBoundaryPointIndex);
    pcl::copyPointCloud(*cloud, indices, *outcloud);
    //qDebug() << alphaShapeCloud->size();

    if (outcloud->size() != 0)
    {
        for (size_t i = 0; i < outcloud->size(); i++)
        {
            QVector2D point;
            point.setX(outcloud->points[i].x);
            point.setY(outcloud->points[i].y);

            knn_boundaryPoints.push_back(point);
        }
    }

    // 调试：保存 KNN Alpha Shape 提取边界
    // if(outcloud->size() !=0)      pcl::io::savePCDFileASCII("KNNAlphaShape.pcd", *outcloud);
}

//寻找点云中最远的两点的索引
// 2D point with original cloud index for convex hull computation
struct HullPoint {
    double x, y;
    int originalIndex;
};

// Cross product of vectors OA and OB (O is origin). Returns positive if O->A->B is counter-clockwise.
static inline double cross2D(const HullPoint &a, const HullPoint &b, const HullPoint &c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Squared 2D Euclidean distance (avoids sqrt for comparisons)
static inline double distSq2D(const HullPoint &a, const HullPoint &b)
{
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

/**
 * Find the two farthest-apart points in a 2D point cloud.
 *
 * Algorithm: Andrew's monotone chain convex hull (O(n log n)) followed by
 * rotating calipers (O(h)) on the hull. Total: O(n log n), where h ≤ n is
 * the hull size. For clouds with < 3 points, falls back to trivial brute-force.
 *
 * @param incloud           Input PCL point cloud (x,y,z,rgb)
 * @param headendPointIndex Output: indices of the two farthest points,
 *                          with headendPointIndex[0] ≤ headendPointIndex[1]
 */
void MyGraphicsView::findMaxDistancePointsofCloud(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & incloud, int headendPointIndex[2])
{
    size_t n = incloud->size();

    // ---------- Edge cases: 0, 1, or 2 points ----------
    if (n == 0) {
        headendPointIndex[0] = 0;
        headendPointIndex[1] = 0;
        return;
    }
    if (n == 1) {
        headendPointIndex[0] = 0;
        headendPointIndex[1] = 0;
        return;
    }
    if (n == 2) {
        headendPointIndex[0] = 0;
        headendPointIndex[1] = 1;
        return;
    }

    // ---------- Step 1: Build 2D point array with original indices ----------
    std::vector<HullPoint> pts(n);
    for (size_t i = 0; i < n; i++) {
        pts[i].x = incloud->points[i].x;
        pts[i].y = incloud->points[i].y;
        pts[i].originalIndex = static_cast<int>(i);
    }

    // ---------- Step 2: Andrew's monotone chain convex hull ----------
    // Sort by x, then y
    std::sort(pts.begin(), pts.end(), [](const HullPoint &a, const HullPoint &b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });

    // Build lower hull
    std::vector<HullPoint> hull;
    for (size_t i = 0; i < n; i++) {
        while (hull.size() >= 2 && cross2D(hull[hull.size() - 2], hull[hull.size() - 1], pts[i]) <= 0)
            hull.pop_back();
        hull.push_back(pts[i]);
    }

    // Build upper hull
    size_t lowerSize = hull.size();
    for (size_t i = n; i-- > 0; ) {
        while (hull.size() > lowerSize && cross2D(hull[hull.size() - 2], hull[hull.size() - 1], pts[i]) <= 0)
            hull.pop_back();
        hull.push_back(pts[i]);
    }

    // Remove duplicate last point (same as first)
    hull.pop_back();

    // If hull degenerated to a line segment (all points collinear), just pick the two extremes
    if (hull.size() < 2) {
        headendPointIndex[0] = pts[0].originalIndex;
        headendPointIndex[1] = pts[n - 1].originalIndex;
        if (headendPointIndex[0] > headendPointIndex[1])
            std::swap(headendPointIndex[0], headendPointIndex[1]);
        return;
    }

    // ---------- Step 3: Rotating calipers ----------
    int h = static_cast<int>(hull.size());
    int bestI = 0, bestJ = 0;
    double bestDistSq = 0.0;

    // Find initial antipodal point (farthest from hull[0])
    int j = 1;
    while (cross2D(hull[0], hull[1], hull[(j + 1) % h]) > cross2D(hull[0], hull[1], hull[j]))
        j = (j + 1) % h;

    // Rotate through all antipodal pairs
    for (int i = 0; i < h; i++) {
        int nextI = (i + 1) % h;
        // Advance j while the next hull edge would produce a larger cross product (area)
        while (cross2D(hull[i], hull[nextI], hull[(j + 1) % h]) > cross2D(hull[i], hull[nextI], hull[j]))
            j = (j + 1) % h;

        // Check distance from hull[i] to hull[j]
        double dSq = distSq2D(hull[i], hull[j]);
        if (dSq > bestDistSq) {
            bestDistSq = dSq;
            bestI = i;
            bestJ = j;
        }

        // Also check distance from hull[nextI] to hull[j]
        dSq = distSq2D(hull[nextI], hull[j]);
        if (dSq > bestDistSq) {
            bestDistSq = dSq;
            bestI = nextI;
            bestJ = j;
        }
    }

    // ---------- Step 4: Map hull indices back to original cloud indices ----------
    headendPointIndex[0] = hull[bestI].originalIndex;
    headendPointIndex[1] = hull[bestJ].originalIndex;
    if (headendPointIndex[0] > headendPointIndex[1])
        std::swap(headendPointIndex[0], headendPointIndex[1]);
}

/*
//将无序点云进行有序排列
*\输入无序点云
*\输入最近邻搜索K
*\输出有序点云
*/
void MyGraphicsView::orderSoetCloud(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & incloud, int neborNumbers, pcl::PointCloud<pcl::PointXYZRGB>::Ptr & outcloud)
{
    //每个点是否遍历过容器
    std::vector<bool> process;
    process.resize(incloud->size(), false);

    //kd快速临近点搜索
    pcl::KdTreeFLANN<pcl::PointXYZRGB>  kdtree;
    kdtree.setInputCloud(incloud);

    //容器储存了每个点邻近点的索引
    std::vector<std::vector<int>> each_point_nebot;

    std::vector<int> nebor_index;
    std::vector<float> nebor_dis;

    each_point_nebot.resize(incloud->points.size(), nebor_index);

    //容器each_point_nebot获得每个点邻近点索引的容器
    for (size_t i = 0; i < incloud->size(); i++)
    {
        if (kdtree.nearestKSearch(incloud->points[i], neborNumbers, nebor_index, nebor_dis) > 0)
        {
            each_point_nebot[i].swap(nebor_index);
        }
    }

    pcl::PointIndices pointIndex;
    std::queue<int> seed;        //种子点，以该点开始进行生长

    //寻找到端点的索引
    int headendPointIndex[2];
    findMaxDistancePointsofCloud(incloud, headendPointIndex);
    qDebug() << "headendPointIndex" << headendPointIndex[0] << headendPointIndex[1];

    seed.push(headendPointIndex[0]);
    pointIndex.indices.push_back(headendPointIndex[0]);
    process[headendPointIndex[0]] = true;

    while (!seed.empty())
    {
        int current_seed_index = seed.front();
        for (size_t i = 1; i < each_point_nebot[current_seed_index].size(); i++)
        {
            if (process[each_point_nebot[current_seed_index][i]] == true)   continue;
            seed.push(each_point_nebot[current_seed_index][i]);
            pointIndex.indices.push_back(each_point_nebot[current_seed_index][i]);
            process[each_point_nebot[current_seed_index][i]] = true;
            break;
        }
        seed.pop();
    }
    qDebug() << pointIndex.indices.size();
    for (size_t i = 0; i < pointIndex.indices.size(); i++)
    {
        outcloud->push_back(incloud->points[pointIndex.indices[i]]);
    }

    // 调试：保存有序排列点云
    // pcl::io::savePCDFileASCII("ordercloud.pcd", *outcloud);
}

//根据点云和投影类型将PCL按照不同比例尺转为QT点集
void MyGraphicsView::PCLCloud2QTPoints(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& incloud, ProjectType protype, QVector<QPointF> &points, QPointF offset)
{
    //根据传入的点云和投影类型转为QT点
    //比例尺 默认 1:50
    if (!incloud)
    {
        return;
    }
    points.reserve(points.size() + static_cast<int>(incloud->size()));

    switch (protype)
    {
    case(XOY):
    {
        for (size_t i = 0; i < incloud->size(); i++)
        {
            QPointF point;
            point.setX(incloud->at(i).y*offset.x());
            point.setY(incloud->at(i).x*offset.y());
            points.push_back(point);
        }
    }
    break;

    case(XOZ):
    {
        for (size_t i = 0; i < incloud->size(); i++)
        {
            QPointF point;
            point.setX(incloud->at(i).z*offset.x() + YMaxMin * offset.x());
            point.setY(incloud->at(i).x*offset.y());
            points.push_back(point);
        }
    }
    break;

    case(YOZ):
    {
        for (size_t i = 0; i < incloud->size(); i++)
        {
            QPointF point;
            point.setX(incloud->at(i).y*offset.x());
            point.setY(-incloud->at(i).z*offset.y() - XMaxMin * offset.y() / 1.2);
            points.push_back(point);
        }
    }
    break;

    case(OLXOY):
    {
        for (size_t i = 0; i < incloud->size(); i++)
        {
            QPointF point;
            point.setX(incloud->at(i).y*offset.x());
            point.setY(incloud->at(i).x*offset.y());
            points.push_back(point);
        }
    }
    break;

    default:
        break;
    }

}

//比例尺改变后重新绘制
void MyGraphicsView::ReDraw(QPointF offset)
{
    invalidateHistory(tr("比例尺重绘"));
    QList<QGraphicsItem*> all_items = scene->items();

    qDebug()<< "items" << all_items.size();

    for (size_t i = 0; i < all_items.size(); i++)
    {
        if (all_items.at(i)->data(ItemCloud).isValid())
        {
            //获得scene中存在有效的item的有序点云和投影类型
            MyOrderCloudType item_ordercloud = all_items.at(i)->data(ItemCloud).value<MyOrderCloudType>();

            //将有序点云按照比例尺转为QT点集
            QVector<QPointF> item_points;
            PCLCloud2QTPoints(item_ordercloud.orderCloud, item_ordercloud.project_type, item_points, QPointF(offset.x(), offset.y()));

            //剖面线绘制
            if (item_points.size() < 4)
            {
                //计算两个点的直线向量
                QVector2D line_dir = QVector2D(item_points.back().x() - item_points.front().x(), item_points.back().y() - item_points.front().y());
                line_dir.normalize();

                QPointF P1, P2, P3, P4;
                P2 = QPointF(item_points.front().x(), item_points.front().y());
                P3 = QPointF(item_points.back().x(), item_points.back().y());
                P1 = QPointF(P2.x() - line_dir.x() * 20, P2.y() - line_dir.y() * 20);
                P4 = QPointF(P3.x() + line_dir.x() * 20, P3.y() + line_dir.y() * 20);

                item_points.clear();
                item_points.push_back(P1);
                item_points.push_back(P2);
                item_points.push_back(P3);
                item_points.push_back(P4);
            }

            //将点云传入到重写的QGraphicsItem中绘制
            MyItem *item = new MyItem(item_points, item_ordercloud.project_type);
            scene->addItem(item);
            scene->clearSelection();
            scene->selectedItems().clear();
            item->setSelected(true);
            MyOrderCloudType item_myordercloud;
            item_myordercloud.orderCloud = item_ordercloud.orderCloud;
            item_myordercloud.project_type = item_ordercloud.project_type;
            item->setData(ItemCloud, QVariant::fromValue(item_myordercloud));

            //将当前item删除
            scene->removeItem(all_items.at(i));
            delete all_items.at(i);
        }
    }

}

//弹出出图模板对话框
void MyGraphicsView::setDlgPlotTab()
{
    dlgPlotTab = new QDlgPlotTab(this);
    dlgPlotTab->setAttribute(Qt::WA_DeleteOnClose);
    Qt::WindowFlags flags = dlgPlotTab->windowFlags();
    dlgPlotTab->setWindowFlags(flags | Qt::WindowStaysOnTopHint);
    dlgPlotTab->show();
    //获得制图人，比例尺，日期文字
    //designerText = dlgPlotTab->getDesignerPTE();
    //scaleText = dlgPlotTab->getScalePTE();
    //dataText = dlgPlotTab->getDataPTE();
}

//绘制出图模板表格
void MyGraphicsView::drawFormTable()
{
    FormTabulationItem * item = new FormTabulationItem(designerText, dataText, scaleText, this);
    addItemWithHistory(item, tr("添加制图信息"));
    scene->clearSelection();

    item->setSelected(true);
    emit sendStrFromGraphicView2Console("已添加制图信息！");
}

//获得制图人，比例尺，日期文字
void MyGraphicsView::getText()
{
    designerText = dlgPlotTab->getDesignerPTE();
    scaleText = dlgPlotTab->getScalePTE();
    dataText = dlgPlotTab->getDataPTE();
}

//设置出图表对话框为空指针
void MyGraphicsView::setDlgPlotTabNull()
{
    this->dlgPlotTab = NULL;
}

//计算点到线的距离
double MyGraphicsView::point2LineDist(const DPPoint& p1, const DPPoint& p2, const DPPoint& p3)
{
    double dist;
    Eigen::Vector4f line_dir = { p1.x - p2.x,p1.y - p2.y,p1.z - p2.z ,0.0 };
    Eigen::Vector4f line_pt = { p1.x,p1.y,p1.z,0.0 };
    Eigen::Vector4f point3 = { p3.x,p3.y,p3.z,0.0 };
    dist = pcl::sqrPointToLineDistance(point3, line_pt, line_dir);              //PCL点到线的距离

    return sqrt(dist);
}

//计算点集中的最大值及其索引
std::pair<double, int> MyGraphicsView::getMaxDistAndIndex(std::vector<DPPoint>& Points, int begin, int end)
{
    double maxDistance = 0.0;
    int maxIndex = begin;
    for (int i = begin; i <= end; i++)
    {
        double dis = point2LineDist(Points[begin], Points[end], Points[i]);
        if (dis > maxDistance)
        {
            maxDistance = dis;
            maxIndex = i;
        }
    }
    return std::make_pair(maxDistance, maxIndex);
}

/*//DP算法简化线条点
\*输入点集，计算后获得DP简化后的点集
\*输入起始点的索引
\*输入终点的索引
\*输入阈值D
*/
void MyGraphicsView::computeDP(std::vector<DPPoint>& Points, int begin, int end, double threshold)
{
    std::stack<std::pair<int, int>> stk;
    stk.push({begin, end});
    while (!stk.empty()) {
        auto [b, e] = stk.top(); stk.pop();
        if (e - b <= 1) continue;
        auto maxDistAndIdx = getMaxDistAndIndex(Points, b, e);
        if (maxDistAndIdx.first > threshold)
        {
            int mid = maxDistAndIdx.second;
            stk.push({b, mid});
            stk.push({mid, e});
        }
        else
        {
            for (int i = b + 1; i < e; i++)
            {
                Points[i].isRemoved = true;
            }
        }
    }
}

void MyGraphicsView::slotOn_actPoints_triggered()
{
    QGraphicsEllipseItem   *item = new QGraphicsEllipseItem(-50, -30, 100, 60);
    item->setFlags(QGraphicsItem::ItemIsFocusable | QGraphicsItem::ItemIsMovable |
        QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusScope);
    item->setBrush(QBrush(Qt::blue)); //填充颜色
    addItemWithHistory(item, tr("添加图元"));
    scene->clearSelection();
    item->setSelected(true);
}

//切割的点云投影连线
void MyGraphicsView::slotOn_actProjLine_triggered()
{
    qDebug() << XMaxMin << YMaxMin << ZMaxMin;
    /*if (project_type == XOY)
    {
        qDebug() << "XOY";
    }
    else if (project_type == XOZ)
    {
        qDebug() << "XOZ";
        drawXOZ();
    }
    else if (project_type == YOZ)
    {
        qDebug() << "YOZ";
        drawYOZ();
    }
    else if (project_type == OLXOY)
    {
        qDebug() << "OLXOY";
        isOverLookProjLine = false;
    }
    else if (project_type == NONE)
    {
    }*/

    switch (project_type)
    {
    case(XOY):      //XOY面投影连线
    {
        qDebug() << "XOY";
        int isdrawLine = dlgDrawXOYLine();      //是否绘制剖面线
        if (isdrawLine == 1)
        {
            //qDebug() << "绘制剖面线";
            getScaleOffset(this->deltaOffset);
            drawXOYLine(deltaOffset);
            emit sendDlgClipVisible(true);      //将VTK的平面裁剪对话框显示
            emit sendStrFromGraphicView2Console("已绘制剖面线！");
        }
        else if(isdrawLine == 0)
        {
            //qDebug() << "不绘制剖面线";
            getScaleOffset(this->deltaOffset);
            drawXOY(deltaOffset);
            emit sendDlgClipVisible(true);
            emit sendStrFromGraphicView2Console("投影到XOY面的点连线绘制完成！");
        }
        else if (isdrawLine == -1)
        {
            qDebug() << "取消";
            emit sendDlgClipVisible(true);
            return;
        }
    }
    break;

    case(XOZ):      //XOZ面投影连线
    {
        qDebug() << "XOZ";
        getScaleOffset(this->deltaOffset);
        drawXOZ(deltaOffset);
    }
    break;

    case(YOZ):      //YOZ面投影连线
    {
        qDebug() << "YOZ";
        getScaleOffset(this->deltaOffset);
        drawYOZ(deltaOffset);
    }
    break;

    case(OLXOY):    //俯视XOY面投影连线
    {
        qDebug() << "OLXOY";
        auto start = std::chrono::steady_clock::now();
        getScaleOffset(this->deltaOffset);
        drawOverLookXOY(deltaOffset);
        auto end = std::chrono::steady_clock::now();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
        emit sendStrFromGraphicView2Console(QString::asprintf("俯视投影到XOY面的点连线绘制完成！用时%ld秒", secs));
        isOverLookProjLine = false;
    }
    break;

    case(NONE):     //点云数太少,不能连线
    {
        qDebug() << "NONE";
        QMessageBox::warning(this, "投影连线", "点云数太少(<10)！");
    }
    default:
        break;
    }
}

//从VTK接受投影后的点云
void MyGraphicsView::getProjCloudFromVTK(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud)
{
    pcl::copyPointCloud(*incloud, *this->project_cloud);
    //this->project_cloud = incloud;

     project_type = projectType(this->project_cloud);
     cloudDensity =  computeCloudMeanDis(this->project_cloud);      //计算点云的密度
    //qDebug() << project_type;
     //qDebug() << "incloud" << incloud->size();
     //qDebug() << "this->project_cloud" << this->project_cloud->size();
    //qDebug() << "density" << cloudDensity;
}

//删除Item
void MyGraphicsView::slotOn_actDeleteItem_triggered()
{
    const auto handles = handlesForItems(scene->selectedItems());
    if (!handles.isEmpty())
        history->push(famp::graphics::makeRemoveItemsCommand(
            scene, handles, tr("删除图元")));
}

//清空
void MyGraphicsView::slotOn_actClearScene_triggered()
{
    const auto handles = handlesForItems(scene->items());
    if (!handles.isEmpty())
        history->push(famp::graphics::makeRemoveItemsCommand(
            scene, handles, tr("清空画布")));
}

void MyGraphicsView::getAABBMaxFromVTK(float x, float y, float z)
{
    this->XMaxMin = x;
    this->YMaxMin = y;
    this->ZMaxMin = z;
}

//接受是否进行俯视XOY连线
void MyGraphicsView::getisOverLookProj(bool isproj)
{
    this->isOverLookProjLine = isproj;
}

//组合按钮
void MyGraphicsView::slotOn_actGroup_triggered()
{
    int selectedCounts = this->scene->selectedItems().count();  //Scene中选中的个数
    //qDebug() << "selectedCounts" << selectedCounts;

    if (selectedCounts>1)
    {
        invalidateHistory(tr("图元组合"));
        QGraphicsItemGroup *group = new QGraphicsItemGroup;         //创建组合
        scene->addItem(group);   //组合添加到场景中

        for (size_t i = 0; i < selectedCounts; i++)
        {

            QGraphicsItem* item = scene->selectedItems().at(0);
            item->setSelected(false); //清除选择虚线框
            item->clearFocus();
            group->addToGroup(item); //添加到组合
        }

        group->setFlags(QGraphicsItem::ItemIsMovable
            | QGraphicsItem::ItemIsSelectable
            | QGraphicsItem::ItemIsFocusable);

        group->setZValue(++frontZ);
        this->scene->clearSelection();
        group->setSelected(true);
    }
}

//打散按钮
void MyGraphicsView::slotOn_actBreak_triggered()
{
    int selectedCounts = this->scene->selectedItems().count();  //Scene中选中的个数
    if (selectedCounts ==1)
    {
        auto* group = qgraphicsitem_cast<QGraphicsItemGroup*>(
            scene->selectedItems().at(0));
        if (!group)
        {
            emit sendStrFromGraphicView2Console(tr("当前选中图元不是组合。"));
            return;
        }
        invalidateHistory(tr("图元打散"));
        scene->destroyItemGroup(group);
    }
}

//向上移动
void MyGraphicsView::slotOn_actMoveUp_triggered()
{
    moveSelectedItemsBy(QPointF(0.0, -10.0), tr("上移图元"));
}

//向下移动
void MyGraphicsView::slotOn_actMoveDown_triggered()
{
    moveSelectedItemsBy(QPointF(0.0, 10.0), tr("下移图元"));
}

//向左移动
void MyGraphicsView::slotOn_actMoveLeft_triggered()
{
    moveSelectedItemsBy(QPointF(-10.0, 0.0), tr("左移图元"));
}

//向右移动
void MyGraphicsView::slotOn_actMoveRight_triggered()
{
    moveSelectedItemsBy(QPointF(10.0, 0.0), tr("右移图元"));
}

void MyGraphicsView::rotateSelectedItems(qreal deltaDegrees)
{
    const auto before = selectedItemStates();
    const int rotatedCount = famp::graphics::rotateItems(
        scene->selectedItems(), deltaDegrees);
    if (rotatedCount > 0)
    {
        emit sendStrFromGraphicView2Console(
            QStringLiteral("已旋转 %1 个图元 %2°")
                .arg(rotatedCount)
                .arg(deltaDegrees));
        pushTransformChange(before, tr("旋转图元"));
    }
}

void MyGraphicsView::slotOn_actRotateLeft_triggered()
{
    rotateSelectedItems(-famp::graphics::RotationStepDegrees);
}

void MyGraphicsView::slotOn_actRotateRight_triggered()
{
    rotateSelectedItems(famp::graphics::RotationStepDegrees);
}

//前置按钮
void MyGraphicsView::slotOn_actEditFront_triggered()
{
    int cnt = scene->selectedItems().count();
    if (cnt>0)
    {
        const auto before = selectedItemStates();
        QGraphicsItem * item = scene->selectedItems().at(0);
        item->setZValue(++frontZ);
        pushTransformChange(before, tr("前置图层"));
        emit sendStrFromGraphicView2Console("已将当前选中图层前置成功！");
    }
}

//后置按钮
void MyGraphicsView::slotOn_actEditBack_triggered()
{
    int cnt = scene->selectedItems().count();
    if (cnt > 0)
    {
        const auto before = selectedItemStates();
        QGraphicsItem * item = scene->selectedItems().at(0);
        item->setZValue(--backZ);
        pushTransformChange(before, tr("后置图层"));
        emit sendStrFromGraphicView2Console("已将当前选中图层后置成功！");
    }
}

//保存按钮
void MyGraphicsView::slotOn_actSave_triggered()
{
    QDialog settingsDialog(this);
    settingsDialog.setWindowTitle(tr("专业成果导出"));
    QFormLayout layout(&settingsDialog);

    QComboBox formatCombo(&settingsDialog);
    formatCombo.addItem(tr("PDF 矢量文档"),
                        static_cast<int>(famp::exporting::Format::Pdf));
    formatCombo.addItem(tr("PNG 高分辨率图像"),
                        static_cast<int>(famp::exporting::Format::Png));
    formatCombo.addItem(tr("BMP 无损图像"),
                        static_cast<int>(famp::exporting::Format::Bmp));

    QComboBox paperCombo(&settingsDialog);
    paperCombo.addItem(QStringLiteral("A4"),
                       static_cast<int>(famp::exporting::PaperSize::A4));
    paperCombo.addItem(QStringLiteral("A3"),
                       static_cast<int>(famp::exporting::PaperSize::A3));

    QComboBox orientationCombo(&settingsDialog);
    orientationCombo.addItem(tr("横向"),
                             static_cast<int>(famp::exporting::Orientation::Landscape));
    orientationCombo.addItem(tr("纵向"),
                             static_cast<int>(famp::exporting::Orientation::Portrait));

    QComboBox dpiCombo(&settingsDialog);
    dpiCombo.addItem(QStringLiteral("150 DPI"), 150);
    dpiCombo.addItem(QStringLiteral("300 DPI"), 300);
    dpiCombo.addItem(QStringLiteral("600 DPI"), 600);
    dpiCombo.setCurrentIndex(1);

    QComboBox scaleModeCombo(&settingsDialog);
    scaleModeCombo.addItem(
        tr("保持当前制图比例尺"),
        static_cast<int>(famp::exporting::ScaleMode::PreservePhysicalScale));
    scaleModeCombo.addItem(
        tr("自动适合页面"),
        static_cast<int>(famp::exporting::ScaleMode::FitToPage));

    layout.addRow(tr("格式"), &formatCombo);
    layout.addRow(tr("纸张"), &paperCombo);
    layout.addRow(tr("方向"), &orientationCombo);
    layout.addRow(tr("缩放"), &scaleModeCombo);
    layout.addRow(tr("分辨率"), &dpiCombo);

    QDialogButtonBox buttons(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel,
        &settingsDialog);
    buttons.button(QDialogButtonBox::Save)->setText(tr("选择路径…"));
    connect(&buttons, &QDialogButtonBox::accepted,
            &settingsDialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected,
            &settingsDialog, &QDialog::reject);
    layout.addRow(&buttons);
    if (settingsDialog.exec() != QDialog::Accepted)
        return;

    famp::exporting::Options options;
    options.format = static_cast<famp::exporting::Format>(
        formatCombo.currentData().toInt());
    options.paperSize = static_cast<famp::exporting::PaperSize>(
        paperCombo.currentData().toInt());
    options.orientation = static_cast<famp::exporting::Orientation>(
        orientationCombo.currentData().toInt());
    options.scaleMode = static_cast<famp::exporting::ScaleMode>(
        scaleModeCombo.currentData().toInt());
    options.dotsPerInch = dpiCombo.currentData().toInt();
    options.sceneUnitsPerMillimeterX = metricPixelsPerMillimeter.x();
    options.sceneUnitsPerMillimeterY = metricPixelsPerMillimeter.y();
    options.creator = QStringLiteral("FAMP %1").arg(
        QString::fromLatin1(famp::Version));

    QString filter;
    switch (options.format)
    {
    case famp::exporting::Format::Pdf:
        filter = tr("PDF 文档 (*.pdf)");
        break;
    case famp::exporting::Format::Bmp:
        filter = tr("BMP 图像 (*.bmp)");
        break;
    case famp::exporting::Format::Png:
    default:
        filter = tr("PNG 图像 (*.png)");
        break;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("导出专业成果"),
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("FAMP-export")),
        filter);
    if (filePath.isEmpty())
        return;

    filePath = famp::exporting::pathWithFormatSuffix(filePath, options.format);
    options.title = QFileInfo(filePath).completeBaseName();
    QString saveError;
    if (!famp::exporting::exportScene(
            scene, filePath, options, &saveError))
    {
        QMessageBox::warning(this, tr("导出失败"), saveError);
        emit sendStrFromGraphicView2Console(tr("导出失败：") + saveError);
        return;
    }

    emit sendStrFromGraphicView2Console(
        tr("专业成果已导出到：") + filePath);
}

//指北针按钮
void MyGraphicsView::slotOn_actCompass_triggered()
{
    //QImage * img_compass = new QImage();
    //img_compass->load(":/images/images/compassMap.bmp");

    CompassItem * item = new CompassItem();
    addItemWithHistory(item, tr("添加指北针"));
    scene->clearSelection();

    item->setSelected(true);
    emit sendStrFromGraphicView2Console("已添加指北针！");
}

//添加文字按钮
void MyGraphicsView::slotOn_actText_triggered()
{
    QString str = QInputDialog::getText(this, "添加文字", "请输入文字");
    if (str.isEmpty())      return;

    QGraphicsTextItem *item = new QGraphicsTextItem(str);

    QFont font = this->font();
    font.setPointSize(15);
    font.setBold(true);
    item->setFont(font);
    item->setFlags(QGraphicsItem::ItemIsMovable
        | QGraphicsItem::ItemIsSelectable
        | QGraphicsItem::ItemIsFocusable);

    item->setPos(140, 200);
    item->setData(ItemName, "文字");
    item->setCursor(Qt::SizeAllCursor);
    addItemWithHistory(item, tr("添加文字"));
    scene->clearSelection();
    item->setSelected(true);
}

//接受ComBox发送过来的比例尺
void MyGraphicsView::getScaleComBoxCurrentIndexChanged(int index)
{
    //qDebug() << "Scale" << index;
    switch (index)
    {
    case(0):
    {
        //qDebug() << "1:10" ;
        scaleType = OneToTen;
        sendReDraw(scaleType);      //比例尺改变时重新作图
        //sendScaleToDlgPlotTap(scaleType); //将比例尺发送到出图模板对话框
    }
    break;

    case(1):
    {
        //qDebug() << "1:20";
        scaleType = OneToTwenty;
        sendReDraw(scaleType);      //比例尺改变时重新作图
        //sendScaleToDlgPlotTap(scaleType); //将比例尺发送到出图模板对话框
    }
    break;

    case(2):
    {
        //qDebug() << "1:50";
        scaleType = OneToFifty;
        sendReDraw(scaleType);      //比例尺改变时重新作图
        //sendScaleToDlgPlotTap(scaleType); //将比例尺发送到出图模板对话框
    }
    break;

    case(3):
    {
        //qDebug() << "1:100";
        scaleType = OneToHundred;
        sendReDraw(scaleType);      //比例尺改变时重新作图
        //sendScaleToDlgPlotTap(scaleType); //将比例尺发送到出图模板对话框
    }
    break;

    default:
        break;
    }
}

int MyGraphicsView::scaleDenominator(ScaleType scale)
{
    switch (scale)
    {
    case OneToTen:     return 10;
    case OneToTwenty:  return 20;
    case OneToFifty:   return 50;
    case OneToHundred: return 100;
    }
    return 50;
}

QPointF MyGraphicsView::pixelsPerMillimeterForScreen(QScreen *screen) const
{
    if (!screen)
    {
        const qreal fallback =
            famp::metric::deviceIndependentPixelsPerMillimeter(
                famp::metric::DefaultDotsPerInch);
        return QPointF(fallback, fallback);
    }

    const qreal dpiX = famp::metric::bestAvailableDotsPerInch(
        screen->physicalDotsPerInchX(), screen->logicalDotsPerInchX());
    const qreal dpiY = famp::metric::bestAvailableDotsPerInch(
        screen->physicalDotsPerInchY(), screen->logicalDotsPerInchY());
    return QPointF(
        famp::metric::deviceIndependentPixelsPerMillimeter(dpiX),
        famp::metric::deviceIndependentPixelsPerMillimeter(dpiY));
}

QPointF MyGraphicsView::scaleOffsetFor(ScaleType scale) const
{
    const int denominator = scaleDenominator(scale);
    return QPointF(
        famp::metric::pixelsPerMeterAtScale(metricPixelsPerMillimeter.x(), denominator),
        famp::metric::pixelsPerMeterAtScale(metricPixelsPerMillimeter.y(), denominator));
}

void MyGraphicsView::setMetricScreen(QScreen *screen)
{
    if (metricScreen == screen)
    {
        refreshMetricLayout();
        return;
    }

    if (metricScreen)
        disconnect(metricScreen.data(), nullptr, this, nullptr);

    metricScreen = screen;
    if (metricScreen)
    {
        connect(metricScreen.data(), &QScreen::physicalDotsPerInchChanged,
                this, &MyGraphicsView::refreshMetricLayout);
        connect(metricScreen.data(), &QScreen::logicalDotsPerInchChanged,
                this, &MyGraphicsView::refreshMetricLayout);
        connect(metricScreen.data(), &QScreen::physicalSizeChanged,
                this, &MyGraphicsView::refreshMetricLayout);
    }

    refreshMetricLayout();
}

void MyGraphicsView::refreshMetricLayout()
{
    const QPointF newPixelsPerMillimeter =
        pixelsPerMillimeterForScreen(metricScreen.data());
    if (fuzzyPointCompare(metricPixelsPerMillimeter,
                          newPixelsPerMillimeter))
    {
        viewport()->update();
        return;
    }

    metricPixelsPerMillimeter = newPixelsPerMillimeter;
    deltaOffset = scaleOffsetFor(scaleType);
    emit sendScaleOffset(deltaOffset);

    if (scene && !scene->items().isEmpty())
        ReDraw(deltaOffset);

    viewport()->update();
}

//接受改变比例尺时重新画图
void MyGraphicsView::getReDraw(ScaleType scale)
{
    scaleType = scale;
    deltaOffset = scaleOffsetFor(scaleType);
    qDebug() << "重新作图1:" << scaleDenominator(scaleType)
             << "每毫米逻辑像素:" << metricPixelsPerMillimeter;
    emit sendScaleOffset(deltaOffset);
    ReDraw(deltaOffset);
}

//得到比例尺变化后坐标的偏移量
void MyGraphicsView::getScaleOffset(QPointF offset)
{
    this->deltaOffset = offset;
}

//出图模板按钮
void MyGraphicsView::slotOn_actPlotTab_triggered()
{
    //sendDlgClipVisible(false);        //关闭裁剪对话框
    setDlgPlotTab();
    sendGetCurrentScale();
    //qDebug() << "scale" << currentScaleIndex;
    dlgPlotTab->getCurrentScaleIndex(currentScaleIndex);    //将当前比例尺发送给出图模板对话框
}

//显示按当前显示器物理DPI绘制的毫米米格纸
void MyGraphicsView::slotOn_actMiGe_triggered(bool checked)
{
    metricGridVisible = checked;
    viewport()->update();
}

void MyGraphicsView::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->save();
    painter->fillRect(rect, Qt::white);

    if (!metricGridVisible
        || metricPixelsPerMillimeter.x() <= 0.0
        || metricPixelsPerMillimeter.y() <= 0.0)
    {
        painter->restore();
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);

    QPainterPath minorLines;
    QPainterPath halfCentimeterLines;
    QPainterPath centimeterLines;

    const qreal stepX = metricPixelsPerMillimeter.x();
    const qreal stepY = metricPixelsPerMillimeter.y();
    const qint64 firstX = static_cast<qint64>(std::floor(rect.left() / stepX));
    const qint64 lastX = static_cast<qint64>(std::ceil(rect.right() / stepX));
    const qint64 firstY = static_cast<qint64>(std::floor(rect.top() / stepY));
    const qint64 lastY = static_cast<qint64>(std::ceil(rect.bottom() / stepY));

    for (qint64 index = firstX; index <= lastX; ++index)
    {
        QPainterPath *path = &minorLines;
        const int position = gridLinePosition(index);
        if (position == 0)
            path = &centimeterLines;
        else if (position == 5)
            path = &halfCentimeterLines;

        const qreal x = index * stepX;
        path->moveTo(x, rect.top());
        path->lineTo(x, rect.bottom());
    }

    for (qint64 index = firstY; index <= lastY; ++index)
    {
        QPainterPath *path = &minorLines;
        const int position = gridLinePosition(index);
        if (position == 0)
            path = &centimeterLines;
        else if (position == 5)
            path = &halfCentimeterLines;

        const qreal y = index * stepY;
        path->moveTo(rect.left(), y);
        path->lineTo(rect.right(), y);
    }

    QPen pen(Qt::red);
    pen.setCosmetic(true);
    pen.setCapStyle(Qt::FlatCap);

    pen.setWidthF(0.5);
    painter->setPen(pen);
    painter->drawPath(minorLines);

    pen.setWidthF(1.0);
    painter->setPen(pen);
    painter->drawPath(halfCentimeterLines);

    pen.setWidthF(2.0);
    painter->setPen(pen);
    painter->drawPath(centimeterLines);
    painter->restore();
}

void MyGraphicsView::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);

    QWindow *windowHandle = window()->windowHandle();
    if (!windowHandle)
        return;

    connect(windowHandle, &QWindow::screenChanged,
            this, &MyGraphicsView::setMetricScreen,
            Qt::UniqueConnection);
    setMetricScreen(windowHandle->screen());
}

void MyGraphicsView::keyPressEvent(QKeyEvent *e)
{
    emit this->keyPress(e);
    QGraphicsView::keyPressEvent(e);
}

void MyGraphicsView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
    {
        QPoint point = e->pos();
        emit this->mouseClicked(point);
    }
    QGraphicsView::mousePressEvent(e);
    mousePressItemStates = e->button() == Qt::LeftButton
        ? selectedItemStates()
        : QVector<famp::graphics::ItemState>();
}

void MyGraphicsView::mouseMoveEvent(QMouseEvent *e)
{
    QPoint point = e->pos();
    gvScenePos = this->mapToScene(point);
    emit this->mouseMovePoint(point);

    return QGraphicsView::mouseMoveEvent(e);
}

void MyGraphicsView::mouseReleaseEvent(QMouseEvent *e)
{
    QGraphicsView::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton && !mousePressItemStates.isEmpty())
        pushTransformChange(mousePressItemStates, tr("拖动图元"));
    mousePressItemStates.clear();
}

void MyGraphicsView::mouseDoubleClickEvent(QMouseEvent * e)
{
    QPoint point = e->pos();
    QPointF pointScene = this->mapToScene(point);       //转换到Scene坐标

    QGraphicsItem  *item = NULL;
    item = scene->itemAt(pointScene, this->transform());    //获取光标下的绘图项

    if (item == NULL)   return;

    switch (item->type())
    {
    case(QGraphicsTextItem::Type):
    {
        QGraphicsTextItem * textItem = qgraphicsitem_cast<QGraphicsTextItem*>(item);

        const QFont previousFont = textItem->font();
        QFont font = previousFont;
        bool ok = false;
        font = QFontDialog::getFont(&ok, font, this, "设置字体");
        if (ok && font != previousFont)
        {
            textItem->setFont(font);
            history->push(famp::graphics::makeTextFontCommand(
                handleForItem(textItem), previousFont, font, tr("修改文字字体")));
        }
    }
    break;

    default:
        break;
    }
}
