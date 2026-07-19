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
#include "CloudLayer.h"
#include "CloudLoader.h"
#include "MyGraphicsView.h"
#include "ProjectDocument.h"
#include "TaskManager.h"
#include "WorkspaceStore.h"
#include "WorkspaceSnapshot.h"
#include "EntityTreeModel.h"
#include "EntityWriterRegistry.h"
#include "RendererRegistry.h"
#include "CloudProjection.h"

#include <QtWidgets/QMainWindow>
#include <QDockWidget>
#include <QWidget>
#include <QLabel>
#include <QDebug>
#include <QFutureWatcher>
#include <QHash>
#include <QPoint>
#include <QPointer>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>
#include <optional>

#include <vtkPlaneWidget.h>

class FAMPController;
class QAction;
class QActionGroup;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QMenu;
class QMessageBox;
class QProgressBar;
class QTimer;
class QToolButton;
class QToolBar;
class QTreeWidget;

struct MyCloudList
{
    int id;
    famp::cloud::CloudLayer layer;
    vtkActor * cloudactor;
    vtkActor * AABBactor;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = Q_NULLPTR);
    ~MainWindow() override;

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
    famp::workspace::WorkspaceStore * workspaceStore;
    famp::presentation::EntityTreeModel * model;
    famp::workspace::EntityWriterRegistry entityWriters;
    famp::workspace::RendererRegistry entityRenderers;
    QTreeWidget * entityProperties;
    bool syncingWorkspaceEntity;
    bool syncingGraphicsEntities;
    bool syncingMeasurementEntities;
    bool isAABB;    //是否显示AABB
    MyItem *myItem;
    FAMPController * controller;
    QMenu * recentFilesMenu;
    QStringList recentFiles;
    QAction * newProjectAction;
    QAction * openProjectAction;
    QAction * saveProjectAction;
    QAction * saveProjectAsAction;
    QAction * exportReportAction;
    QTimer * autosaveTimer;
    QString currentProjectPath;
    QMenu * toolsMenu;
    QMenu * editMenu;
    QAction * setProjectCrsAction;
    QAction * coordinateConverterAction;
    QAction * cloudCoordinateAction;
    QAction * reprojectCloudAction;
    QAction * archaeologyMetadataAction;
    QAction * controlPointsAction;
    QAction * terrainAnalysisAction;
    QAction * cutFillAction;
    QAction * cloudProfileAction;
    QActionGroup * measurementActionGroup;
    QAction * distanceMeasureAction;
    QAction * areaMeasureAction;
    QAction * angleMeasureAction;
    QAction * clearMeasurementsAction;
    QAction * cloudDisplaySettingsAction;
    QAction * preprocessCloudAction;
    QAction * cropCloudAction;
    QAction * registerCloudAction;
    QAction * saveSelectedEntityAction;
    QAction * newEntityGroupAction;
    QAction * renameEntityAction;
    QAction * toggleEntityAction;
    QAction * lockEntityAction;
    QAction * zoomEntityAction;
    QAction * undoGraphicsAction;
    QAction * redoGraphicsAction;
    QLabel * crsStatusLabel;
    QString projectCrs;
    bool projectDirty;
    bool loadingProject;
    QFutureWatcher<famp::cloud::LoadResult> * cloudLoadWatcher;
    QProgressBar * cloudLoadProgress;
    QToolButton * cloudLoadCancelButton;
    famp::tasks::TaskManager * taskManager;
    famp::tasks::Handle cloudLoadTask;
    QStringList pendingCloudFiles;
    QStringList cloudLoadFailurePaths;
    QStringList cloudLoadFailureMessages;
    QString currentCloudLoadPath;
    QString cloudLoadProjectPath;
    int cloudLoadTotal;
    int cloudLoadCompleted;
    int cloudLoadSucceeded;
    bool cloudLoadBusy;
    bool cloudLoadProjectBatch;
    bool cloudLoadProjectRecovery;
    bool cloudLoadCancelled;
    QHash<QString, famp::project::CloudReference> projectCloudReferences;
    QVector<famp::measurement::Record3D> pendingProjectMeasurements3d;
    QJsonObject pendingWorkspaceState;
    QString pendingProfileLayerId;
    QString pendingProfileSourceCrs;
    QString pendingProfileCrsDescription;
    QString pendingProfileHorizontalUnitName;
    double pendingProfileHorizontalUnitToMetre = 1.0;
    const pcl::PointCloud<pcl::PointXYZRGB>* pendingProfilePointCloud = nullptr;
    famp::cloud::SpatialReference pendingProfileSpatial;
    famp::workspace::EntityId activeVtkSourceId;
    famp::projection::Workflow projectionWorkflow;
    QToolBar * archaeologyWorkflowBar;
    QVector<QToolButton*> archaeologyWorkflowSteps;
    QLabel * archaeologyWorkflowContext;
    QPointer<QMessageBox> projectionDecisionDialog;
    std::optional<QPoint> lastProjectionDecisionPosition;

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
    bool beginCloudLoadBatch(const QStringList& paths,
                             bool projectBatch = false,
                             const QString& projectPath = QString(),
                             bool projectRecovery = false);
    void startNextCloudLoad();
    void integrateLoadedCloud(const famp::cloud::LoadResult& result);
    bool integrateDerivedCloud(
        const MyCloudList& source,
        const QString& requestedName,
        const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& points,
        const famp::cloud::SpatialReference& spatial,
        const QString& crs,
        const famp::cloud::CloudAttributes& attributes,
        famp::workspace::Provenance provenance,
        bool hideSource = true,
        famp::workspace::EntityId* resultId = nullptr);
    bool integrateDerivedEntity(
        const famp::workspace::EntityId& sourceId,
        famp::workspace::WorkspaceEntity entity,
        famp::workspace::EntityId* resultId = nullptr);
    void finishCloudLoadBatch();
    void setCloudLoadUiBusy(bool busy);
    bool selectedCloudData(MyCloudList& cloud, QString* path = nullptr) const;
    bool applyCloudLayerState(
        const QString& layerId,
        const famp::cloud::CloudLayer& state);
    bool applyCloudMetadataState(
        const QString& layerId,
        const famp::cloud::CloudLayer& state);
    void updateCloudData(const MyCloudList& cloud);
    void initializeEntityWorkspace();
    void applyInitialDockLayout();
    famp::workspace::EntityId ensureWorkspaceSystemGroup(
        const QString& role,
        const QString& name);
    void synchronizeGraphicsEntities();
    void synchronizeMeasurementEntities();
    void updateEntityProperties();
    void clearEntitySelectionHighlight();
    void highlightWorkspaceEntity(const QModelIndex& index);
    void synchronizeWorkspaceEntity(
        const famp::workspace::EntityId& id,
        const QVector<int>& roles = {});
    famp::workspace::EntityId entityIdForCloud(
        const MyCloudList& cloud) const;
    QVector<famp::workspace::EntityId> selectedEntityIds() const;
    void showEntityContextMenu(const QPoint& position);
    bool saveWorkspaceEntity(const famp::workspace::EntityId& id);
    void updateCloudToolActions();
    void initializeArchaeologyWorkflowGuide();
    void synchronizeProjectionWorkflowFromSelection();
    void updateProjectionActions();
    void updateArchaeologyWorkflowGuide();
    void clearTransientProjectionPreview();
    void closeProjectionDecisionDialog(bool clearPreview = true);
    void recordProjectionDrawing(famp::projection::Plane plane);
    bool hasArchaeologyDrawingInTree(
        famp::projection::Plane plane) const;
    QStringList missingArchaeologyCanvasDrawings() const;
    QStringList missingArchaeologyTreeDrawings() const;
    QStringList missingArchaeologyDrawings() const;
    bool archaeologyDrawingWorkflowComplete() const;
    QPoint projectionDecisionPopupPosition(const QSize& dialogSize) const;
    std::optional<famp::projection::Plane> projectionPlaneForEntity(
        const famp::workspace::WorkspaceEntity& entity) const;
    bool integrateProjectionPreview();
    void initializeRecentFilesMenu();
    void addRecentFile(const QString& path);
    void updateRecentFilesMenu();
    void initializeProjectActions();
    void initializeCrsActions();
    void initializeUndoActions();
    bool currentProjectDocument(
        famp::project::Document& document,
        QString* errorMessage = nullptr,
        const QHash<famp::workspace::EntityId, QString>& assetOverrides = {}) const;
    bool prepareProjectAssets(
        const QString& projectPath,
        QHash<famp::workspace::EntityId, QString>& assetOverrides,
        QString* errorMessage = nullptr) const;
    void commitProjectAssets(
        const QHash<famp::workspace::EntityId, QString>& assetOverrides);
    QString recoveryProjectPath() const;
    bool saveProject(bool forceSaveAs);
    bool saveProjectToPath(const QString& path);
    bool loadProjectFromPath(const QString& path, bool isRecovery = false);
    void relocateMissingProjectClouds(famp::project::Document& document,
                                      const QString& projectPath);
    bool restoreWorkspaceAnalysisEntities(
        const famp::workspace::WorkspaceSnapshot& snapshot,
        QStringList& warnings);
    bool maybeSaveCurrentProject();
    void removeCloudFromWorkspace(const MyCloudList& cloud);
    void clearWorkspace();
    void markProjectDirty();
    void updateWindowTitle();
    void updateCrsStatus();
    void applyProjectCrs(const QString& crs);
    void removeRecoveryProject();
    void checkForRecoveryProject();
    void generateCloudProfile(const QString& layerId,
                              const QVector3D& localStart,
                              const QVector3D& localEnd,
                              const QString& sourceCrs,
                              const QString& crsDescription,
                              const QString& horizontalUnitName,
                              double horizontalUnitToMetre);

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
    void slotCloudLoadFinished();
    void slotCloudDisplaySettings();
    void slotPreprocessCloud();
    void slotCropCloud();
    void slotRegisterCloud();
    void slotIntegratePlaneClip(
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud,
        QVector<qint64> sourceIndices,
        QVector3D planeOrigin,
        QVector3D planeNormal,
        double threshold);
    void slotHandleProjectedCloudPreview(
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud,
        famp::projection::Plane plane);
    void slotAutoDrawProjection();
    void slotNewProject();
    void slotOpenProject();
    void slotSaveProject();
    void slotSaveProjectAs();
    void slotExportArchaeologyReport();
    void slotAutosaveProject();
    void slotSetProjectCrs();
    void slotOpenCoordinateConverter();
    void slotOpenCloudCoordinateViewer();
    void slotReprojectCloud();
    void slotEditArchaeologyMetadata();
    void slotEditControlPoints();
    void slotGenerateTerrain();
    void slotCalculateCutFill();
    void slotStartCloudProfile();
    void slotShowQuickStart();
    void slotShowShortcuts();
    void slotShowAbout();

    void slotOn_treeView_clicked(const QModelIndex & index);    //点击DB Tree项目
    void slotOn_actDelete_triggered();              //DB Tree 删除项
    void slotSaveSelectedEntity();
    void slotCreateEntityGroup();
    void slotRenameEntity();
    void slotToggleSelectedEntities();
    void slotLockSelectedEntities();
    void slotZoomSelectedEntity();

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
