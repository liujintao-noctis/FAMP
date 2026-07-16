/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: VTK 3D 视图门面 — 平面切割、投影
 *****************************************************************/

#pragma once

#include "QstringAndStringConvert.h"
#include "QDlgClip.h"
#include "Measurement.h"

#include <QObject>
#include <QtWidgets/QMainWindow>
#include <QDebug>
#include <QMessageBox>
#include <QHash>
#include <QVector>
#include <QVector3D>
#include <QFileDialog>

#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <vtkAutoInit.h>
#include <vtkVersion.h>
#include <vtkActor.h>
#include <vtkSmartPointer.h>
#include <vtkPlaneWidget.h>
#include <vtkPlane.h>
#include <vtkMath.h>
#include <vtkCamera.h>
#include <vtkPolyData.h>
#include <vtkNamedColors.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkProperty.h>
#include <vtkBillboardTextActor3D.h>
#include <vtkCallbackCommand.h>
#include <vtkPointPicker.h>
#include <vtkPolyDataMapper.h>

#include <vector>

#if VTK_MAJOR_VERSION >= 9
#include <QVTKOpenGLNativeWidget.h>
#define FAMP_QVTK_WIDGET QVTKOpenGLNativeWidget
#else
#include <QVTKWidget.h>
#define FAMP_QVTK_WIDGET QVTKWidget
#endif

// 投影平面枚举
enum class ProjectionPlane { YOZ, XOZ, XOY, OVERLOOK };

class VTKRenderManager;
class VTKPointCloudManager;
class VTKProjectionManager;

class MyVTK : public FAMP_QVTK_WIDGET
{
    Q_OBJECT

public:
    MyVTK(QWidget *parent);
    ~MyVTK();

private:
    struct MeasurementVisual
    {
        famp::measurement::Record3D record;
        vtkSmartPointer<vtkActor> geometry;
        vtkSmartPointer<vtkBillboardTextActor3D> label;
    };

    struct CloudActorMetadata
    {
        QString layerId;
        QString crs;
    };

    // 管理器实例
    VTKRenderManager *m_renderManager;
    VTKPointCloudManager *m_pointCloudManager;
    VTKProjectionManager *m_projectionManager;

    //显示点云
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr orignalCloud;    //从主窗口获得的点云
    vtkPolyData * AABB_Polydata;

private:
    vtkPlaneWidget * randomPlane;       //随机平面
    vtkPlaneWidget * horizonalPlane;    //水平平面
    vtkPlaneWidget * verticalPlane;     //垂直平面
    vtkPlaneWidget * clipPlane;     //切割平面
    QDlgClip * dlgClip;             //切割平面对话框
    QDlgClip * vtkDlgClip;

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr currentItemCloud;        //获得DB Tree下的点云

    bool measurementActive = false;
    famp::measurement::Kind measurementKind =
        famp::measurement::Kind::Distance;
    QString measurementLayerId;
    QString measurementCrs;
    QVector<QVector3D> measurementPoints;
    QVector3D measurementHoverPoint;
    bool measurementHasHoverPoint = false;
    vtkSmartPointer<vtkPointPicker> measurementPicker;
    vtkSmartPointer<vtkCallbackCommand> measurementCallback;
    std::vector<unsigned long> measurementObserverTags;
    std::vector<vtkActor*> cloudActors;
    QHash<vtkActor*, CloudActorMetadata> cloudActorMetadata;
    vtkSmartPointer<vtkPolyDataMapper> measurementPreviewMapper;
    vtkSmartPointer<vtkActor> measurementPreviewActor;
    vtkSmartPointer<vtkBillboardTextActor3D> measurementPreviewLabel;
    std::vector<MeasurementVisual> measurementVisuals;

    static void measurementEventCallback(vtkObject* caller,
                                         unsigned long eventId,
                                         void* clientData,
                                         void* callData);
    bool handleMeasurementEvent(unsigned long eventId);
    bool pickMeasurementPoint(QVector3D& point,
                              QString& layerId,
                              QString& crs);
    void beginMeasurement(famp::measurement::Kind kind, bool announce);
    void updateMeasurementPreview();
    void finishMeasurement();
    void resetMeasurementInteraction(bool notify);
    void rebuildMeasurementPickList();
    void addMeasurementVisual(const famp::measurement::Record3D& record);
    bool hasRegisteredLayer(const QString& layerId) const;
    bool recordMatchesRegisteredLayer(
        const famp::measurement::Record3D& record,
        QString* errorMessage = nullptr) const;
    bool isLayerVisible(const QString& layerId) const;
    void updateMeasurementVisibility(const QString& layerId, bool visible);

public:
    void setFrontView();    //正视图
    void setTopView();      //顶视图
    void setLeftView();     //左视图
    void setRightView();    //右视图
    void setBottomView();   //底视图
    void setBackView(); //后视图
    vtkActor * appendCloudActor();      //添加点云演员
    vtkActor * appendAABBActor();       //添加AABB包围盒演员
    bool updateCloudActors(
        vtkActor * cloudActor,
        vtkActor * aabbActor,
        const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& cloud);
    bool setCloudActorMetadata(vtkActor * actor,
                               const QString& layerId,
                               const QString& crs,
                               QString* errorMessage = nullptr);
    void unregisterCloudActor(vtkActor * actor);
    void display(vtkActor * actor);     //显示点云
    void removeCloudDisplay(vtkActor * actor);      //移除点云演员显示
    void removeAABBDisplay(vtkActor * actor);       //移除AABB演员显示
    void initCamera();      //初始化相机
    void refresh();         //立即刷新渲染窗口
    vtkPlaneWidget * DisplayVerticalPlane();        //显示相机垂直面
    vtkPlaneWidget * DisplayHorizonalPlane();       //显示相机垂直面
    vtkPlaneWidget * DisplayRandomPlane();          //显示随机面
    void beginClipPlane();                          //开始进行切割
    void  setDlgClip();                     //弹出切割平面对话框
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_cut_plane;     //在切割面上的点云
    void setQDlgClipNULL();                 //将QDLG对话框设置空指针
    void getDBItemCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr Cloud);      //获得DBTree下的点云
    void endCutRemoveActors();                  //切割结束后关闭掉切割平面和移除掉显示的选中的演员
    bool isActiveDlgClip;           //切割平面对话框是否激活
    void saveDlgCloudFile();            //保存点云文件
    bool isClipSucessed;            //判断是否已经切割成功生成点
    void triggeredSignalProj();     //触发sendActProjLineEnable(bool enable)信号
    bool isProjectionSuccess;       //是否投影完成开始在米格纸上绘图
    void triggeredSignalXOYProj();  //触发发送投影到XOY按钮的信号
    void triggeredSignalXOZProj();  //触发发送投影到XOZ按钮的信号
    void triggeredSignalYOZProj();  //触发发送投影到YOZ按钮的信号
    void triggeredSignalOverLookProj(); //触发发送俯视投影按钮的信号
    void AABBOrignalPosAxis(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud);        //以AABB最小的坐标创建坐标轴
    void displayAABBOrignalPosAxis(bool enable);            //显示AABB最小的坐标创建坐标轴
    void projectToPlane(ProjectionPlane plane);         //统一的投影到平面方法
    float getAABBCoordinateMax(pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud); //获得AABB最大值最小值之差
    void computeCurrentCloudAABB(pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud);   //计算选中的点云AABB包围盒

    // getter 供 MainWindow 等外部代码访问内部 VTK 对象
    vtkCamera * getCamera() const;

public slots:
    void getOrignalCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud);//从主窗口获得点云
    void getAABBPolydata(vtkPolyData * polyData);               //获得AABBPolydata
    void getClipPlane(vtkPlaneWidget *plane);                   //接受主窗口发送过来的切割面
    void getPbnClipEnable(bool enable);         //从主窗口获得切割按钮禁用与否
    void getActorFromGraphicView(vtkActor *actor);      //接受GraphicsView发送过来的演员
    void slotActProjYOZ_triggered();        //投影到YOZ面按钮
    void slotActProjXOZ_triggered();    //投影到XOZ面按钮
    void slotActProjXOY_triggered();    //投影到XOY面按钮
    void slotActOverLookProj_triggered();       //俯视投影按钮
    void setDlgClipVisible(bool enable);        //设置弹出的对话框是否隐藏
    void startDistanceMeasurement(bool announce = true);
    void startAreaMeasurement(bool announce = true);
    void startAngleMeasurement(bool announce = true);
    void cancelMeasurement();
    void deactivateMeasurement();
    int measurementCount() const;
    QVector<famp::measurement::Record3D> measurements() const;
    bool addMeasurement(const famp::measurement::Record3D& record,
                        QString* errorMessage = nullptr);
    bool removeMeasurement(const QString& recordId);
    bool setMeasurements(
        const QVector<famp::measurement::Record3D>& records,
        QString* errorMessage = nullptr);
    void clearMeasurements(bool announce = true);

signals:
    void sendAABBPolydata(vtkPolyData * polyData);              //发送AABBPolydata
    void sendGetDBItem();       //获得当前点击下DB Tree下的点云
    void sendStrFromVTK2Console(QString str);
    void sendClipCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud);     //将裁剪成功生成的点云发送到米格纸界面进行投影
    void sendActProjLineEnable(bool enable);        //发送投影按钮能否获得
    void sendActProjXOYEnable(bool enable);     //发送投影到XOY按钮能否获得
    void sendActProjXOZEnable(bool enable);     //发送投影到XOZ按钮能否获得
    void sendActProjYOZEnable(bool enable);     //发送投影到YOZ按钮能否获得
    void sendActOverLookProjEnable(bool enable);        //发送俯视投影按钮能否获得
    void sendIsOverLookProj(bool isproj);       //发送判断是否进行俯视投影
    void sendProjCloud2GraphicsView(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud);    //将投影后的点云发送到GraphicsView
    void sendAABBBoxXYZMAX(float x, float y, float z);      //将AABB的边界框发送到GraphicsView
    void measurementModeEnded();
    void measurementCompleted(const famp::measurement::Record3D& record);
    void measurementsChanged();
    void measurementStatus(const QString& message);
};
