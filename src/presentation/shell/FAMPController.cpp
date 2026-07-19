/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 中介者控制器 — 统一管理信号/槽连接
 *****************************************************************/

#include "FAMPController.h"
#include "MainWindow.h"
#include "MyVTK.h"
#include "MyGraphicsView.h"

#include <QAction>

#include <vtkActor.h>
#include <vtkPlaneWidget.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

FAMPController::FAMPController(MainWindow* mainWindow, MyVTK* myVTK, MyGraphicsView* graphicsView, QObject* parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_myVTK(myVTK)
    , m_graphicsView(graphicsView)
{
}

void FAMPController::initializeConnections(const Ui::MainWindowClass& ui, QAbstractItemModel* model, QWidget* centerDock, QComboBox* scaleCombox)
{
    (void)centerDock;
    (void)model;

    // ── Dock widget visibility & floating ─────────────────────────────

    // DBTree 窗口可见
    connect(ui.actDBTreeVisible, SIGNAL(triggered(bool)), m_mainWindow, SLOT(slotOn_actDBTreeVisible_triggered(bool)));
    connect(ui.dockWidget1, SIGNAL(visibilityChanged(bool)), m_mainWindow, SLOT(slotOn_actDBTreeVisible_visibilityChanged(bool)));
    // DBTree 窗口浮动
    connect(ui.actDBTreeFloat, SIGNAL(triggered(bool)), m_mainWindow, SLOT(slotOn_actDBTreeFloat_triggered(bool)));
    connect(ui.dockWidget1, SIGNAL(topLevelChanged(bool)), m_mainWindow, SLOT(slotOn_actDBTreeFloat_topLevelChanged(bool)));

    // GraphView 窗口可见
    connect(ui.acGraViewVisible, SIGNAL(triggered(bool)), m_mainWindow, SLOT(slotOn_actGraViewVisible_triggered(bool)));
    connect(ui.dockWidget2, SIGNAL(visibilityChanged(bool)), m_mainWindow, SLOT(slotOn_actGraViewVisible_visibilityChanged(bool)));
    // GraphView 窗口浮动
    connect(ui.actGRaViewFloat, SIGNAL(triggered(bool)), m_mainWindow, SLOT(slotOn_actGraViewFloat_triggered(bool)));
    connect(ui.dockWidget2, SIGNAL(topLevelChanged(bool)), m_mainWindow, SLOT(slotOn_actGraViewFloat_topLevelChanged(bool)));

    // VTK 窗口可见
    connect(ui.actVTKVisible, SIGNAL(triggered(bool)), m_mainWindow, SLOT(slotOn_actVTKVisible_triggered(bool)));

    // Console 窗口可见
    connect(ui.actConsoleVisible, SIGNAL(triggered(bool)), m_mainWindow, SLOT(slotOn_actConsoleVisible_triggered(bool)));
    connect(ui.dockWidget3, SIGNAL(visibilityChanged(bool)), m_mainWindow, SLOT(slotOn_actConsoleVisible_visibilityChanged(bool)));
    // Console 窗口浮动
    connect(ui.actConsoleFloat, SIGNAL(triggered(bool)), m_mainWindow, SLOT(slotOn_actConsoleFloat_triggered(bool)));
    connect(ui.dockWidget3, SIGNAL(topLevelChanged(bool)), m_mainWindow, SLOT(slotOn_actConsoleFloat_topLevelChanged(bool)));

    // ── 视图控制 ─────────────────────────────────────────────────────

    connect(ui.actFullScreen, SIGNAL(triggered()), m_mainWindow, SLOT(slotFullScreen()));
    connect(ui.actFrontView, SIGNAL(triggered()), m_mainWindow, SLOT(slotFrontView()));
    connect(ui.actTopView, SIGNAL(triggered()), m_mainWindow, SLOT(slotTopView()));
    connect(ui.actBackView, SIGNAL(triggered()), m_mainWindow, SLOT(slotBackView()));
    connect(ui.actLeftView, SIGNAL(triggered()), m_mainWindow, SLOT(slotLeftView()));
    connect(ui.actRightView, SIGNAL(triggered()), m_mainWindow, SLOT(slotRightView()));
    connect(ui.actBottomView, SIGNAL(triggered()), m_mainWindow, SLOT(slotBottomView()));

    // ── 点云加载 ─────────────────────────────────────────────────────

    connect(ui.actOpenCloud, SIGNAL(triggered()), m_mainWindow, SLOT(slotOpenCloud()));
    connect(m_mainWindow, SIGNAL(sendOrignalCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr)), m_myVTK, SLOT(getOrignalCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr)));

    // ── DB Tree ──────────────────────────────────────────────────────

    connect(ui.treeView, SIGNAL(clicked(QModelIndex)), m_mainWindow, SLOT(slotOn_treeView_clicked(QModelIndex)));
    connect(ui.actDelete, SIGNAL(triggered()), m_mainWindow, SLOT(slotOn_actDelete_triggered()));
    connect(ui.actAABB, SIGNAL(triggered(bool)), m_mainWindow, SLOT(slotOn_actAABB_triggered(bool)));

    // ── Console 消息 ─────────────────────────────────────────────────

    connect(m_mainWindow, SIGNAL(sendStr2Console(QString)), m_mainWindow, SLOT(slotGetStr2Console(QString)));

    // ── 切割平面 ─────────────────────────────────────────────────────

    connect(ui.actRandomPlane, SIGNAL(triggered(bool)), m_mainWindow, SLOT(slotOn_actRandomPlane_triggered(bool)));
    connect(ui.actVerticalPlane, SIGNAL(triggered()), m_mainWindow, SLOT(slotOn_actVerticalPlane_triggered()));
    connect(ui.actHorizonalPlane, SIGNAL(triggered(bool)), m_mainWindow, SLOT(slotOn_actHorizonalPlane_triggered(bool)));
    connect(m_mainWindow, SIGNAL(sendClipPlane(vtkPlaneWidget*)), m_myVTK, SLOT(getClipPlane(vtkPlaneWidget*)));
    connect(m_myVTK, SIGNAL(sendGetDBItem()), m_mainWindow, SLOT(DBTreeSendVTKItemCloud()));
    connect(m_myVTK,
            SIGNAL(sendClipCloudResult(pcl::PointCloud<pcl::PointXYZRGB>::Ptr,QVector<qint64>,QVector3D,QVector3D,double)),
            m_mainWindow,
            SLOT(slotIntegratePlaneClip(pcl::PointCloud<pcl::PointXYZRGB>::Ptr,QVector<qint64>,QVector3D,QVector3D,double)));

    // DBTree item 点击 → 按钮启用/禁用
    connect(ui.treeView, SIGNAL(clicked(QModelIndex)), m_mainWindow, SLOT(setClipButtonEnable(QModelIndex)));
    connect(ui.treeView, SIGNAL(clicked(QModelIndex)), m_mainWindow, SLOT(setDlgClipPbnEnable(QModelIndex)));
    connect(m_mainWindow, SIGNAL(sendDlgClipPbnEnable(bool)), m_myVTK, SLOT(getPbnClipEnable(bool)));

    // ── 控制台消息中继 ───────────────────────────────────────────────

    connect(m_myVTK, SIGNAL(sendStrFromVTK2Console(QString)), m_mainWindow, SLOT(slotGetStr2Console(QString)));
    connect(m_graphicsView, SIGNAL(sendStrFromGraphicView2Console(QString)), m_mainWindow, SLOT(slotGetStr2Console(QString)));

    // ── GraphicsView 绘图动作 ────────────────────────────────────────

    connect(ui.actMiGe, SIGNAL(triggered(bool)), m_graphicsView, SLOT(slotOn_actMiGe_triggered(bool)));
    connect(m_graphicsView, &MyGraphicsView::metricGridVisibilityChanged,
            ui.actMiGe, &QAction::setChecked);
    connect(ui.actCalibrateMetricGrid, SIGNAL(triggered()),
            m_graphicsView, SLOT(slotCalibrateMetricGrid()));
    connect(ui.actpoints, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actPoints_triggered()));

    // ── 投影使能信号 ─────────────────────────────────────────────────


    // ── 投影连线 ─────────────────────────────────────────────────────

    connect(ui.actProjLine, SIGNAL(triggered()),
            m_mainWindow, SLOT(slotAutoDrawProjection()));
    connect(m_graphicsView, SIGNAL(mouseMovePoint(QPoint)), m_mainWindow, SLOT(slotOn_mouseMove_SceneCoordinate(QPoint)));

    // ── Actor 传递 ───────────────────────────────────────────────────

    connect(m_graphicsView, SIGNAL(sendActor(vtkActor*)), m_myVTK, SLOT(getActorFromGraphicView(vtkActor*)));

    // ── 投影面按钮 ───────────────────────────────────────────────────

    connect(ui.actProjYOZ, SIGNAL(triggered()), m_myVTK, SLOT(slotActProjYOZ_triggered()));
    connect(ui.actProjXOY, SIGNAL(triggered()), m_myVTK, SLOT(slotActProjXOY_triggered()));
    connect(ui.actProjXOZ, SIGNAL(triggered()), m_myVTK, SLOT(slotActProjXOZ_triggered()));
    connect(ui.actOverLookProj, SIGNAL(triggered()), m_myVTK, SLOT(slotActOverLookProj_triggered()));

    // ── GraphicsView 编辑动作 ────────────────────────────────────────

    connect(ui.actDeleteItem, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actDeleteItem_triggered()));
    connect(ui.actClearScene, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actClearScene_triggered()));

    // ── GraphicsView 图元操作 ────────────────────────────────────────

    connect(ui.actGroup, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actGroup_triggered()));
    connect(ui.actBreak, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actBreak_triggered()));
    connect(ui.actMoveUp, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actMoveUp_triggered()));
    connect(ui.actMoveDown, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actMoveDown_triggered()));
    connect(ui.actMoveLeft, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actMoveLeft_triggered()));
    connect(ui.actMoveRight, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actMoveRight_triggered()));
    connect(ui.actRotateLeft, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actRotateLeft_triggered()));
    connect(ui.actRotateRight, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actRotateRight_triggered()));
    connect(m_graphicsView, &MyGraphicsView::selectionAvailabilityChanged,
            ui.actRotateLeft, &QAction::setEnabled);
    connect(m_graphicsView, &MyGraphicsView::selectionAvailabilityChanged,
            ui.actRotateRight, &QAction::setEnabled);
    ui.actRotateLeft->setEnabled(false);
    ui.actRotateRight->setEnabled(false);
    connect(ui.actEditFront, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actEditFront_triggered()));
    connect(ui.acEditBack, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actEditBack_triggered()));
    connect(ui.actSave, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actSave_triggered()));
    connect(ui.actCompass, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actCompass_triggered()));
    connect(ui.actText, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actText_triggered()));

    // ── XOY 标签 & 比例尺可见性 ─────────────────────────────────────

    connect(m_graphicsView, SIGNAL(sendClosedXOYLabel(bool)), m_mainWindow, SLOT(getClosedXOYLabel(bool)));
    connect(m_graphicsView, SIGNAL(sendClosedScale(bool)), m_mainWindow, SLOT(getClosedScale(bool)));

    // ── 裁剪对话框可见性 ─────────────────────────────────────────────

    connect(m_graphicsView, SIGNAL(sendDlgClipVisible(bool)), m_myVTK, SLOT(setDlgClipVisible(bool)));

    // ── 比例尺联动 ───────────────────────────────────────────────────

    connect(scaleCombox, SIGNAL(currentIndexChanged(int)), m_graphicsView, SLOT(getScaleComBoxCurrentIndexChanged(int)));
    connect(m_graphicsView, SIGNAL(sendGetCurrentScale()), m_mainWindow, SLOT(sendCurrentScaleToGraphicView()));
    connect(m_graphicsView, SIGNAL(sendReDraw(ScaleType)), m_graphicsView, SLOT(getReDraw(ScaleType)));
    connect(m_graphicsView, SIGNAL(sendScaleOffset(QPointF)), m_graphicsView, SLOT(getScaleOffset(QPointF)));

    // 信号连接完成后同步下拉框初始值，避免界面与内部比例尺状态不一致。
    m_graphicsView->getScaleComBoxCurrentIndexChanged(scaleCombox->currentIndex());

    // ── 出图模板 ─────────────────────────────────────────────────────

    connect(ui.actPlotTab, SIGNAL(triggered()), m_graphicsView, SLOT(slotOn_actPlotTab_triggered()));

    // ── 帮助与版本信息 ──────────────────────────────────────────────

    connect(ui.actQuickStart, SIGNAL(triggered()), m_mainWindow, SLOT(slotShowQuickStart()));
    connect(ui.actShortcuts, SIGNAL(triggered()), m_mainWindow, SLOT(slotShowShortcuts()));
    connect(ui.actAbout, SIGNAL(triggered()), m_mainWindow, SLOT(slotShowAbout()));
}
