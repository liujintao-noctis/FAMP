/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 2D 米格纸视图 — DP 简化、B 样条拟合、KNN Alpha Shape
 *****************************************************************/

#pragma once

#include "GraphicsUndoCommands.h"
#include "MyItem.h"
#include "CloudProjection.h"
#include "QDlgPlotTab.h"
#include "CompassItem.h"
#include "FormTabulationItem.h"
#include "MeasurementItem.h"
#include "ContourItem.h"

#include <QObject>
#include <QWidget>
#include <QLabel>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QString>
#include <QComboBox>
#include <QGraphicsItem>
#include <QApplication>
#include <QRect>
#include <QDebug>
#include <QPoint>
#include <QEvent>
#include <QMouseEvent>
#include <QPen>
#include <QFileDialog>
#include <QItemSelectionModel>
#include <QPointer>
#include <QStandardItemModel>
#include <QVector3D>
#include <QFontDialog>
#include <QInputDialog>
#include <QGraphicsSceneEvent>
#include <QHash>
#include <QJsonObject>

#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/io/pcd_io.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/filters/uniform_sampling.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/search/kdtree.h>
#include <pcl/common/common.h>
#include <pcl/common/distances.h>
#include <pcl/common/point_tests.h>
#include <pcl/features/moment_of_inertia_estimation.h>

#include <vtkPlane.h>

#include <queue>
#include <memory>
#include <vector>

class QPainter;
class QGraphicsPathItem;
class QGraphicsSimpleTextItem;
class QScreen;
class QShowEvent;
class QUndoStack;
class vtkActor;

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

struct WorkspaceGraphicsItem
{
    QString id;
    QString name;
    bool measurement = false;
    bool visible = true;
    famp::graphics::ItemHandle handle;
};

struct DPPoint      //DP算法
{
    int ID;
    std::string Name;
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
    QLabel *labelScene;     //显示坐标信息
    void getDBItemCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud);      //获得DBtree的点云
    void setSectionPlaneReference(const QVector3D& origin,
                                  const QVector3D& normal);
    void clearSectionPlaneReference();
    QPointF gvScenePos;     //鼠标的Scene坐标
    int currentScaleIndex;  //当前比例尺索引

    QString dataText;
    QString designerText;
    QString scaleText;
    void setDlgPlotTabNull();           //设置出图表对话框为空指针
    void drawFormTable();           //绘制出图模板表格
    void getText();                 //获得制图人，比例尺，日期文字
    QUndoStack* commandStack() const;
    void clearSceneAndHistory();
    QJsonObject saveProjectState(QString* errorMessage = nullptr) const;
    bool validateProjectState(const QJsonObject& state,
                              QString* errorMessage = nullptr) const;
    bool restoreProjectState(const QJsonObject& state,
                             QString* errorMessage = nullptr);
    QVector<WorkspaceGraphicsItem> workspaceGraphicsItems();
    int projectionDrawingCount() const;
    bool hasProjectionDrawing(famp::projection::Plane plane) const;
    QRectF projectionDrawingSceneBounds(
        famp::projection::Plane plane) const;
    QPointF projectionDrawingSceneOrigin(
        famp::projection::Plane plane) const;
    bool hasSectionCutLine(famp::projection::Plane profilePlane) const;
    QRectF sectionCutLineSceneBounds(
        famp::projection::Plane profilePlane) const;
    QRectF projectionLayoutSceneBounds() const;
    QRectF drawingSceneRect() const;
    int drawingScaleDenominator() const;
    bool hasSelectedItems() const;
    bool setProjectionInput(
        const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud,
        famp::projection::Plane plane,
        QString* errorMessage = nullptr);
    void clearProjectionInput();
    bool hasProjectionInput() const;
    famp::projection::Plane projectionPlane() const;
    bool confirmProjectionRotation(QWidget* dialogParent = nullptr);
    bool confirmInitialOrthographicRotation(QWidget* dialogParent = nullptr);
    bool setProjectionRotationDegrees(famp::projection::Plane plane,
                                      int degrees);
    int projectionRotationDegrees(famp::projection::Plane plane) const;
    bool setOrthographicRotationDegrees(int degrees);
    int orthographicRotationDegrees() const;
    bool setWorkspaceItemVisible(const QString& id, bool visible);
    bool removeWorkspaceItemPermanently(const QString& id);
    void selectWorkspaceItem(const QString& id, bool center = false);
    void clearWorkspaceItemSelection();
    bool addTerrainContours(
        const QVector<famp::terrain::ContourLine>& lines,
        double horizontalUnitToMetre,
        const QString& sourceCrs,
        const QString& sourceLayerId,
        const QString& sourceLayerName,
        const QString& demPath,
        double interval,
        double baseElevation,
        QString* errorMessage = nullptr);
    int terrainContourCount() const;

private:
    // 绘制投影的配置参数，消除4个draw方法的代码重复
    struct DrawConfig {
        ProjectType projType;           // 投影类型：XOZ, YOZ, XOY, OLXOY
        QString labelText;              // Item的标签文本
        bool isClosed;                  // B样条闭合参数 (OLXOY=true, 其他=false)
        bool useVoxelDownsample;        // 是否体素降采样+KNN边界提取
        const char* debugOrderFile;     // 有序点云调试保存文件名
        const char* debugDPFile;        // DP简化后点云调试保存文件名
        const char* consoleMsg;         // 控制台输出消息
    };

    QGraphicsScene * scene;
    QUndoStack * history;
    QHash<QGraphicsItem*, std::weak_ptr<famp::graphics::ItemLifetime>> itemHandles;
    void invalidateItemHandles();
    QVector<famp::graphics::ItemState> mousePressItemStates;
    bool measurementActive = false;
    famp::measurement::Kind measurementKind = famp::measurement::Kind::Distance;
    QVector<QPointF> measurementScenePoints;
    QPointF measurementHoverPoint;
    bool measurementHasHoverPoint = false;
    QGraphicsPathItem* measurementPreviewPath = nullptr;
    QGraphicsSimpleTextItem* measurementPreviewLabel = nullptr;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr project_cloud;   //VTK投影后的点云
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr currentItemCloud;    //获得DBtree的点云
    bool hasSectionPlaneReference_ = false;
    QVector3D sectionPlaneOrigin_;
    QVector3D sectionPlaneNormal_;
    double computeCloudMeanDis(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud);       //计算点云的平均密度（点与点之间的平均距离）
    ProjectType project_type = NONE;   //由投影命令显式指定，不再根据坐标猜测
    famp::projection::Plane projectionPlane_ =
        famp::projection::Plane::XOY;
    // The plan and its two section lines share the overlook rotation. Each
    // profile keeps its own confirmed direction and is laid out only after
    // that direction is known.
    int orthographicRotationDegrees_ = 0;
    int xozRotationDegrees_ = 0;
    int yozRotationDegrees_ = 0;
    int dlgDrawXOYLine();       //是否绘制切割线对话框
    void drawProjection(const DrawConfig& config, QPointF offset);  //统一的投影绘制方法
    void drawXOZ(QPointF offset);       //画出XOZ面的投影
    void drawYOZ(QPointF offset);       //画出YOZ面的投影
    void drawXOY(QPointF offset);       //画出XOY面的投影
    void drawOverLookXOY(QPointF offset);       //画出俯视XOY面的投影
    void drawXOYLine(QPointF offset);       //画出XOY面的剖线
    void drawSectionCutLine(ProjectType profileType, QPointF offset);

    void KNNAlphaShape(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr &cloud, float Alpha, float neborRadius, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &outcloud);  //KNNAlphaShape提取边界
    void findMaxDistancePointsofCloud(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & incloud, int headendPointIndex[2]);    //寻找点云中最远的两点的索引
    void orderSoetCloud(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & incloud, int neborNumbers,pcl::PointCloud<pcl::PointXYZRGB>::Ptr & outcloud);    //将无序点云进行有序排列

    //根据点云和投影类型将PCL按照不同比例尺转为QT点集
    void PCLCloud2QTPoints(const pcl::PointCloud<pcl::PointXYZRGB>::Ptr&  incloud, ProjectType protype, QVector<QPointF> &points,QPointF offset);

    int backZ = 0;      //前置
    int frontZ = 0;     //后置

    static const int ItemName = 1;  //自定义Item名字
    static const int ItemCloud = 2;     //自定义储存点云
    ScaleType scaleType;    //当前比例尺
    QPointF deltaOffset;        //比例尺变换后x,y的偏移量
    bool metricGridVisible = false;     //是否显示物理毫米网格
    QPointF metricPixelsPerMillimeter;  //当前屏幕每毫米对应的Qt逻辑像素
    QPointer<QScreen> metricScreen;      //当前绘图窗口所在屏幕

    static int scaleDenominator(ScaleType scale);
    QPointF pixelsPerMillimeterForScreen(QScreen *screen) const;
    QPointF scaleOffsetFor(ScaleType scale) const;
    void setMetricScreen(QScreen *screen);
    void refreshMetricLayout();
    MyItem* primaryProjectionItem(ProjectType type) const;
    MyItem* sectionCutLineItem(ProjectType profileType) const;
    void layoutOrthographicProjectionDrawings();
    void ReDraw(QPointF offset);    //比例尺改变后重新绘制
    void rotateSelectedItems(qreal deltaDegrees);
    famp::graphics::ItemHandle handleForItem(QGraphicsItem* item);
    QVector<famp::graphics::ItemHandle> handlesForItems(
        const QList<QGraphicsItem*>& items);
    QVector<famp::graphics::ItemState> selectedItemStates();
    void pushTransformChange(const QVector<famp::graphics::ItemState>& before,
                             const QString& text);
    void addItemWithHistory(QGraphicsItem* item, const QString& text);
    void invalidateHistory(const QString& reason);
    void moveSelectedItemsBy(const QPointF& delta, const QString& text);
    void beginMeasurement(famp::measurement::Kind kind, bool announce);
    void updateMeasurementPreview();
    void finishMeasurement();
    void resetMeasurementInteraction(bool notify);
    void rescaleMeasurementItems();
    void rescaleTerrainItems();
    void applyScale(ScaleType scale);

    QDlgPlotTab  *dlgPlotTab;
    void setDlgPlotTab();           //设置弹出出图模板对话框

    //----------DP算法------------
    double point2LineDist(const DPPoint& p1, const DPPoint& p2, const DPPoint& p3);      //计算点到线的距离
    std::pair<double, int> getMaxDistAndIndex(std::vector<DPPoint> &Points, int begin, int end);  //计算点集中的最大值及其索引
    void computeDP(std::vector<DPPoint> &Points, int begin, int end, double threshold); //DP算法简化线条点

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void showEvent(QShowEvent *event) override;

    //鼠标键盘重写事件
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

public slots:
    void slotOn_actMiGe_triggered(bool checked);        //显示米格纸
    void slotCalibrateMetricGrid();                     //按显示器保存100 mm直尺校准
    void slotOn_actPoints_triggered();
    void slotOn_actProjLine_triggered();            //投影连线按钮
    void slotOn_actDeleteItem_triggered();          //删除
    void slotOn_actClearScene_triggered();          //清空
    void slotOn_actGroup_triggered();           //组合按钮
    void slotOn_actBreak_triggered();           //打散按钮
    void slotOn_actMoveUp_triggered();          //向上移动
    void slotOn_actMoveDown_triggered();            //向下移动
    void slotOn_actMoveLeft_triggered();            //向左移动
    void slotOn_actMoveRight_triggered();           //向右移动
    void slotOn_actRotateLeft_triggered();           //逆时针旋转
    void slotOn_actRotateRight_triggered();          //顺时针旋转
    void slotOn_actEditFront_triggered();           //前置按钮
    void slotOn_actEditBack_triggered();            //后置按钮
    void slotOn_actSave_triggered();            //保存按钮
    void slotOn_actCompass_triggered();         //指北针按钮
    void slotOn_actText_triggered();            //添加文字按钮
    void getScaleComBoxCurrentIndexChanged(int index);          //接受ComBox改变时发送过来的比例尺

    void getReDraw(ScaleType scale);                //接受改变比例尺时重新画图
    void getScaleOffset(QPointF offset);        //得到比例尺变化后坐标的偏移量
    void slotOn_actPlotTab_triggered();         //出图模板按钮
    void startDistanceMeasurement(bool announce = true);
    void startAreaMeasurement(bool announce = true);
    void startAngleMeasurement(bool announce = true);
    void deactivateMeasurement();
    int measurementCount() const;
    void clearMeasurements(bool announce = true);
    void cancelMeasurement();

signals:
    void keyPress(QKeyEvent *e);
    void mouseClicked(QPoint point);
    void mouseMovePoint(QPoint point);
    void sendActor(vtkActor * actor);   //发送演员
    void sendStrFromGraphicView2Console(QString str);   //将消息发送到Console
    void sendClosedXOYLabel(bool enable);       //发送是否关闭XOY图标
    void sendClosedScale(bool enable);          //发送是否关闭比例尺
    void sendDlgClipVisible(bool enable);       //发送是否隐藏VTK的平面裁剪对话框
    void sendReDraw(ScaleType scale);           //发送改变比例尺时重新画图
    void sendScaleOffset(QPointF offset);       //比例尺变化后坐标的偏移量
    void sendGetCurrentScale();         //发送获得当前比例尺信号
    void selectionAvailabilityChanged(bool available);
    void measurementModeEnded();
    void measurementStatus(QString message);
    void scaleIndexChangedByHistory(int index);
    void metricGridVisibilityChanged(bool visible);
    void workspaceItemsChanged();
    void projectionDrawingCreated(famp::projection::Plane plane);
    //void sendGetText();                   //发送获得制图人，比例尺，日期的信号
};
