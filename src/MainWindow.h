/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 主窗口 — UI 编排、点云加载、DB Tree 管理
 *****************************************************************/

#pragma once

#include "ui_MainWindow.h"
#include "MyVTK.h"
#include "Cloud.h"
#include "QDlgClip.h"
#include "MyGraphicsView.h"
#include "ProjectDocument.h"

#include <QtWidgets/QMainWindow>
#include <QDockWidget>
#include <QWidget>
#include <QLabel>
#include <QIcon>
#include <QStandardItemModel>
#include <QDebug>
#include <QStringList>

#include <vtkPlaneWidget.h>

class FAMPController;
class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QMenu;
class QTimer;

struct MyCloudList
{
    int id;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr input_cloud;
    vtkActor * cloudactor;
    vtkActor * AABBactor;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = Q_NULLPTR);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    Ui::MainWindowClass ui;

private:
    MyVTK * myVTK;
    //MyGraphicsView * myGview;
    QWidget *centerDock;
    Cloud * myCloud;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr inCloud;     //读取的点云
    QStandardItemModel * model;     //标准模式
    QIcon icon_1;   //文件夹图标
    QIcon icon_2;   //点云图标
    QStandardItem * itemProject;    //文件夹
    QStandardItem * itemCloud;      //文件点云
    bool isAABB;    //是否显示AABB
    QDlgClip * dlgClip;
    MyItem *myItem;
    FAMPController * controller;
    QMenu * recentFilesMenu;
    QStringList recentFiles;
    QAction * newProjectAction;
    QAction * openProjectAction;
    QAction * saveProjectAction;
    QAction * saveProjectAsAction;
    QTimer * autosaveTimer;
    QString currentProjectPath;
    QMenu * toolsMenu;
    QAction * setProjectCrsAction;
    QAction * coordinateConverterAction;
    QLabel * crsStatusLabel;
    QString projectCrs;
    bool projectDirty;
    bool loadingProject;

    QLabel *xoy_label;      //在GraphicsView左上方添加XOY坐标的图片
    QHBoxLayout *layout;    //添加一个垂直布局
    void addXOYLabel();     //添加XOY标签
    void setXOYLabelVisible(bool enable);   //设置标签的可见性

    void addScaleWidget();                  //添加比例尺
    QComboBox * scaleCombox;
    QLabel * scaleLabel;
    void setScaleVisible(bool enable);          //设置比例尺的可见性
    void showPlaneWidget(vtkPlaneWidget* (MyVTK::*displayFunc)(), const char* consoleMsg); //统一的平面部件显示方法
    void showHelpDialog(const QString& title, const QString& html);
    bool openCloudFile(const QString& path);
    void initializeRecentFilesMenu();
    void addRecentFile(const QString& path);
    void updateRecentFilesMenu();
    void initializeProjectActions();
    void initializeCrsActions();
    famp::project::Document currentProjectDocument() const;
    QString recoveryProjectPath() const;
    bool saveProject(bool forceSaveAs);
    bool saveProjectToPath(const QString& path);
    bool loadProjectFromPath(const QString& path, bool isRecovery = false);
    bool maybeSaveCurrentProject();
    void removeCloudFromWorkspace(const MyCloudList& cloud);
    void clearWorkspace();
    void markProjectDirty();
    void updateWindowTitle();
    void updateCrsStatus();
    void removeRecoveryProject();
    void checkForRecoveryProject();

private slots:
    //GrapView显示浮动
    void slotOn_actGraViewVisible_triggered(bool checked);
    void slotOn_actGraViewVisible_visibilityChanged(bool visible);
    void slotOn_actGraViewFloat_triggered(bool checked);
    void slotOn_actGraViewFloat_topLevelChanged(bool topLevel);
    //VTK显示
    void slotOn_actVTKVisible_triggered(bool checked);
    void slotOn_actVTKViewVisible_visibilityChanged(bool visible);
    //Console显示浮动
    void slotOn_actConsoleVisible_triggered(bool checked);
    void slotOn_actConsoleVisible_visibilityChanged(bool visible);
    void slotOn_actConsoleFloat_triggered(bool checked);
    void slotOn_actConsoleFloat_topLevelChanged(bool topLevel);
    //DBTree显示浮动
    void slotOn_actDBTreeVisible_triggered(bool checked);
    void slotOn_actDBTreeVisible_visibilityChanged(bool visible);
    void slotOn_actDBTreeFloat_triggered(bool checked);
    void slotOn_actDBTreeFloat_topLevelChanged(bool topLevel);

    void slotFullScreen();  //全屏显示
    void slotFrontView();//正视
    void slotTopView(); //顶视
    void slotBottomView();  //底视
    void slotLeftView();    //左视
    void slotRightView();   //右视
    void slotBackView();    //后视

    void slotOpenCloud();   //打开点云文件
    void slotNewProject();
    void slotOpenProject();
    void slotSaveProject();
    void slotSaveProjectAs();
    void slotAutosaveProject();
    void slotSetProjectCrs();
    void slotOpenCoordinateConverter();
    void slotShowQuickStart();
    void slotShowShortcuts();
    void slotShowAbout();

    void slotOn_treeView_clicked(const QModelIndex & index);    //点击DB Tree项目
    void slotOn_treeItemChanged(QStandardItem * item);          //DB Tree  发生变化
    void slotOn_actDelete_triggered();              //DB Tree 删除项

    void slotOn_actAABB_triggered(bool checked);        //开启AABB按钮
    void slotGetStr2Console(QString text);          //接受消息到Console

    //切割平面
    void slotOn_actRandomPlane_triggered(bool checked);     //任意平面
    void slotOn_actVerticalPlane_triggered();   //垂直平面
    void slotOn_actHorizonalPlane_triggered(bool checked);  //水平平面
    void DBTreeSendVTKItemCloud();                          //将DBTree下的Item点云送到VTK
    void DBTreeSendGraphicViewItemCloud();                          //将DBTree下的Item点云送到GraphicView

    void setClipButtonEnable(const QModelIndex & index);    //设置DBTree item点云与切割按钮的禁用与否
    void setDlgClipPbnEnable(const QModelIndex & index);    //设置切割对话框中的切割按键禁用与否

    void setActProjLineEnable(bool enable);     //设置投影按钮能否获得
    void setActProjXOYEnable(bool enable);          //
    void setActProjXOZEnable(bool enable);
    void setActProjYOZEnable(bool enable);
    void setActOverLookProjEnable(bool enable);
    void slotOn_mouseMove_SceneCoordinate(QPoint point);    //鼠标追踪获得GraphicScene坐标

    void getClosedXOYLabel(bool enable);        //接受是否关闭XOY图标
    void getClosedScale(bool enable);           //接受是否关闭比例尺

    void sendCurrentScaleToGraphicView();               //发送当前比例尺到GraphicView

public:
    std::vector<MyCloudList> pointCloudList;        //加载的点云集

signals:
    void sendOrignalCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr incloud);
    void sendStr2Console(QString text);         //发送消息到Console
    void sendClipPlane(vtkPlaneWidget *plane);          //发送切割面方程
    void sendDlgClipPbnEnable(bool enable);     // 发送给切割Dlg对话框切割按钮获得与否

};
