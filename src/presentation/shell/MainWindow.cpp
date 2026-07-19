/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 主窗口 — UI 编排、点云加载、DB Tree 管理
 *****************************************************************/

#include "MainWindow.h"
#include "ArchaeologyMetadataDialog.h"
#include "ArchaeologyReport.h"
#include "FAMPController.h"
#include "CrsService.h"
#include "CloudDisplaySettings.h"
#include "CloudCrop.h"
#include "CloudProcessing.h"
#include "CloudRegistration.h"
#include "CloudReprojection.h"
#include "ControlPointDialog.h"
#include "CutFillAnalysis.h"
#include "CutFillDialog.h"
#include "CutFillIO.h"
#include "FileIO.h"
#include "GraphicsUndoCommands.h"
#include "HelpContent.h"
#include "LasLoader.h"
#include "PcdLoader.h"
#include "ProcessingRecipe.h"
#include "ProfileAnalysis.h"
#include "ProfileDialog.h"
#include "ProfileIO.h"
#include "RecentFiles.h"
#include "TerrainAnalysis.h"
#include "TerrainDialog.h"
#include "TerrainIO.h"
#include "WorkspaceSnapshot.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFuture>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPixmap>
#include <QPushButton>
#include <QProgressBar>
#include <QProgressDialog>
#include <QFileInfo>
#include <QCoreApplication>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QSettings>
#include <QSet>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QScreen>
#include <QSplitter>
#include <QStandardPaths>
#include <QSpinBox>
#include <QTime>
#include <QTextBrowser>
#include <QTimer>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <QToolButton>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <pcl/pcl_config.h>
#include <vtkVersion.h>
#include <vtkMapper.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkProperty.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

Q_DECLARE_METATYPE(MyCloudList)

static int iCount = 0;      //记录点云的ID号

namespace
{
constexpr std::array<famp::projection::Plane, 3>
    RequiredArchaeologyPlanes{
        famp::projection::Plane::Overlook,
        famp::projection::Plane::XOZ,
        famp::projection::Plane::YOZ};

bool isRequiredArchaeologyPlane(famp::projection::Plane plane)
{
    return std::find(RequiredArchaeologyPlanes.cbegin(),
                     RequiredArchaeologyPlanes.cend(), plane)
        != RequiredArchaeologyPlanes.cend();
}

bool isArchaeologyProfilePlane(famp::projection::Plane plane)
{
    return plane == famp::projection::Plane::XOZ
        || plane == famp::projection::Plane::YOZ;
}

QString archaeologyPlaneName(famp::projection::Plane plane)
{
    return plane == famp::projection::Plane::Overlook
        ? QStringLiteral("俯视")
        : famp::projection::axisName(plane);
}

ProjectType projectTypeForPlane(famp::projection::Plane plane)
{
    switch (plane)
    {
    case famp::projection::Plane::YOZ:
        return YOZ;
    case famp::projection::Plane::XOZ:
        return XOZ;
    case famp::projection::Plane::XOY:
        return XOY;
    case famp::projection::Plane::Overlook:
        return OLXOY;
    }
    return NONE;
}

void setCenteredSpatialMatrix(
    vtkMatrix4x4* matrix,
    const famp::cloud::SpatialReference& spatial)
{
    matrix->Identity();
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            matrix->SetElement(
                row, column,
                spatial.transform[static_cast<std::size_t>(
                    row * 4 + column)]);
        }
    }
}

bool transformLayerMeasurements(
    const QString& layerId,
    const famp::cloud::SpatialReference& before,
    const famp::cloud::SpatialReference& after,
    const QVector<famp::measurement::Record3D>& input,
    QVector<famp::measurement::Record3D>& output,
    QString* errorMessage)
{
    QVector<famp::measurement::Record3D> candidate = input;
    for (auto& record : candidate)
    {
        if (record.layerId.compare(layerId, Qt::CaseInsensitive) != 0)
            continue;
        for (QVector3D& point : record.points)
        {
            const famp::cloud::Point3d source{
                point.x(), point.y(), point.z()};
            famp::cloud::Point3d transformed;
            QString transformError;
            if (!famp::control::transformDisplayPoint(
                    before, after, source, transformed, &transformError)
                || std::abs(transformed[0]) > std::numeric_limits<float>::max()
                || std::abs(transformed[1]) > std::numeric_limits<float>::max()
                || std::abs(transformed[2]) > std::numeric_limits<float>::max())
            {
                if (errorMessage)
                {
                    *errorMessage = transformError.isEmpty()
                        ? QStringLiteral("控制点变换后的测量坐标超出安全范围。")
                        : transformError;
                }
                return false;
            }
            point = QVector3D(
                static_cast<float>(transformed[0]),
                static_cast<float>(transformed[1]),
                static_cast<float>(transformed[2]));
        }
        QString validationError;
        if (!famp::measurement::validateRecord3D(record, &validationError))
        {
            if (errorMessage)
                *errorMessage = validationError;
            return false;
        }
    }
    output = candidate;
    if (errorMessage)
        errorMessage->clear();
    return true;
}

QString derivedEntityName(const QString& sourceName, const QString& suffix)
{
    const QFileInfo info(sourceName.trimmed());
    const QString base = info.completeSuffix().isEmpty()
        ? sourceName.trimmed() : info.completeBaseName();
    return (base.isEmpty() ? QStringLiteral("未命名点云") : base)
        + suffix;
}

famp::workspace::Matrix4d workspaceMatrix(const Eigen::Matrix4d& matrix)
{
    famp::workspace::Matrix4d values{};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            values[static_cast<std::size_t>(row * 4 + column)] =
                matrix(row, column);
        }
    }
    return values;
}

QJsonObject sourceSnapshot(const famp::workspace::WorkspaceEntity& entity,
                           const MyCloudList& cloud)
{
    return QJsonObject{
        {QStringLiteral("entityId"),
         entity.id.toString(QUuid::WithoutBraces)},
        {QStringLiteral("name"), entity.name},
        {QStringLiteral("layerId"), cloud.layer.id},
        {QStringLiteral("pointCount"),
         static_cast<double>(cloud.layer.pointCount())},
        {QStringLiteral("crs"), cloud.layer.crs}};
}

struct TerrainTaskOutput
{
    famp::terrain::Result analysis;
    QStringList savedPaths;
    QStringList warnings;
    QString error;
    bool sidecarSaved = false;
    bool saveRequested = false;
    bool cancelled = false;

    bool succeeded() const
    {
        return analysis.succeeded() && (!saveRequested || sidecarSaved)
            && error.isEmpty() && !cancelled;
    }
};

struct ProfileTaskOutput
{
    famp::profile::Result analysis;
    QStringList savedPaths;
    QStringList warnings;
    QString error;
    bool sidecarSaved = false;
    bool saveRequested = false;
    bool cancelled = false;

    bool succeeded() const
    {
        return analysis.succeeded() && (!saveRequested || sidecarSaved)
            && error.isEmpty() && !cancelled;
    }
};

struct CutFillTaskOutput
{
    famp::cutfill::Result analysis;
    QStringList savedPaths;
    QStringList warnings;
    QString error;
    bool sidecarSaved = false;
    bool saveRequested = false;
    bool cancelled = false;

    bool succeeded() const
    {
        // Saving the required sidecar already performs the complete result
        // consistency check. Avoid another full-grid pass on the GUI thread.
        return !analysis.cancelled && analysis.error.isEmpty()
            && (!saveRequested || sidecarSaved)
            && error.isEmpty() && !cancelled;
    }
};

struct GraphicsEntityPayload
{
    QString itemId;
    famp::graphics::ItemHandle handle;
};
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , myVTK(nullptr)
    , workspaceStore(nullptr)
    , model(nullptr)
    , entityProperties(nullptr)
    , syncingWorkspaceEntity(false)
    , syncingGraphicsEntities(false)
    , syncingMeasurementEntities(false)
    , recentFilesMenu(nullptr)
    , newProjectAction(nullptr)
    , openProjectAction(nullptr)
    , saveProjectAction(nullptr)
    , saveProjectAsAction(nullptr)
    , exportReportAction(nullptr)
    , autosaveTimer(nullptr)
    , toolsMenu(nullptr)
    , editMenu(nullptr)
    , setProjectCrsAction(nullptr)
    , coordinateConverterAction(nullptr)
    , cloudCoordinateAction(nullptr)
    , reprojectCloudAction(nullptr)
    , archaeologyMetadataAction(nullptr)
    , controlPointsAction(nullptr)
    , terrainAnalysisAction(nullptr)
    , cutFillAction(nullptr)
    , cloudProfileAction(nullptr)
    , measurementActionGroup(nullptr)
    , distanceMeasureAction(nullptr)
    , areaMeasureAction(nullptr)
    , angleMeasureAction(nullptr)
    , clearMeasurementsAction(nullptr)
    , cloudDisplaySettingsAction(nullptr)
    , preprocessCloudAction(nullptr)
    , cropCloudAction(nullptr)
    , registerCloudAction(nullptr)
    , saveSelectedEntityAction(nullptr)
    , newEntityGroupAction(nullptr)
    , renameEntityAction(nullptr)
    , toggleEntityAction(nullptr)
    , lockEntityAction(nullptr)
    , zoomEntityAction(nullptr)
    , undoGraphicsAction(nullptr)
    , redoGraphicsAction(nullptr)
    , crsStatusLabel(nullptr)
    , projectDirty(false)
    , loadingProject(false)
    , cloudLoadWatcher(nullptr)
    , cloudLoadProgress(nullptr)
    , cloudLoadCancelButton(nullptr)
    , taskManager(nullptr)
    , cloudLoadTotal(0)
    , cloudLoadCompleted(0)
    , cloudLoadSucceeded(0)
    , cloudLoadBusy(false)
    , cloudLoadProjectBatch(false)
    , cloudLoadProjectRecovery(false)
    , cloudLoadCancelled(false)
    , archaeologyWorkflowBar(nullptr)
    , archaeologyWorkflowContext(nullptr)
{
    ui.setupUi(this);

    this->resize(1920, 1080);
    setAcceptDrops(true);

    myCloud = NULL;
    inCloud = NULL;
    myItem = NULL;

    isAABB = false; //是否显示AABB

    initializeEntityWorkspace();

    //允许嵌套dock
    setDockNestingEnabled(true);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

    // Content tree on the left, matching the CloudCompare-style workspace.
    ui.dockWidget1->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, ui.dockWidget1);

    // Archaeological drafting canvas on the right. The deferred layout pass
    // below gives it the same initial width as the central VTK viewport.
    ui.dockWidget2->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, ui.dockWidget2);
    ui.dockWidget2->setWindowTitle("");

    //Console
    ui.dockWidget3->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    this->resizeDocks({ ui.dockWidget3 }, {200}, Qt::Vertical);
    addDockWidget(Qt::BottomDockWidgetArea, ui.dockWidget3);

    // CenterView VTK. Reuse the central widget created by Qt Designer so the
    // QMainWindow keeps its menu bars, toolbars, and dock layout intact on
    // Windows. Replacing the central widget after setupUi() lets the QVTK
    // OpenGL surface cover sibling UI in some drivers.
    centerDock = ui.centralWidget;
    delete ui.openGLWidget;
    ui.openGLWidget = nullptr;
    QVBoxLayout* centerLayout = new QVBoxLayout(centerDock);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);
    myVTK = new MyVTK(centerDock);
    centerLayout->addWidget(myVTK);

    //this->setCursor(Qt::WaitCursor);

    //ui.statusBar->addWidget(ui.graphicsView->labelScene); //显示坐标信息
    addXOYLabel();                  //在GraphicsView左上方添加XOY坐标

    //添加比例尺
    addScaleWidget();

    // FAMPController 中介者：统一管理所有信号/槽连接
    controller = new FAMPController(this, myVTK, ui.graphicsView, this);
    controller->initializeConnections(ui, model, centerDock, scaleCombox);
    connect(ui.graphicsView, &MyGraphicsView::selectionAvailabilityChanged,
            this, [this](bool) { updateArchaeologyWorkflowGuide(); });
    initializeProjectActions();
    initializeRecentFilesMenu();
    initializeCrsActions();
    initializeUndoActions();
    initializeArchaeologyWorkflowGuide();
    connect(myVTK, &MyVTK::sendProjectedCloudPreview,
            this, &MainWindow::slotHandleProjectedCloudPreview);
    updateProjectionActions();

    taskManager = new famp::tasks::TaskManager(this);
    cloudLoadWatcher = new QFutureWatcher<famp::cloud::LoadResult>(this);
    connect(cloudLoadWatcher,
            &QFutureWatcher<famp::cloud::LoadResult>::finished,
            this,
            &MainWindow::slotCloudLoadFinished);
    cloudLoadProgress = new QProgressBar(this);
    cloudLoadProgress->setObjectName(QStringLiteral("cloudLoadProgress"));
    cloudLoadProgress->setMinimumWidth(180);
    cloudLoadProgress->setMaximumWidth(260);
    cloudLoadProgress->setTextVisible(true);
    cloudLoadProgress->hide();
    statusBar()->addPermanentWidget(cloudLoadProgress);
    cloudLoadCancelButton = new QToolButton(this);
    cloudLoadCancelButton->setText(tr("取消加载"));
    cloudLoadCancelButton->setToolTip(tr("安全取消当前点云及剩余队列"));
    cloudLoadCancelButton->hide();
    statusBar()->addPermanentWidget(cloudLoadCancelButton);
    connect(cloudLoadCancelButton, &QToolButton::clicked, this, [this]() {
        if (!cloudLoadBusy || !cloudLoadTask.isValid())
            return;
        cloudLoadCancelled = true;
        taskManager->requestCancellation(cloudLoadTask.id);
        pendingCloudFiles.clear();
        cloudLoadCancelButton->setEnabled(false);
        statusBar()->showMessage(tr("正在安全取消点云加载…"));
    });
    connect(scaleCombox, &QComboBox::currentTextChanged,
            this, [this]() { markProjectDirty(); });
    connect(ui.graphicsView, &MyGraphicsView::scaleIndexChangedByHistory,
            this, [this](int index) {
                if (scaleCombox->currentIndex() == index)
                    return;
                const QSignalBlocker blocker(scaleCombox);
                scaleCombox->setCurrentIndex(index);
                markProjectDirty();
            });
    connect(ui.graphicsView->commandStack(), &QUndoStack::cleanChanged,
            this, [this](bool clean) {
                if (!clean)
                    markProjectDirty();
            });

    autosaveTimer = new QTimer(this);
    autosaveTimer->setInterval(60 * 1000);
    connect(autosaveTimer, &QTimer::timeout,
            this, &MainWindow::slotAutosaveProject);
    autosaveTimer->start();
    updateWindowTitle();
    QTimer::singleShot(0, this, [this]() {
        applyInitialDockLayout();
        checkForRecoveryProject();
    });

}

MainWindow::~MainWindow()
{
    closeProjectionDecisionDialog();
    if (ui.graphicsView)
    {
        // The workspace keeps shared handles for 2D items. Detach those
        // handles while both the canvas and workflow widgets are still alive;
        // otherwise QObject child destruction order can leave the workspace
        // with pointers to scene items that were already deleted.
        QObject::disconnect(ui.graphicsView, nullptr, this, nullptr);
        ui.graphicsView->clearSceneAndHistory();
    }
    // VTK actors use explicit reference counting and are not QObject
    // children. Release every remaining workspace reference while MyVTK is
    // still alive, otherwise closing a project with loaded clouds leaks the
    // actor and its mapper data until process termination.
    while (!pointCloudList.empty())
        removeCloudFromWorkspace(pointCloudList.back());
    inCloud.reset();
    delete myCloud;
    myCloud = nullptr;
}

void MainWindow::initializeArchaeologyWorkflowGuide()
{
    archaeologyWorkflowBar = new QToolBar(tr("考古制图流程"), this);
    archaeologyWorkflowBar->setObjectName(
        QStringLiteral("archaeologyWorkflowBar"));
    archaeologyWorkflowBar->setMovable(false);
    archaeologyWorkflowBar->setFloatable(false);
    archaeologyWorkflowBar->setAllowedAreas(Qt::TopToolBarArea);

    auto* container = new QWidget(archaeologyWorkflowBar);
    container->setObjectName(QStringLiteral("archaeologyWorkflowNavigator"));
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* outerLayout = new QVBoxLayout(container);
    outerLayout->setContentsMargins(8, 3, 8, 4);
    outerLayout->setSpacing(3);
    auto* stepLayout = new QHBoxLayout;
    stepLayout->setContentsMargins(0, 0, 0, 0);
    stepLayout->setSpacing(4);

    const QStringList stepNames{
        tr("① 选择点云"),
        tr("② 点云准备（可选）"),
        tr("③ 选择制图类型"),
        tr("④ 投影预览"),
        tr("⑤ 三项自动绘图"),
        tr("⑥ 编辑与 A4 出图")};
    const QStringList toolTips{
        tr("在左侧内容列表选择任意可见点云。"),
        tr("可先进行切割、预处理、ICP 配准或重投影。"),
        tr("考古制图必需成果为俯视（沿 Z 轴垂直投影）平面图、XOZ 剖面图和 YOZ 剖面图；XOY 为可选辅助投影。"),
        tr("生成当前所选方向的临时预览；预览不累计，不会自动落盘或加入内容列表。"),
        tr("分别生成俯视、XOZ、YOZ 自动绘图；三项成果必须同时存在于右侧画布和左侧“二维制图”目录。"),
        tr("仅当俯视、XOZ、YOZ 三项自动绘图同时存在于画布和“二维制图”目录时，才可编辑并按 A4 实尺导出。")};

    QFont workflowFont = container->font();
    workflowFont.setWeight(QFont::DemiBold);
    const QFontMetrics workflowMetrics(workflowFont);
    int stepWidth = 0;
    for (const QString& name : stepNames)
        stepWidth = std::max(stepWidth, workflowMetrics.horizontalAdvance(name));
    stepWidth += 28;
    const int stepHeight = workflowMetrics.height() + 14;

    archaeologyWorkflowSteps.clear();
    for (int index = 0; index < stepNames.size(); ++index)
    {
        auto* button = new QToolButton(container);
        button->setObjectName(
            QStringLiteral("archaeologyWorkflowStep%1").arg(index + 1));
        button->setText(stepNames.at(index));
        button->setToolTip(toolTips.at(index));
        button->setFont(workflowFont);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setAutoRaise(false);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setFixedSize(stepWidth, stepHeight);
        archaeologyWorkflowSteps.append(button);
        stepLayout->addWidget(button);
        if (index + 1 < stepNames.size())
        {
            auto* arrow = new QLabel(QStringLiteral("→"), container);
            arrow->setStyleSheet(QStringLiteral("color:#78909c;"));
            arrow->setAlignment(Qt::AlignCenter);
            arrow->setFixedWidth(workflowMetrics.horizontalAdvance(
                QStringLiteral("→")) + 6);
            stepLayout->addWidget(arrow);
        }
    }
    stepLayout->addStretch(1);
    outerLayout->addLayout(stepLayout);

    archaeologyWorkflowContext = new QLabel(container);
    archaeologyWorkflowContext->setObjectName(
        QStringLiteral("archaeologyWorkflowContext"));
    archaeologyWorkflowContext->setFont(workflowFont);
    archaeologyWorkflowContext->setWordWrap(false);
    archaeologyWorkflowContext->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Fixed);
    archaeologyWorkflowContext->setMinimumWidth(0);
    const int contextHeight = workflowMetrics.height() + 10;
    archaeologyWorkflowContext->setFixedHeight(contextHeight);
    archaeologyWorkflowContext->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    outerLayout->addWidget(archaeologyWorkflowContext);
    container->setFixedHeight(stepHeight + contextHeight + 10);
    archaeologyWorkflowBar->addWidget(container);

    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(Qt::TopToolBarArea, archaeologyWorkflowBar);

    connect(archaeologyWorkflowSteps.at(0), &QToolButton::clicked,
            this, [this]() {
                ui.dockWidget1->show();
                ui.treeView->setFocus();
                statusBar()->showMessage(
                    tr("请在左侧内容列表选择一个点云。"), 5000);
            });
    connect(archaeologyWorkflowSteps.at(1), &QToolButton::clicked,
            this, [this]() {
                statusBar()->showMessage(
                    tr("点云准备是可选步骤：可在工具菜单中使用预处理、裁切、ICP 或重投影。"),
                    8000);
            });
    connect(archaeologyWorkflowSteps.at(2), &QToolButton::clicked,
            this, [this]() {
                statusBar()->showMessage(
                    tr("必做成果：俯视平面图、XOZ 剖面图、YOZ 剖面图；XOY 为可选辅助投影。"),
                    8000);
            });
    connect(archaeologyWorkflowSteps.at(3), &QToolButton::clicked,
            this, [this]() {
                if (!projectionWorkflow.hasSource())
                {
                    statusBar()->showMessage(
                        tr("请先在左侧内容列表选择一个源点云。"), 6000);
                    return;
                }
                const famp::projection::Preview* preview =
                    projectionWorkflow.preview();
                statusBar()->showMessage(
                    preview
                        ? tr("当前投影预览：%1。预览不累计，可继续自动绘图或切换方向。")
                              .arg(famp::projection::displayName(
                                  preview->plane))
                        : tr("请选择俯视、XOZ 或 YOZ 投影按钮生成当前预览；预览不累计。"),
                    8000);
            });
    connect(archaeologyWorkflowSteps.at(4), &QToolButton::clicked,
            this, [this]() {
                const famp::projection::Preview* preview =
                    projectionWorkflow.preview();
                if (!preview || !ui.actProjLine->isEnabled())
                {
                    statusBar()->showMessage(
                        tr("自动绘图需要先生成有效投影预览。"), 6000);
                    return;
                }
                if (!isRequiredArchaeologyPlane(preview->plane))
                {
                    statusBar()->showMessage(
                        tr("当前 XOY 是可选辅助投影；必做自动绘图为俯视、XOZ、YOZ。"),
                        8000);
                    return;
                }
                ui.actProjLine->trigger();
            });
    connect(archaeologyWorkflowSteps.at(5), &QToolButton::clicked,
            this, [this]() {
                if (!archaeologyDrawingWorkflowComplete())
                {
                    const QStringList missingCanvas =
                        missingArchaeologyCanvasDrawings();
                    const QStringList missingTree =
                        missingArchaeologyTreeDrawings();
                    statusBar()->showMessage(
                        tr("第⑥步尚未解锁：绘图画布缺 %1；左侧“二维制图”缺 %2。")
                            .arg(missingCanvas.isEmpty()
                                     ? tr("无")
                                     : missingCanvas.join(QStringLiteral("、")),
                                 missingTree.isEmpty()
                                     ? tr("无")
                                     : missingTree.join(QStringLiteral("、"))),
                        9000);
                    return;
                }
                ui.dockWidget2->show();
                ui.graphicsView->setFocus();
                statusBar()->showMessage(
                    tr("在右侧编辑线图，添加指北针与制图信息后使用保存/A4 导出。"),
                    8000);
            });

    updateArchaeologyWorkflowGuide();
}

bool MainWindow::hasArchaeologyDrawingInTree(
    famp::projection::Plane plane) const
{
    if (!workspaceStore)
        return false;

    const ProjectType expectedType = projectTypeForPlane(plane);
    for (const famp::workspace::EntityId& id : workspaceStore->allEntityIds())
    {
        const famp::workspace::WorkspaceEntity* entity =
            workspaceStore->entity(id);
        if (!entity
            || entity->kind != famp::workspace::EntityKind::GraphicsItem)
        {
            continue;
        }
        const famp::workspace::WorkspaceEntity* parent =
            workspaceStore->entity(entity->parentId);
        if (!parent || parent->kind != famp::workspace::EntityKind::Group
            || parent->display.value(QStringLiteral("systemRole")).toString()
                   != QStringLiteral("canvas2d"))
        {
            continue;
        }
        const std::shared_ptr<GraphicsEntityPayload> payload =
            entity->payloadAs<GraphicsEntityPayload>();
        const MyItem* drawing = payload && payload->handle
            ? dynamic_cast<const MyItem*>(payload->handle->item)
            : nullptr;
        if (drawing && drawing->projectionType() == expectedType)
            return true;
    }
    return false;
}

QStringList MainWindow::missingArchaeologyCanvasDrawings() const
{
    QStringList missing;
    for (const famp::projection::Plane plane : RequiredArchaeologyPlanes)
    {
        if (!ui.graphicsView
            || !ui.graphicsView->hasProjectionDrawing(plane))
            missing.append(archaeologyPlaneName(plane));
    }
    return missing;
}

QStringList MainWindow::missingArchaeologyTreeDrawings() const
{
    QStringList missing;
    for (const famp::projection::Plane plane : RequiredArchaeologyPlanes)
    {
        if (!hasArchaeologyDrawingInTree(plane))
            missing.append(archaeologyPlaneName(plane));
    }
    return missing;
}

QStringList MainWindow::missingArchaeologyDrawings() const
{
    QStringList missing;
    for (const famp::projection::Plane plane : RequiredArchaeologyPlanes)
    {
        if (!ui.graphicsView
            || !ui.graphicsView->hasProjectionDrawing(plane)
            || !hasArchaeologyDrawingInTree(plane))
        {
            missing.append(archaeologyPlaneName(plane));
        }
    }
    return missing;
}

bool MainWindow::archaeologyDrawingWorkflowComplete() const
{
    return missingArchaeologyDrawings().isEmpty();
}

void MainWindow::recordProjectionDrawing(famp::projection::Plane plane)
{
    if (!isRequiredArchaeologyPlane(plane))
        return;

    emit sendStr2Console(
        tr("考古制图进度：%1 自动绘图已生成；系统将按画布和“二维制图”目录中的实际成果验收。")
            .arg(archaeologyPlaneName(plane)));
    updateArchaeologyWorkflowGuide();
}

void MainWindow::updateArchaeologyWorkflowGuide()
{
    if (!archaeologyWorkflowContext
        || archaeologyWorkflowSteps.size() != 6)
    {
        return;
    }

    const bool hasSource = projectionWorkflow.hasSource();
    const famp::projection::Preview* preview = projectionWorkflow.preview();
    const bool hasPreview = preview && preview->points
        && !preview->points->empty();
    const QStringList missingCanvas = missingArchaeologyCanvasDrawings();
    const QStringList missingTree = missingArchaeologyTreeDrawings();
    const QStringList missingDrawings = missingArchaeologyDrawings();
    const bool workflowComplete = missingDrawings.isEmpty();
    const bool planReady = ui.graphicsView
        && ui.graphicsView->hasProjectionDrawing(
            famp::projection::Plane::Overlook);
    int currentStep = 0;
    int completedThrough = -1;
    QString context;

    if (workflowComplete)
    {
        currentStep = 5;
        completedThrough = 4;
        context = tr("当前：俯视、XOZ、YOZ 三项自动绘图已同时存在于画布和“二维制图”目录  |  第⑥步已解锁：编辑并按 A4 实尺导出。");
    }
    else if (hasSource && hasPreview)
    {
        currentStep = 4;
        completedThrough = 3;
        context = !planReady
            ? tr("★ 首要步骤：请先完成【俯视投影二维制图】，再生成 XOZ/YOZ 剖面图  |  当前预览：%1")
                  .arg(famp::projection::displayName(preview->plane))
            : tr("当前预览：%1（预览不累计）  |  绘图画布缺：%2；左侧“二维制图”缺：%3。可自动绘图或切换下一方向。")
                  .arg(famp::projection::displayName(preview->plane),
                       missingCanvas.isEmpty()
                           ? tr("无")
                           : missingCanvas.join(QStringLiteral("、")),
                       missingTree.isEmpty()
                           ? tr("无")
                           : missingTree.join(QStringLiteral("、")));
    }
    else if (hasSource)
    {
        currentStep = 3;
        completedThrough = 2;
        const auto* source = projectionWorkflow.source();
        context = !planReady
            ? tr("★ 首要步骤：请先完成【俯视投影二维制图】，再生成 XOZ/YOZ 剖面图  |  当前点云：%1")
                  .arg(source ? source->name : QString())
            : tr("当前：%1  |  请选择 XOZ 或 YOZ 生成投影预览并确认剖面方向；绘图画布缺：%2，左侧“二维制图”缺：%3。")
                  .arg(source ? source->name : QString(),
                       missingCanvas.join(QStringLiteral("、")),
                       missingTree.join(QStringLiteral("、")));
    }
    else
    {
        context = tr("当前：尚未选择点云  |  下一步：选择点云；三项自动绘图需同时保留在画布和左侧“二维制图”目录。");
    }

    for (int index = 0; index < archaeologyWorkflowSteps.size(); ++index)
    {
        QString style;
        if (index <= completedThrough)
        {
            style = QStringLiteral(
                "QToolButton{color:#1b5e20;background:#e8f5e9;"
                "border:2px solid #81c784;border-radius:4px;"
                "padding:3px 6px;}");
        }
        else if (index == currentStep)
        {
            const bool decision = index == 4 && hasPreview
                && !workflowComplete;
            style = decision
                ? QStringLiteral(
                    "QToolButton{color:#e65100;background:#fff3e0;"
                    "border:2px solid #fb8c00;border-radius:4px;"
                    "padding:3px 6px;}")
                : QStringLiteral(
                    "QToolButton{color:#0d47a1;background:#e3f2fd;"
                    "border:2px solid #42a5f5;border-radius:4px;"
                    "padding:3px 6px;}");
        }
        else
        {
            style = QStringLiteral(
                "QToolButton{color:#607d8b;background:#f5f7f8;"
                "border:2px solid #cfd8dc;border-radius:4px;"
                "padding:3px 6px;}");
        }
        archaeologyWorkflowSteps.at(index)->setStyleSheet(style);
    }
    archaeologyWorkflowSteps.at(5)->setEnabled(workflowComplete);
    const bool canEditSelection = workflowComplete
        && ui.graphicsView && ui.graphicsView->hasSelectedItems();
    const QList<QAction*> finalStepActions{
        ui.actpoints,
        ui.actCompass,
        ui.actText,
        ui.actPlotTab,
        ui.actSave};
    for (QAction* action : finalStepActions)
        action->setEnabled(workflowComplete);
    const QList<QAction*> selectionEditingActions{
        ui.actGroup,
        ui.actBreak,
        ui.actMoveUp,
        ui.actMoveDown,
        ui.actMoveLeft,
        ui.actMoveRight,
        ui.actRotateLeft,
        ui.actRotateRight,
        ui.actEditFront,
        ui.acEditBack};
    for (QAction* action : selectionEditingActions)
        action->setEnabled(canEditSelection);
    ui.actSave->setToolTip(
        workflowComplete
            ? tr("按 A4 实尺导出当前考古制图成果。")
            : tr("需让俯视、XOZ、YOZ 三项自动绘图同时存在于画布和左侧“二维制图”目录。"));
    archaeologyWorkflowSteps.at(3)->setToolTip(
        hasPreview
            ? tr("当前投影预览：%1。预览不累计，可自动绘图或切换方向。")
                  .arg(famp::projection::displayName(preview->plane))
            : tr("尚无当前投影预览；预览不累计。"));
    QString automaticDrawingToolTip;
    if (workflowComplete)
    {
        automaticDrawingToolTip = tr(
            "俯视、XOZ、YOZ 三项自动绘图已同时存在于画布和“二维制图”目录。");
    }
    else if (!planReady)
    {
        automaticDrawingToolTip = tr(
            "请先完成俯视投影二维制图；随后 XOZ、YOZ 各自确认旋转方向并自动对齐。");
    }
    else
    {
        automaticDrawingToolTip = tr(
            "绘图画布缺：%1；左侧“二维制图”缺：%2。")
            .arg(missingCanvas.isEmpty()
                     ? tr("无")
                     : missingCanvas.join(QStringLiteral("、")),
                 missingTree.isEmpty()
                     ? tr("无")
                     : missingTree.join(QStringLiteral("、")));
    }
    archaeologyWorkflowSteps.at(4)->setToolTip(
        automaticDrawingToolTip);
    archaeologyWorkflowSteps.at(5)->setToolTip(
        workflowComplete
            ? tr("三项自动绘图成果齐全，可编辑并按 A4 实尺导出。")
            : tr("锁定：俯视、XOZ、YOZ 三项自动绘图必须同时存在于画布和左侧“二维制图”目录。"));
    archaeologyWorkflowContext->setText(context);
    archaeologyWorkflowContext->setToolTip(context);
    archaeologyWorkflowContext->setStyleSheet(
        !workflowComplete && hasSource && !planReady
            ? QStringLiteral(
                "QLabel{color:#9a3412;background:#fff7ed;"
                "border-left:5px solid #f97316;"
                "padding:3px 8px;font-weight:800;}")
            : QStringLiteral(
                "QLabel{color:#263238;background:#fafafa;"
                "border-left:3px solid #42a5f5;"
                "padding:3px 8px;font-weight:600;}"));
}

void MainWindow::clearTransientProjectionPreview()
{
    projectionWorkflow.clearPreview();
    if (ui.graphicsView)
        ui.graphicsView->clearProjectionInput();
    if (myVTK)
        myVTK->clearProjectionPreview();
    updateProjectionActions();
    updateArchaeologyWorkflowGuide();
}

void MainWindow::closeProjectionDecisionDialog(bool clearPreview)
{
    if (!projectionDecisionDialog)
    {
        if (clearPreview)
            clearTransientProjectionPreview();
        return;
    }

    QMessageBox* dialog = projectionDecisionDialog.data();
    lastProjectionDecisionPosition = dialog->pos();
    projectionDecisionDialog.clear();
    QObject::disconnect(dialog, nullptr, this, nullptr);
    dialog->close();
    dialog->deleteLater();
    if (clearPreview)
        clearTransientProjectionPreview();
}

QPoint MainWindow::projectionDecisionPopupPosition(
    const QSize& dialogSize) const
{
    const QPoint mainWindowCenter = mapToGlobal(rect().center());
    QPoint requested = lastProjectionDecisionPosition.value_or(
        mainWindowCenter
        - QPoint(dialogSize.width() / 2, dialogSize.height() / 2));

    QScreen* screen = QGuiApplication::screenAt(
        requested + QPoint(dialogSize.width() / 2,
                           dialogSize.height() / 2));
    if (!screen)
        screen = QGuiApplication::screenAt(mainWindowCenter);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return requested;

    const QRect available = screen->availableGeometry();
    const int maximumX = std::max(
        available.left(), available.right() - dialogSize.width() + 1);
    const int maximumY = std::max(
        available.top(), available.bottom() - dialogSize.height() + 1);
    requested.setX(qBound(available.left(), requested.x(), maximumX));
    requested.setY(qBound(available.top(), requested.y(), maximumY));
    return requested;
}

void MainWindow::applyInitialDockLayout()
{
    if (!ui.dockWidget1 || !ui.dockWidget2 || !centerDock)
        return;

    // The reference interface uses a compact content column. Scale it with
    // the window while keeping the tree and property table practical.
    const int contentWidth = qBound(260, width() / 7, 340);
    resizeDocks({ui.dockWidget1}, {contentWidth}, Qt::Horizontal);
    if (QMainWindow::layout())
        QMainWindow::layout()->activate();

    // resizeDocks() cannot resize the central widget directly. Resizing the
    // right dock to half of the combined VTK/drafting span gives both work
    // areas equal initial widths; QMainWindow assigns the remainder to VTK.
    const int workspaceWidth = centerDock->width() + ui.dockWidget2->width();
    if (workspaceWidth > 0)
    {
        resizeDocks({ui.dockWidget2}, {workspaceWidth / 2}, Qt::Horizontal);
        if (QMainWindow::layout())
            QMainWindow::layout()->activate();
    }
}

void MainWindow::initializeEntityWorkspace()
{
    workspaceStore = new famp::workspace::WorkspaceStore(this);
    model = new famp::presentation::EntityTreeModel(workspaceStore,
                                                     ui.treeView);
    ui.treeView->setModel(model);
    ui.treeView->setHeaderHidden(false);
    ui.treeView->setItemsExpandable(true);
    ui.treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui.treeView->setDragEnabled(true);
    ui.treeView->setAcceptDrops(true);
    ui.treeView->setDropIndicatorShown(true);
    ui.treeView->setDragDropMode(QAbstractItemView::InternalMove);
    ui.treeView->setDefaultDropAction(Qt::MoveAction);
    ui.treeView->setEditTriggers(
        QAbstractItemView::EditKeyPressed
        | QAbstractItemView::SelectedClicked);
    ui.treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui.treeView->expand(model->indexForId(workspaceStore->rootId()));

    auto* splitter = new QSplitter(Qt::Vertical, ui.dockWidgetContents_1);
    splitter->setObjectName(QStringLiteral("entityTreePropertiesSplitter"));
    ui.gridLayout->removeWidget(ui.treeView);
    splitter->addWidget(ui.treeView);
    entityProperties = new QTreeWidget(splitter);
    entityProperties->setObjectName(QStringLiteral("entityProperties"));
    entityProperties->setColumnCount(2);
    entityProperties->setHeaderLabels({tr("属性"), tr("值")});
    entityProperties->setRootIsDecorated(false);
    entityProperties->setAlternatingRowColors(true);
    entityProperties->setEditTriggers(QAbstractItemView::NoEditTriggers);
    entityProperties->header()->setStretchLastSection(true);
    splitter->addWidget(entityProperties);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({420, 220});
    ui.gridLayout->addWidget(splitter, 0, 0);

    ui.actDelete->setShortcut(QKeySequence::Delete);
    ui.actDelete->setShortcutContext(Qt::ApplicationShortcut);
    ui.actSave->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+S")));

    saveSelectedEntityAction = new QAction(tr("保存所选实体…"), this);
    saveSelectedEntityAction->setObjectName(QStringLiteral("actSaveSelectedEntity"));
    saveSelectedEntityAction->setShortcut(QKeySequence::Save);
    saveSelectedEntityAction->setShortcutContext(Qt::ApplicationShortcut);
    saveSelectedEntityAction->setEnabled(false);
    addAction(saveSelectedEntityAction);
    ui.menu_4->insertAction(ui.actSave, saveSelectedEntityAction);

    newEntityGroupAction = new QAction(tr("新建组"), this);
    renameEntityAction = new QAction(tr("重命名"), this);
    renameEntityAction->setShortcut(QKeySequence(Qt::Key_F2));
    renameEntityAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    toggleEntityAction = new QAction(tr("切换可见性"), this);
    toggleEntityAction->setShortcut(QKeySequence(Qt::Key_V));
    toggleEntityAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    lockEntityAction = new QAction(tr("锁定/解锁"), this);
    zoomEntityAction = new QAction(tr("缩放并居中"), this);
    zoomEntityAction->setShortcut(QKeySequence(Qt::Key_Z));
    zoomEntityAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    ui.treeView->addActions({renameEntityAction, toggleEntityAction,
                             zoomEntityAction});

    connect(saveSelectedEntityAction, &QAction::triggered,
            this, &MainWindow::slotSaveSelectedEntity);
    connect(newEntityGroupAction, &QAction::triggered,
            this, &MainWindow::slotCreateEntityGroup);
    connect(renameEntityAction, &QAction::triggered,
            this, &MainWindow::slotRenameEntity);
    connect(toggleEntityAction, &QAction::triggered,
            this, &MainWindow::slotToggleSelectedEntities);
    connect(lockEntityAction, &QAction::triggered,
            this, &MainWindow::slotLockSelectedEntities);
    connect(zoomEntityAction, &QAction::triggered,
            this, &MainWindow::slotZoomSelectedEntity);
    connect(ui.treeView, &QTreeView::customContextMenuRequested,
            this, &MainWindow::showEntityContextMenu);
    connect(ui.treeView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, [this]() {
                updateEntityProperties();
                updateCloudToolActions();
                synchronizeProjectionWorkflowFromSelection();
                const bool hasSelection = !selectedEntityIds().isEmpty();
                ui.actDelete->setEnabled(hasSelection);
                saveSelectedEntityAction->setEnabled(hasSelection);
            });
    connect(workspaceStore, &famp::workspace::WorkspaceStore::entityChanged,
            this, [this](const famp::workspace::EntityId& id,
                         const QVector<int>& roles) {
                synchronizeWorkspaceEntity(id, roles);
                updateEntityProperties();
                synchronizeProjectionWorkflowFromSelection();
            });
    connect(workspaceStore, &famp::workspace::WorkspaceStore::entityInserted,
            this, [this](const famp::workspace::EntityId& id) {
                ui.treeView->expand(model->indexForId(
                    workspaceStore->entity(id)->parentId));
            });
    connect(workspaceStore, &famp::workspace::WorkspaceStore::entityRemoved,
            this, [this](const famp::workspace::EntityId&,
                         const QVector<famp::workspace::EntityId>&) {
                updateEntityProperties();
                updateCloudToolActions();
                synchronizeProjectionWorkflowFromSelection();
            });
    connect(ui.graphicsView, &MyGraphicsView::workspaceItemsChanged,
            this, [this]() {
                if (!loadingProject && !syncingGraphicsEntities)
                    synchronizeGraphicsEntities();
                updateArchaeologyWorkflowGuide();
            });
    connect(ui.graphicsView, &MyGraphicsView::projectionDrawingCreated,
            this, &MainWindow::recordProjectionDrawing);

    famp::workspace::EntityWriter pointCloudWriter;
    pointCloudWriter.description = tr("PCD 点云");
    pointCloudWriter.extensions = {QStringLiteral("pcd")};
    pointCloudWriter.write = [](const famp::workspace::WorkspaceEntity& entity,
                                const QString& path,
                                QString* errorMessage) {
        const std::shared_ptr<MyCloudList> cloud = entity.payloadAs<MyCloudList>();
        if (!cloud || !cloud->layer.points || cloud->layer.points->empty())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("点云实体没有可保存的数据");
            return false;
        }
        return famp::io::savePcdAsciiAtomically(
            famp::io::pathWithRequiredSuffix(path, QStringLiteral("pcd")),
            *cloud->layer.points,
            errorMessage,
            &cloud->layer.spatial,
            &cloud->layer.attributes);
    };
    entityWriters.registerWriter(famp::workspace::EntityKind::PointCloud,
                                 std::move(pointCloudWriter));

    famp::workspace::EntityWriter demWriter;
    demWriter.description = tr("FAMP DEM");
    demWriter.extensions = {QStringLiteral("famp-dem")};
    demWriter.write = [](const famp::workspace::WorkspaceEntity& entity,
                         const QString& path,
                         QString* errorMessage) {
        const std::shared_ptr<famp::terrain::Result> result =
            entity.payloadAs<famp::terrain::Result>();
        if (!result || !result->grid.isValid())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("DEM 实体没有可保存的网格数据");
            return false;
        }
        return famp::terrainio::saveGridAtomically(
            famp::terrainio::pathWithDemSuffix(path), result->grid,
            errorMessage);
    };
    entityWriters.registerWriter(famp::workspace::EntityKind::Dem,
                                 std::move(demWriter));

    famp::workspace::EntityWriter contourWriter;
    contourWriter.description = tr("等高线 CSV");
    contourWriter.extensions = {QStringLiteral("csv")};
    contourWriter.write = [](const famp::workspace::WorkspaceEntity& entity,
                             const QString& path,
                             QString* errorMessage) {
        const std::shared_ptr<famp::terrain::Result> result =
            entity.payloadAs<famp::terrain::Result>();
        if (!result || result->contours.isEmpty())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("等高线实体没有可保存的数据");
            return false;
        }
        return famp::terrainio::exportContoursCsvAtomically(
            famp::io::pathWithRequiredSuffix(path, QStringLiteral("csv")),
            result->contours, errorMessage);
    };
    entityWriters.registerWriter(famp::workspace::EntityKind::ContourSet,
                                 std::move(contourWriter));

    famp::workspace::EntityWriter profileWriter;
    profileWriter.description = tr("FAMP 点云剖面");
    profileWriter.extensions = {QStringLiteral("famp-profile")};
    profileWriter.write = [](const famp::workspace::WorkspaceEntity& entity,
                             const QString& path,
                             QString* errorMessage) {
        const std::shared_ptr<famp::profile::Result> result =
            entity.payloadAs<famp::profile::Result>();
        if (!result || !result->succeeded())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("剖面实体没有可保存的数据");
            return false;
        }
        return famp::profileio::saveResultAtomically(
            famp::profileio::pathWithProfileSuffix(path), *result,
            errorMessage);
    };
    entityWriters.registerWriter(famp::workspace::EntityKind::Profile,
                                 std::move(profileWriter));

    famp::workspace::EntityWriter cutFillWriter;
    cutFillWriter.description = tr("FAMP 挖填方成果");
    cutFillWriter.extensions = {QStringLiteral("famp-volume")};
    cutFillWriter.write = [](const famp::workspace::WorkspaceEntity& entity,
                             const QString& path,
                             QString* errorMessage) {
        const std::shared_ptr<famp::cutfill::Result> result =
            entity.payloadAs<famp::cutfill::Result>();
        if (!result || !result->succeeded())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("挖填方实体没有可保存的数据");
            return false;
        }
        return famp::cutfillio::saveResultAtomically(
            famp::cutfillio::pathWithVolumeSuffix(path), *result,
            errorMessage);
    };
    entityWriters.registerWriter(famp::workspace::EntityKind::CutFill,
                                 std::move(cutFillWriter));

    famp::workspace::RendererCallbacks pointCloudRenderer;
    pointCloudRenderer.show = [this](const famp::workspace::WorkspaceEntity& entity,
                                     QString* errorMessage) {
        const std::shared_ptr<MyCloudList> cloud = entity.payloadAs<MyCloudList>();
        if (!cloud || !cloud->cloudactor)
        {
            if (errorMessage)
                *errorMessage = tr("点云实体没有渲染对象");
            return false;
        }
        myVTK->display(cloud->cloudactor);
        if (errorMessage)
            errorMessage->clear();
        return true;
    };
    pointCloudRenderer.hide = [this](const famp::workspace::WorkspaceEntity& entity) {
        const std::shared_ptr<MyCloudList> cloud = entity.payloadAs<MyCloudList>();
        if (cloud && cloud->cloudactor)
            myVTK->removeCloudDisplay(cloud->cloudactor);
        if (cloud && cloud->AABBactor)
            myVTK->removeAABBDisplay(cloud->AABBactor);
    };
    pointCloudRenderer.remove = [](const famp::workspace::WorkspaceEntity&) {};
    pointCloudRenderer.select = [this](
        const famp::workspace::WorkspaceEntity& entity) {
        const std::shared_ptr<MyCloudList> cloud =
            entity.payloadAs<MyCloudList>();
        if (cloud && cloud->AABBactor && cloud->layer.visible)
            myVTK->display(cloud->AABBactor);
    };
    pointCloudRenderer.zoom = [this](const famp::workspace::WorkspaceEntity&) {
        myVTK->initCamera();
        myVTK->update();
    };
    entityRenderers.registerRenderer(famp::workspace::EntityKind::PointCloud,
                                     std::move(pointCloudRenderer));

    const auto canvasRenderer = [this]() {
        famp::workspace::RendererCallbacks renderer;
        renderer.show = [this](const famp::workspace::WorkspaceEntity& entity,
                               QString* errorMessage) {
            const QString itemId = entity.display.value(
                QStringLiteral("itemId")).toString();
            const bool shown = ui.graphicsView->setWorkspaceItemVisible(
                itemId, true);
            if (!shown && errorMessage)
                *errorMessage = tr("二维图元已经不存在");
            else if (errorMessage)
                errorMessage->clear();
            return shown;
        };
        renderer.hide = [this](const famp::workspace::WorkspaceEntity& entity) {
            ui.graphicsView->setWorkspaceItemVisible(
                entity.display.value(QStringLiteral("itemId")).toString(),
                false);
        };
        renderer.remove = [this](const famp::workspace::WorkspaceEntity& entity) {
            ui.graphicsView->removeWorkspaceItemPermanently(
                entity.display.value(QStringLiteral("itemId")).toString());
        };
        renderer.select = [this](const famp::workspace::WorkspaceEntity& entity) {
            ui.graphicsView->selectWorkspaceItem(
                entity.display.value(QStringLiteral("itemId")).toString(),
                false);
        };
        renderer.zoom = [this](const famp::workspace::WorkspaceEntity& entity) {
            ui.graphicsView->selectWorkspaceItem(
                entity.display.value(QStringLiteral("itemId")).toString(),
                true);
        };
        return renderer;
    };
    entityRenderers.registerRenderer(
        famp::workspace::EntityKind::Measurement2D, canvasRenderer());
    entityRenderers.registerRenderer(
        famp::workspace::EntityKind::GraphicsItem, canvasRenderer());

    famp::workspace::RendererCallbacks measurement3dRenderer;
    measurement3dRenderer.show =
        [this](const famp::workspace::WorkspaceEntity& entity,
               QString* errorMessage) {
            const QString recordId = entity.display.value(
                QStringLiteral("measurementId")).toString();
            const bool shown = myVTK
                && myVTK->setMeasurementVisible(recordId, true);
            if (!shown && errorMessage)
                *errorMessage = tr("三维测量已经不存在");
            else if (errorMessage)
                errorMessage->clear();
            return shown;
        };
    measurement3dRenderer.hide =
        [this](const famp::workspace::WorkspaceEntity& entity) {
            if (myVTK)
            {
                myVTK->setMeasurementVisible(
                    entity.display.value(
                        QStringLiteral("measurementId")).toString(),
                    false);
            }
        };
    measurement3dRenderer.remove =
        [this](const famp::workspace::WorkspaceEntity& entity) {
            if (myVTK)
            {
                myVTK->removeMeasurement(
                    entity.display.value(
                        QStringLiteral("measurementId")).toString());
            }
        };
    measurement3dRenderer.select =
        [this](const famp::workspace::WorkspaceEntity& entity) {
            if (myVTK)
            {
                myVTK->setMeasurementSelected(
                    entity.display.value(
                        QStringLiteral("measurementId")).toString(),
                    true);
            }
        };
    measurement3dRenderer.zoom =
        [this](const famp::workspace::WorkspaceEntity&) {
            if (myVTK)
            {
                myVTK->initCamera();
                myVTK->update();
            }
        };
    entityRenderers.registerRenderer(
        famp::workspace::EntityKind::Measurement3D,
        std::move(measurement3dRenderer));

    updateEntityProperties();
}

famp::workspace::EntityId MainWindow::ensureWorkspaceSystemGroup(
    const QString& role,
    const QString& name)
{
    for (const famp::workspace::EntityId& id : workspaceStore->allEntityIds())
    {
        const famp::workspace::WorkspaceEntity* entity = workspaceStore->entity(id);
        if (entity && entity->kind == famp::workspace::EntityKind::Group
            && entity->display.value(QStringLiteral("systemRole")).toString()
                   == role)
        {
            return id;
        }
    }
    famp::workspace::WorkspaceEntity group = famp::workspace::makeEntity(
        famp::workspace::EntityKind::Group, name);
    group.display.insert(QStringLiteral("systemRole"), role);
    group.dirty = true;
    return workspaceStore->addEntity(group, workspaceStore->rootId());
}

void MainWindow::synchronizeGraphicsEntities()
{
    if (syncingGraphicsEntities || !workspaceStore || !ui.graphicsView)
        return;
    const QScopedValueRollback<bool> graphicsGuard(
        syncingGraphicsEntities, true);
    const QScopedValueRollback<bool> entityGuard(
        syncingWorkspaceEntity, true);
    const QVector<WorkspaceGraphicsItem> items =
        ui.graphicsView->workspaceGraphicsItems();

    famp::workspace::EntityId groupId;
    for (const famp::workspace::EntityId& id : workspaceStore->allEntityIds())
    {
        const famp::workspace::WorkspaceEntity* entity = workspaceStore->entity(id);
        if (entity && entity->kind == famp::workspace::EntityKind::Group
            && entity->display.value(QStringLiteral("systemRole")).toString()
                   == QStringLiteral("canvas2d"))
        {
            groupId = id;
            break;
        }
    }
    if (groupId.isNull() && items.isEmpty())
        return;
    if (groupId.isNull())
    {
        groupId = ensureWorkspaceSystemGroup(
            QStringLiteral("canvas2d"), tr("二维制图"));
        if (groupId.isNull())
            return;
    }

    QSet<famp::workspace::EntityId> liveIds;
    bool changed = false;
    for (const WorkspaceGraphicsItem& item : items)
    {
        const famp::workspace::EntityId id(item.id);
        if (id.isNull())
            continue;
        liveIds.insert(id);
        const auto kind = item.measurement
            ? famp::workspace::EntityKind::Measurement2D
            : famp::workspace::EntityKind::GraphicsItem;
        const famp::workspace::WorkspaceEntity* existing =
            workspaceStore->entity(id);
        if (!existing)
        {
            famp::workspace::WorkspaceEntity entity =
                famp::workspace::makeEntity(kind, item.name);
            entity.id = id;
            entity.visible = item.visible;
            entity.dirty = true;
            entity.display.insert(QStringLiteral("systemRole"),
                                  QStringLiteral("canvas-item"));
            entity.display.insert(QStringLiteral("itemId"), item.id);
            entity.setPayload(std::make_shared<GraphicsEntityPayload>(
                GraphicsEntityPayload{item.id, item.handle}));
            if (!workspaceStore->addEntity(entity, groupId).isNull())
                changed = true;
            continue;
        }
        if (existing->kind != kind)
            continue;
        const std::shared_ptr<GraphicsEntityPayload> payload =
            existing->payloadAs<GraphicsEntityPayload>();
        if (existing->visible == item.visible && payload
            && payload->handle == item.handle)
        {
            continue;
        }
        famp::workspace::WorkspaceEntity replacement = *existing;
        replacement.visible = item.visible;
        replacement.display.insert(QStringLiteral("systemRole"),
                                   QStringLiteral("canvas-item"));
        replacement.display.insert(QStringLiteral("itemId"), item.id);
        replacement.setPayload(std::make_shared<GraphicsEntityPayload>(
            GraphicsEntityPayload{item.id, item.handle}));
        workspaceStore->replaceEntity(replacement);
    }

    const QVector<famp::workspace::EntityId> allIds =
        workspaceStore->allEntityIds();
    for (const famp::workspace::EntityId& id : allIds)
    {
        const famp::workspace::WorkspaceEntity* entity = workspaceStore->entity(id);
        if (!entity || (entity->kind != famp::workspace::EntityKind::Measurement2D
                        && entity->kind != famp::workspace::EntityKind::GraphicsItem)
            || liveIds.contains(id))
        {
            continue;
        }
        if (entity->locked)
            workspaceStore->setLocked(id, false);
        if (workspaceStore->removeEntity(id))
            changed = true;
    }
    if (changed && !loadingProject)
        markProjectDirty();
}

void MainWindow::synchronizeMeasurementEntities()
{
    if (syncingMeasurementEntities || !workspaceStore || !myVTK)
        return;
    const QScopedValueRollback<bool> measurementGuard(
        syncingMeasurementEntities, true);
    const QScopedValueRollback<bool> entityGuard(
        syncingWorkspaceEntity, true);
    const QVector<famp::measurement::Record3D> records =
        myVTK->measurements();

    famp::workspace::EntityId groupId;
    for (const famp::workspace::EntityId& id : workspaceStore->allEntityIds())
    {
        const famp::workspace::WorkspaceEntity* entity = workspaceStore->entity(id);
        if (entity && entity->kind == famp::workspace::EntityKind::Group
            && entity->display.value(QStringLiteral("systemRole")).toString()
                   == QStringLiteral("measurements3d"))
        {
            groupId = id;
            break;
        }
    }
    if (groupId.isNull() && records.isEmpty())
        return;
    if (groupId.isNull())
    {
        groupId = ensureWorkspaceSystemGroup(
            QStringLiteral("measurements3d"), tr("三维测量"));
        if (groupId.isNull())
            return;
    }

    QSet<famp::workspace::EntityId> liveIds;
    bool changed = false;
    for (const famp::measurement::Record3D& record : records)
    {
        const famp::workspace::EntityId id(record.id);
        if (id.isNull())
            continue;
        liveIds.insert(id);
        const famp::workspace::WorkspaceEntity* existing =
            workspaceStore->entity(id);
        if (!existing)
        {
            QString kindLabel;
            switch (record.kind)
            {
            case famp::measurement::Kind::Distance:
                kindLabel = tr("距离");
                break;
            case famp::measurement::Kind::Area:
                kindLabel = tr("面积");
                break;
            case famp::measurement::Kind::Angle:
                kindLabel = tr("角度");
                break;
            }
            famp::workspace::WorkspaceEntity entity =
                famp::workspace::makeEntity(
                    famp::workspace::EntityKind::Measurement3D,
                    tr("三维%1 %2").arg(kindLabel, record.id.left(8)));
            entity.id = id;
            entity.dirty = true;
            entity.display.insert(QStringLiteral("systemRole"),
                                  QStringLiteral("measurement3d-item"));
            entity.display.insert(QStringLiteral("measurementId"), record.id);
            entity.display.insert(QStringLiteral("layerId"), record.layerId);
            entity.setPayload(
                std::make_shared<famp::measurement::Record3D>(record));
            if (!workspaceStore->addEntity(entity, groupId).isNull())
                changed = true;
            continue;
        }
        if (existing->kind != famp::workspace::EntityKind::Measurement3D)
        {
            emit sendStr2Console(
                tr("三维测量 ID 与现有实体冲突：%1").arg(record.id));
            continue;
        }

        famp::workspace::WorkspaceEntity replacement = *existing;
        replacement.display.insert(QStringLiteral("systemRole"),
                                   QStringLiteral("measurement3d-item"));
        replacement.display.insert(QStringLiteral("measurementId"), record.id);
        replacement.display.insert(QStringLiteral("layerId"), record.layerId);
        replacement.setPayload(
            std::make_shared<famp::measurement::Record3D>(record));
        workspaceStore->replaceEntity(replacement);
        myVTK->setMeasurementVisible(record.id, existing->visible);
    }

    const QVector<famp::workspace::EntityId> allIds =
        workspaceStore->allEntityIds();
    for (const famp::workspace::EntityId& id : allIds)
    {
        const famp::workspace::WorkspaceEntity* entity = workspaceStore->entity(id);
        if (!entity
            || entity->kind != famp::workspace::EntityKind::Measurement3D
            || liveIds.contains(id))
        {
            continue;
        }
        if (entity->locked)
            workspaceStore->setLocked(id, false);
        if (workspaceStore->removeEntity(id))
            changed = true;
    }
    if (changed && !loadingProject)
        markProjectDirty();
}

void MainWindow::initializeUndoActions()
{
    editMenu = new QMenu(tr("编辑"), this);
    editMenu->setObjectName(QStringLiteral("menuEdit"));
    ui.menuBar->insertMenu(ui.menu_5->menuAction(), editMenu);

    undoGraphicsAction = ui.graphicsView->commandStack()->createUndoAction(
        this, tr("撤销"));
    undoGraphicsAction->setObjectName(QStringLiteral("actUndoGraphics"));
    undoGraphicsAction->setShortcuts(
        QKeySequence::keyBindings(QKeySequence::Undo));

    redoGraphicsAction = ui.graphicsView->commandStack()->createRedoAction(
        this, tr("重做"));
    redoGraphicsAction->setObjectName(QStringLiteral("actRedoGraphics"));
    redoGraphicsAction->setShortcuts(
        QKeySequence::keyBindings(QKeySequence::Redo));

    editMenu->addAction(undoGraphicsAction);
    editMenu->addAction(redoGraphicsAction);
}

void MainWindow::initializeCrsActions()
{
    toolsMenu = new QMenu(tr("工具"), this);
    toolsMenu->setObjectName(QStringLiteral("menuTools"));
    ui.menuBar->insertMenu(ui.menu_8->menuAction(), toolsMenu);

    setProjectCrsAction = toolsMenu->addAction(tr("项目坐标系…"));
    setProjectCrsAction->setObjectName(QStringLiteral("actSetProjectCrs"));
    coordinateConverterAction = toolsMenu->addAction(tr("坐标转换器…"));
    coordinateConverterAction->setObjectName(QStringLiteral("actCoordinateConverter"));
    cloudCoordinateAction = toolsMenu->addAction(tr("点云局部/真实坐标…"));
    cloudCoordinateAction->setObjectName(
        QStringLiteral("actCloudCoordinateViewer"));
    cloudCoordinateAction->setEnabled(false);
    reprojectCloudAction = toolsMenu->addAction(tr("重投影所选点云…"));
    reprojectCloudAction->setObjectName(QStringLiteral("actReprojectCloud"));
    reprojectCloudAction->setEnabled(false);
    archaeologyMetadataAction = toolsMenu->addAction(tr("考古图层属性…"));
    archaeologyMetadataAction->setObjectName(
        QStringLiteral("actArchaeologyMetadata"));
    archaeologyMetadataAction->setEnabled(false);
    controlPointsAction = toolsMenu->addAction(tr("控制点与空间配准…"));
    controlPointsAction->setObjectName(QStringLiteral("actControlPoints"));
    controlPointsAction->setEnabled(false);
    terrainAnalysisAction = toolsMenu->addAction(tr("DEM 与等高线…"));
    terrainAnalysisAction->setObjectName(
        QStringLiteral("actTerrainAnalysis"));
    terrainAnalysisAction->setEnabled(false);
    cutFillAction = toolsMenu->addAction(tr("挖填方与体积…"));
    cutFillAction->setObjectName(QStringLiteral("actCutFillAnalysis"));
    cutFillAction->setEnabled(false);
    cloudProfileAction = toolsMenu->addAction(tr("点云高程剖面…"));
    cloudProfileAction->setObjectName(QStringLiteral("actCloudProfile"));
    cloudProfileAction->setEnabled(false);
    connect(setProjectCrsAction, &QAction::triggered,
            this, &MainWindow::slotSetProjectCrs);
    connect(coordinateConverterAction, &QAction::triggered,
            this, &MainWindow::slotOpenCoordinateConverter);
    connect(cloudCoordinateAction, &QAction::triggered,
            this, &MainWindow::slotOpenCloudCoordinateViewer);
    connect(reprojectCloudAction, &QAction::triggered,
            this, &MainWindow::slotReprojectCloud);
    connect(archaeologyMetadataAction, &QAction::triggered,
            this, &MainWindow::slotEditArchaeologyMetadata);
    connect(controlPointsAction, &QAction::triggered,
            this, &MainWindow::slotEditControlPoints);
    connect(terrainAnalysisAction, &QAction::triggered,
            this, &MainWindow::slotGenerateTerrain);
    connect(cutFillAction, &QAction::triggered,
            this, &MainWindow::slotCalculateCutFill);
    connect(cloudProfileAction, &QAction::triggered,
            this, &MainWindow::slotStartCloudProfile);
    connect(myVTK, &MyVTK::profileLineSelectionCancelled,
            this, [this]() {
                pendingProfileLayerId.clear();
                pendingProfilePointCloud = nullptr;
            });
    connect(myVTK, &MyVTK::profileLineSelected,
            this,
            [this](const QString& layerId,
                   const QVector3D& localStart,
                   const QVector3D& localEnd) {
                if (layerId != pendingProfileLayerId)
                {
                    pendingProfileLayerId.clear();
                    pendingProfilePointCloud = nullptr;
                    emit sendStr2Console(
                        tr("剖面线关联图层已变化，本次生成已取消。"));
                    return;
                }
                const auto current = std::find_if(
                    pointCloudList.cbegin(), pointCloudList.cend(),
                    [&layerId](const MyCloudList& cloud) {
                        return cloud.layer.id.compare(
                            layerId, Qt::CaseInsensitive) == 0;
                    });
                QString effectiveCrs;
                if (current != pointCloudList.cend())
                {
                    effectiveCrs = current->layer.crs.trimmed();
                    if (effectiveCrs.isEmpty())
                        effectiveCrs = projectCrs.trimmed();
                    if (!effectiveCrs.isEmpty())
                        effectiveCrs = famp::crs::normalizedEpsg(effectiveCrs);
                }
                if (current == pointCloudList.cend()
                    || current->layer.points.get() != pendingProfilePointCloud
                    || current->layer.spatial.origin
                        != pendingProfileSpatial.origin
                    || current->layer.spatial.transform
                        != pendingProfileSpatial.transform
                    || effectiveCrs.compare(
                        pendingProfileSourceCrs, Qt::CaseInsensitive) != 0)
                {
                    pendingProfileLayerId.clear();
                    pendingProfilePointCloud = nullptr;
                    const QString message = tr(
                        "拾取期间点云数据或空间参考发生变化，请重新选择剖面线。");
                    statusBar()->showMessage(message, 8000);
                    emit sendStr2Console(message);
                    return;
                }
                const QString sourceCrs = pendingProfileSourceCrs;
                const QString crsDescription = pendingProfileCrsDescription;
                const QString unitName = pendingProfileHorizontalUnitName;
                const double unitToMetre =
                    pendingProfileHorizontalUnitToMetre;
                pendingProfileLayerId.clear();
                pendingProfilePointCloud = nullptr;
                generateCloudProfile(
                    layerId, localStart, localEnd, sourceCrs,
                    crsDescription, unitName, unitToMetre);
            });

    toolsMenu->addSeparator();
    cloudDisplaySettingsAction = toolsMenu->addAction(tr("点云显示设置…"));
    cloudDisplaySettingsAction->setObjectName(
        QStringLiteral("actCloudDisplaySettings"));
    cloudDisplaySettingsAction->setEnabled(false);
    preprocessCloudAction = toolsMenu->addAction(tr("点云预处理…"));
    preprocessCloudAction->setObjectName(
        QStringLiteral("actPreprocessCloud"));
    preprocessCloudAction->setEnabled(false);
    cropCloudAction = toolsMenu->addAction(tr("按坐标范围裁剪…"));
    cropCloudAction->setObjectName(QStringLiteral("actCropCloud"));
    cropCloudAction->setEnabled(false);
    registerCloudAction = toolsMenu->addAction(tr("点云 ICP 配准…"));
    registerCloudAction->setObjectName(QStringLiteral("actRegisterCloud"));
    registerCloudAction->setEnabled(false);
    connect(cloudDisplaySettingsAction, &QAction::triggered,
            this, &MainWindow::slotCloudDisplaySettings);
    connect(preprocessCloudAction, &QAction::triggered,
            this, &MainWindow::slotPreprocessCloud);
    connect(cropCloudAction, &QAction::triggered,
            this, &MainWindow::slotCropCloud);
    connect(registerCloudAction, &QAction::triggered,
            this, &MainWindow::slotRegisterCloud);

    toolsMenu->addSeparator();
    measurementActionGroup = new QActionGroup(this);
    measurementActionGroup->setExclusive(true);
    distanceMeasureAction = toolsMenu->addAction(tr("测量距离"));
    distanceMeasureAction->setObjectName(QStringLiteral("actMeasureDistance"));
    distanceMeasureAction->setCheckable(true);
    distanceMeasureAction->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+Alt+D")));
    areaMeasureAction = toolsMenu->addAction(tr("测量面积"));
    areaMeasureAction->setObjectName(QStringLiteral("actMeasureArea"));
    areaMeasureAction->setCheckable(true);
    areaMeasureAction->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+Alt+A")));
    angleMeasureAction = toolsMenu->addAction(tr("测量角度"));
    angleMeasureAction->setObjectName(QStringLiteral("actMeasureAngle"));
    angleMeasureAction->setCheckable(true);
    angleMeasureAction->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+Alt+G")));
    measurementActionGroup->addAction(distanceMeasureAction);
    measurementActionGroup->addAction(areaMeasureAction);
    measurementActionGroup->addAction(angleMeasureAction);
    clearMeasurementsAction = toolsMenu->addAction(tr("清除测量结果"));
    clearMeasurementsAction->setObjectName(
        QStringLiteral("actClearMeasurements"));

    connect(distanceMeasureAction, &QAction::triggered,
            this, [this](bool checked) {
                if (!checked)
                    return;
                ui.graphicsView->startDistanceMeasurement(false);
                myVTK->startDistanceMeasurement(false);
                const QString message = tr(
                    "距离测量已启用：可在中央点云或右侧制图画布左键选点，右键完成，Esc 取消。");
                statusBar()->showMessage(message, 10000);
                emit sendStr2Console(message);
            });
    connect(areaMeasureAction, &QAction::triggered,
            this, [this](bool checked) {
                if (!checked)
                    return;
                ui.graphicsView->startAreaMeasurement(false);
                myVTK->startAreaMeasurement(false);
                const QString message = tr(
                    "面积测量已启用：可在中央点云或右侧制图画布左键选取边界，右键闭合，Esc 取消。");
                statusBar()->showMessage(message, 10000);
                emit sendStr2Console(message);
            });
    connect(angleMeasureAction, &QAction::triggered,
            this, [this](bool checked) {
                if (!checked)
                    return;
                ui.graphicsView->startAngleMeasurement(false);
                myVTK->startAngleMeasurement(false);
                const QString message = tr(
                    "角度测量已启用：依次选取第一边点、顶点和第二边点，可在点云或制图画布操作。");
                statusBar()->showMessage(message, 10000);
                emit sendStr2Console(message);
            });
    connect(clearMeasurementsAction, &QAction::triggered,
            this, [this]() {
                const int count = ui.graphicsView->measurementCount()
                    + myVTK->measurementCount();
                const QVector<famp::measurement::Record3D> measurements3d =
                    myVTK->measurements();
                if (count > 0)
                {
                    QUndoStack* stack = ui.graphicsView->commandStack();
                    stack->beginMacro(tr("清除测量结果"));
                    ui.graphicsView->clearMeasurements(false);
                    if (!measurements3d.isEmpty())
                    {
                        stack->push(famp::graphics::makeCallbackCommand(
                            [this, measurements3d]() {
                                QString error;
                                if (!myVTK->setMeasurements(
                                        measurements3d, &error))
                                {
                                    emit sendStr2Console(
                                        tr("恢复三维测量失败：%1").arg(error));
                                }
                            },
                            [this]() {
                                QString error;
                                if (!myVTK->setMeasurements({}, &error))
                                {
                                    emit sendStr2Console(
                                        tr("清除三维测量失败：%1").arg(error));
                                }
                            },
                            tr("清除三维测量结果")));
                    }
                    stack->endMacro();
                }
                const QString message = count > 0
                    ? tr("已清除 %1 个测量结果，可通过撤销恢复。")
                          .arg(count)
                    : tr("当前点云和制图画布没有测量结果。");
                statusBar()->showMessage(message, 8000);
                emit sendStr2Console(message);
            });
    connect(ui.graphicsView, &MyGraphicsView::measurementModeEnded,
            this, [this]() {
                myVTK->deactivateMeasurement();
                if (QAction* checked = measurementActionGroup->checkedAction())
                    checked->setChecked(false);
            });
    connect(myVTK, &MyVTK::measurementModeEnded,
            this, [this]() {
                ui.graphicsView->deactivateMeasurement();
                if (QAction* checked = measurementActionGroup->checkedAction())
                    checked->setChecked(false);
            });
    connect(ui.graphicsView, &MyGraphicsView::measurementStatus,
            this, [this](const QString& message) {
                statusBar()->showMessage(message, 8000);
                emit sendStr2Console(message);
            });
    connect(myVTK, &MyVTK::measurementStatus,
            this, [this](const QString& message) {
                statusBar()->showMessage(message, 8000);
                emit sendStr2Console(message);
            });
    connect(myVTK, &MyVTK::measurementCompleted,
            this, [this](const famp::measurement::Record3D& record) {
                QString commandText = tr("添加三维距离测量");
                if (record.kind == famp::measurement::Kind::Area)
                    commandText = tr("添加三维面积测量");
                else if (record.kind == famp::measurement::Kind::Angle)
                    commandText = tr("添加三维角度测量");
                ui.graphicsView->commandStack()->push(
                    famp::graphics::makeCallbackCommand(
                        [this, id = record.id]() {
                            myVTK->removeMeasurement(id);
                        },
                        [this, record]() {
                            QString error;
                            if (!myVTK->addMeasurement(record, &error))
                            {
                                emit sendStr2Console(
                                    tr("添加三维测量失败：%1").arg(error));
                            }
                        },
                        commandText));
            });
    connect(myVTK, &MyVTK::measurementsChanged,
            this, [this]() {
                if (!loadingProject && !syncingMeasurementEntities)
                    synchronizeMeasurementEntities();
                markProjectDirty();
            });

    crsStatusLabel = new QLabel(this);
    crsStatusLabel->setMinimumWidth(150);
    statusBar()->addPermanentWidget(crsStatusLabel);
    updateCrsStatus();
}

void MainWindow::initializeProjectActions()
{
    newProjectAction = new QAction(tr("新建项目"), this);
    newProjectAction->setObjectName(QStringLiteral("actNewProject"));
    newProjectAction->setShortcut(QKeySequence::New);
    openProjectAction = new QAction(tr("打开项目…"), this);
    openProjectAction->setObjectName(QStringLiteral("actOpenProject"));
    openProjectAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    saveProjectAction = new QAction(tr("保存项目"), this);
    saveProjectAction->setObjectName(QStringLiteral("actSaveProject"));
    saveProjectAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    saveProjectAsAction = new QAction(tr("项目另存为…"), this);
    saveProjectAsAction->setObjectName(QStringLiteral("actSaveProjectAs"));
    exportReportAction = new QAction(tr("导出考古项目报告…"), this);
    exportReportAction->setObjectName(QStringLiteral("actExportArchaeologyReport"));

    ui.menu_4->insertAction(ui.actOpenCloud, newProjectAction);
    ui.menu_4->insertAction(ui.actOpenCloud, openProjectAction);
    ui.menu_4->insertAction(ui.actOpenCloud, saveProjectAction);
    ui.menu_4->insertAction(ui.actOpenCloud, saveProjectAsAction);
    ui.menu_4->insertAction(ui.actOpenCloud, exportReportAction);
    ui.menu_4->insertSeparator(ui.actOpenCloud);
    ui.actSave->setText(tr("导出平面图…"));

    connect(newProjectAction, &QAction::triggered,
            this, &MainWindow::slotNewProject);
    connect(openProjectAction, &QAction::triggered,
            this, &MainWindow::slotOpenProject);
    connect(saveProjectAction, &QAction::triggered,
            this, &MainWindow::slotSaveProject);
    connect(saveProjectAsAction, &QAction::triggered,
            this, &MainWindow::slotSaveProjectAs);
    connect(exportReportAction, &QAction::triggered,
            this, &MainWindow::slotExportArchaeologyReport);
}

void MainWindow::slotExportArchaeologyReport()
{
    famp::project::Document project;
    QString error;
    if (!currentProjectDocument(project, &error))
    {
        QMessageBox::warning(this, tr("导出考古项目报告"), error);
        return;
    }

    famp::report::Data report;
    report.projectPath = currentProjectPath;
    report.projectName = currentProjectPath.isEmpty()
        ? tr("未命名项目")
        : QFileInfo(currentProjectPath).completeBaseName();
    report.projectCrs = project.projectCrs;
    report.mapScale = project.mapScale;
    report.applicationVersion = QCoreApplication::applicationVersion();
    report.generatedAt = QDateTime::currentDateTime();
    report.graphicsState = project.graphicsState;
    report.measurements3d = project.measurements3d;
    for (const MyCloudList& cloud : pointCloudList)
    {
        const famp::workspace::WorkspaceEntity* entity =
            workspaceStore->entity(entityIdForCloud(cloud));
        famp::report::CloudEntry entry;
        entry.name = entity ? entity->name : cloud.layer.name;
        entry.path = entity && entity->assetPath.has_value()
            ? *entity->assetPath : cloud.layer.sourcePath;
        entry.crs = cloud.layer.crs;
        entry.pointCount = cloud.layer.points ? cloud.layer.points->size() : 0;
        entry.visible = entity ? entity->visible : cloud.layer.visible;
        entry.spatial = cloud.layer.spatial;
        entry.archaeologyFields = cloud.layer.archaeologyFields;
        entry.controlPoints = cloud.layer.controlPoints;
        report.clouds.append(entry);
    }

    const QString initialDirectory = currentProjectPath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(currentProjectPath).absolutePath();
    const QString initialPath = QDir(initialDirectory).filePath(
        report.projectName + QStringLiteral("_考古项目报告.pdf"));
    QString selectedFilter;
    QString outputPath = QFileDialog::getSaveFileName(
        this, tr("导出考古项目报告"), initialPath,
        tr("PDF 报告 (*.pdf);;HTML 报告 (*.html)"), &selectedFilter);
    if (outputPath.isEmpty())
        return;
    const bool html = selectedFilter.contains(QStringLiteral("HTML"),
                                               Qt::CaseInsensitive)
        || outputPath.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive);
    outputPath = famp::io::pathWithRequiredSuffix(
        outputPath, html ? QStringLiteral("html") : QStringLiteral("pdf"));
    const bool saved = html
        ? famp::report::saveHtml(outputPath, report, &error)
        : famp::report::savePdf(outputPath, report, &error);
    if (!saved)
    {
        QMessageBox::warning(this, tr("导出考古项目报告失败"), error);
        return;
    }
    statusBar()->showMessage(tr("考古项目报告已保存"), 5000);
    emit sendStr2Console(tr("已导出考古项目报告  %1").arg(outputPath));
}

bool MainWindow::currentProjectDocument(
    famp::project::Document& document,
    QString* errorMessage,
    const QHash<famp::workspace::EntityId, QString>& assetOverrides) const
{
    document.mapScale = scaleCombox->currentText();
    document.projectCrs = projectCrs;
    document.graphicsState = ui.graphicsView->saveProjectState(errorMessage);
    if (document.graphicsState.isEmpty())
        return false;
    document.workspaceState = famp::workspace::serializeSnapshot(
        *workspaceStore, errorMessage, assetOverrides);
    if (document.workspaceState.isEmpty())
        return false;
    document.measurements3d = myVTK->measurements();
    document.windowGeometry = saveGeometry();
    document.windowState = saveState(famp::project::SchemaVersion);
    document.xoyLabelVisible = xoy_label && !xoy_label->isHidden();
    document.scaleVisible = scaleCombox && !scaleCombox->isHidden();
    for (const MyCloudList& cloud : pointCloudList)
    {
        const famp::workspace::WorkspaceEntity* entity =
            workspaceStore->entity(entityIdForCloud(cloud));
        const famp::workspace::EntityId entityId = entityIdForCloud(cloud);
        const QString overridePath = assetOverrides.value(entityId);
        const QString path = !overridePath.isEmpty()
            ? overridePath
            : (entity && entity->assetPath.has_value()
                   ? *entity->assetPath : cloud.layer.sourcePath);
        if (!path.isEmpty())
        {
            document.cloudFiles.append(path);
            famp::project::CloudReference reference;
            reference.path = path;
            reference.layerId = cloud.layer.id;
            reference.name = entity ? entity->name : cloud.layer.name;
            reference.crs = cloud.layer.crs;
            reference.visible = entity ? entity->visible : cloud.layer.visible;
            reference.locked = entity ? entity->locked : cloud.layer.locked;
            reference.spatial = cloud.layer.spatial;
            reference.display = cloud.layer.display;
            reference.attributes = cloud.layer.attributes.summaries();
            reference.archaeologyFields = cloud.layer.archaeologyFields;
            reference.controlPoints = cloud.layer.controlPoints;
            document.clouds.append(reference);
        }
    }
    return true;
}

bool MainWindow::prepareProjectAssets(
    const QString& projectPath,
    QHash<famp::workspace::EntityId, QString>& assetOverrides,
    QString* errorMessage) const
{
    assetOverrides.clear();
    const QFileInfo projectInfo(famp::project::pathWithProjectSuffix(projectPath));
    if (projectInfo.absoluteFilePath().isEmpty())
    {
        if (errorMessage)
            *errorMessage = tr("项目路径为空。");
        return false;
    }
    const QString assetsDirectoryPath = projectInfo.absoluteDir().filePath(
        projectInfo.completeBaseName() + QStringLiteral(".famp-assets"));
    QDir assetsDirectory(assetsDirectoryPath);
    const auto ensureAssetsDirectory = [&]() {
        return assetsDirectory.exists()
            || QDir().mkpath(assetsDirectoryPath);
    };

    for (const MyCloudList& cloud : pointCloudList)
    {
        const famp::workspace::EntityId id = entityIdForCloud(cloud);
        const famp::workspace::WorkspaceEntity* entity = workspaceStore->entity(id);
        if (!entity || entity->kind != famp::workspace::EntityKind::PointCloud)
            continue;
        const QString existingPath = entity->assetPath.has_value()
            ? entity->assetPath->trimmed() : cloud.layer.sourcePath.trimmed();
        if (!entity->provenance.has_value()
            && !existingPath.isEmpty() && QFileInfo::exists(existingPath))
            continue;
        if (!cloud.layer.points || cloud.layer.points->empty())
        {
            if (errorMessage)
                *errorMessage = tr("内存点云 %1 没有可保存的数据。")
                                    .arg(entity->name);
            return false;
        }
        if (!ensureAssetsDirectory())
        {
            if (errorMessage)
                *errorMessage = tr("无法创建项目资产目录：%1")
                                    .arg(assetsDirectoryPath);
            return false;
        }
        const QString outputPath = assetsDirectory.filePath(
            id.toString(QUuid::WithoutBraces).toLower()
            + QStringLiteral(".pcd"));
        QString saveError;
        if (!famp::io::savePcdAsciiAtomically(
                outputPath, *cloud.layer.points, &saveError,
                &cloud.layer.spatial, &cloud.layer.attributes))
        {
            if (errorMessage)
                *errorMessage = tr("保存项目资产 %1 失败：%2")
                                    .arg(entity->name, saveError);
            return false;
        }
        assetOverrides.insert(id, QFileInfo(outputPath).absoluteFilePath());
    }

    for (const famp::workspace::EntityId& id : workspaceStore->allEntityIds())
    {
        const famp::workspace::WorkspaceEntity* entity = workspaceStore->entity(id);
        if (!entity || entity->kind == famp::workspace::EntityKind::ProjectRoot
            || entity->kind == famp::workspace::EntityKind::Group
            || entity->kind == famp::workspace::EntityKind::PointCloud)
        {
            continue;
        }
        const QString existingPath = entity->assetPath.has_value()
            ? entity->assetPath->trimmed() : QString();
        if (!entity->provenance.has_value()
            && !existingPath.isEmpty() && QFileInfo::exists(existingPath))
            continue;

        QString suffix;
        bool supported = true;
        switch (entity->kind)
        {
        case famp::workspace::EntityKind::Dem:
        case famp::workspace::EntityKind::ContourSet:
            suffix = QStringLiteral(".famp-dem");
            break;
        case famp::workspace::EntityKind::Profile:
            suffix = QStringLiteral(".famp-profile");
            break;
        case famp::workspace::EntityKind::CutFill:
            suffix = QStringLiteral(".famp-volume");
            break;
        default:
            supported = false;
            break;
        }
        if (!supported)
            continue;
        if (!ensureAssetsDirectory())
        {
            if (errorMessage)
                *errorMessage = tr("无法创建项目资产目录：%1")
                                    .arg(assetsDirectoryPath);
            return false;
        }
        const QString outputPath = assetsDirectory.filePath(
            id.toString(QUuid::WithoutBraces).toLower() + suffix);
        QString saveError;
        bool saved = false;
        if (entity->kind == famp::workspace::EntityKind::Dem
            || entity->kind == famp::workspace::EntityKind::ContourSet)
        {
            const std::shared_ptr<famp::terrain::Result> result =
                entity->payloadAs<famp::terrain::Result>();
            saved = result && result->grid.isValid()
                && famp::terrainio::saveGridAtomically(
                    outputPath, result->grid, &saveError);
        }
        else if (entity->kind == famp::workspace::EntityKind::Profile)
        {
            const std::shared_ptr<famp::profile::Result> result =
                entity->payloadAs<famp::profile::Result>();
            saved = result && result->succeeded()
                && famp::profileio::saveResultAtomically(
                    outputPath, *result, &saveError);
        }
        else if (entity->kind == famp::workspace::EntityKind::CutFill)
        {
            const std::shared_ptr<famp::cutfill::Result> result =
                entity->payloadAs<famp::cutfill::Result>();
            saved = result && result->succeeded()
                && famp::cutfillio::saveResultAtomically(
                    outputPath, *result, &saveError);
        }
        if (!saved)
        {
            if (errorMessage)
            {
                *errorMessage = saveError.isEmpty()
                    ? tr("内存成果 %1 没有可保存的数据。").arg(entity->name)
                    : tr("保存项目资产 %1 失败：%2")
                          .arg(entity->name, saveError);
            }
            return false;
        }
        assetOverrides.insert(id, QFileInfo(outputPath).absoluteFilePath());
    }
    if (errorMessage)
        errorMessage->clear();
    return true;
}

void MainWindow::commitProjectAssets(
    const QHash<famp::workspace::EntityId, QString>& assetOverrides)
{
    const QScopedValueRollback<bool> guard(syncingWorkspaceEntity, true);
    for (auto iterator = assetOverrides.cbegin();
         iterator != assetOverrides.cend(); ++iterator)
    {
        const famp::workspace::WorkspaceEntity* current =
            workspaceStore->entity(iterator.key());
        if (!current)
            continue;
        famp::workspace::WorkspaceEntity saved = *current;
        saved.assetPath = iterator.value();
        saved.dirty = false;
        const std::shared_ptr<MyCloudList> payload = saved.payloadAs<MyCloudList>();
        if (payload)
        {
            MyCloudList cloud = *payload;
            cloud.layer.sourcePath = iterator.value();
            saved.setPayload(std::make_shared<MyCloudList>(cloud));
            for (MyCloudList& stored : pointCloudList)
            {
                if (stored.layer.id == cloud.layer.id)
                {
                    stored = cloud;
                    break;
                }
            }
        }
        workspaceStore->replaceEntity(saved);
    }
    for (const famp::workspace::EntityId& id : workspaceStore->allEntityIds())
        workspaceStore->setDirty(id, false);
}

QString MainWindow::recoveryProjectPath() const
{
    const QString recoveryDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/recovery");
    QDir().mkpath(recoveryDirectory);
    return recoveryDirectory + QStringLiteral("/autosave.famp");
}

bool MainWindow::saveProject(bool forceSaveAs)
{
    QString targetPath = currentProjectPath;
    if (forceSaveAs || targetPath.isEmpty())
    {
        const QString initialPath = targetPath.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                  + QStringLiteral("/FAMP-project.famp")
            : targetPath;
        targetPath = QFileDialog::getSaveFileName(
            this,
            tr("保存 FAMP 项目"),
            initialPath,
            tr("FAMP 项目 (*.famp)"));
        if (targetPath.isEmpty())
            return false;
    }
    return saveProjectToPath(famp::project::pathWithProjectSuffix(targetPath));
}

bool MainWindow::saveProjectToPath(const QString& path)
{
    QString error;
    QHash<famp::workspace::EntityId, QString> assetOverrides;
    if (!prepareProjectAssets(path, assetOverrides, &error))
    {
        QMessageBox::warning(this, tr("保存项目失败"), error);
        return false;
    }
    famp::project::Document document;
    if (!currentProjectDocument(document, &error, assetOverrides))
    {
        QMessageBox::warning(this, tr("保存项目失败"), error);
        return false;
    }
    if (!famp::project::save(
            path,
            document,
            QCoreApplication::applicationVersion(),
            &error))
    {
        QMessageBox::warning(this, tr("保存项目失败"), error);
        return false;
    }

    commitProjectAssets(assetOverrides);
    currentProjectPath = famp::project::pathWithProjectSuffix(path);
    projectDirty = false;
    ui.graphicsView->commandStack()->setClean();
    removeRecoveryProject();
    updateWindowTitle();
    emit sendStr2Console(tr("已保存项目  %1").arg(currentProjectPath));
    return true;
}

bool MainWindow::loadProjectFromPath(const QString& path, bool isRecovery)
{
    if (cloudLoadBusy)
    {
        statusBar()->showMessage(tr("请等待当前点云加载完成"), 5000);
        return false;
    }

    famp::project::Document document;
    QString error;
    if (!famp::project::load(path, document, &error))
    {
        QMessageBox::warning(this, tr("打开项目失败"), error);
        return false;
    }
    if (!document.projectCrs.isEmpty())
    {
        famp::crs::Info crsInfo;
        if (!famp::crs::inspect(document.projectCrs, crsInfo, &error))
        {
            QMessageBox::warning(this, tr("打开项目失败"), error);
            return false;
        }
    }
    if (!document.graphicsState.isEmpty()
        && !ui.graphicsView->validateProjectState(
            document.graphicsState, &error))
    {
        QMessageBox::warning(this, tr("打开项目失败"), error);
        return false;
    }
    relocateMissingProjectClouds(document, path);
    projectCloudReferences.clear();
    for (const famp::project::CloudReference& reference : document.clouds)
        projectCloudReferences.insert(reference.path, reference);

    loadingProject = true;
    clearWorkspace();
    pendingWorkspaceState = document.workspaceState;
    pendingProjectMeasurements3d = document.measurements3d;
    const int scaleIndex = scaleCombox->findText(document.mapScale);
    if (scaleIndex >= 0)
        scaleCombox->setCurrentIndex(scaleIndex);
    projectCrs = document.projectCrs;
    updateCrsStatus();
    if (!document.graphicsState.isEmpty()
        && !ui.graphicsView->restoreProjectState(
            document.graphicsState, &error))
    {
        loadingProject = false;
        QMessageBox::warning(this, tr("打开项目失败"), error);
        return false;
    }
    setXOYLabelVisible(document.xoyLabelVisible);
    setScaleVisible(document.scaleVisible);
    if (!document.windowGeometry.isEmpty())
        restoreGeometry(document.windowGeometry);
    if (!document.windowState.isEmpty())
        restoreState(document.windowState, famp::project::SchemaVersion);

    if (isRecovery)
    {
        QSettings settings;
        currentProjectPath = settings.value(
            QStringLiteral("MainWindow/recoveryOriginalProject")).toString();
    }
    else
    {
        currentProjectPath = QFileInfo(path).absoluteFilePath();
    }
    projectDirty = false;
    updateWindowTitle();

    beginCloudLoadBatch(document.cloudFiles, true, path, isRecovery);
    return true;
}

void MainWindow::relocateMissingProjectClouds(
    famp::project::Document& document,
    const QString& projectPath)
{
    if (document.clouds.isEmpty())
        return;

    QStringList resolvedFiles;
    for (famp::project::CloudReference& reference : document.clouds)
    {
        if (QFileInfo::exists(reference.path))
        {
            resolvedFiles.append(reference.path);
            continue;
        }

        const QString replacement = QFileDialog::getOpenFileName(
            this,
            tr("重新定位缺失点云：%1").arg(QFileInfo(reference.path).fileName()),
            QFileInfo(projectPath).absolutePath(),
            tr("点云文件 (*.pcd *.las *.laz *.ply *.xyz);;PCD (*.pcd);;LAS/LAZ (*.las *.laz);;PLY (*.ply);;XYZ (*.xyz)"));
        if (replacement.isEmpty())
        {
            resolvedFiles.append(reference.path);
            continue;
        }
        QString normalized;
        QString validationError;
        if (!famp::cloud::validatePath(
                replacement, &normalized, &validationError))
        {
            QMessageBox::warning(this, tr("无法重新定位点云"), validationError);
            resolvedFiles.append(reference.path);
            continue;
        }

        const QFileInfo replacementInfo(normalized);
        const bool metadataMismatch =
            (reference.size >= 0 && replacementInfo.size() != reference.size)
            || (reference.modifiedUtcMilliseconds >= 0
                && replacementInfo.lastModified().toUTC().toMSecsSinceEpoch()
                    != reference.modifiedUtcMilliseconds);
        if (metadataMismatch
            && QMessageBox::question(
                   this,
                   tr("点云元数据不一致"),
                   tr("所选文件的大小或修改时间与项目记录不同，是否仍使用该文件？\n%1")
                       .arg(normalized),
                   QMessageBox::Yes | QMessageBox::No,
                   QMessageBox::No) != QMessageBox::Yes)
        {
            resolvedFiles.append(reference.path);
            continue;
        }
        reference.path = normalized;
        reference.size = replacementInfo.size();
        reference.modifiedUtcMilliseconds =
            replacementInfo.lastModified().toUTC().toMSecsSinceEpoch();
        resolvedFiles.append(reference.path);
    }
    document.cloudFiles = resolvedFiles;
}

bool MainWindow::restoreWorkspaceAnalysisEntities(
    const famp::workspace::WorkspaceSnapshot& snapshot,
    QStringList& warnings)
{
    bool complete = true;
    const QScopedValueRollback<bool> guard(syncingWorkspaceEntity, true);
    for (const famp::workspace::SnapshotRecord& record : snapshot.entities)
    {
        if (record.kind != famp::workspace::EntityKind::Dem
            && record.kind != famp::workspace::EntityKind::ContourSet
            && record.kind != famp::workspace::EntityKind::Profile
            && record.kind != famp::workspace::EntityKind::CutFill)
        {
            continue;
        }
        const famp::workspace::WorkspaceEntity* current =
            workspaceStore->entity(record.id);
        if (!current)
        {
            warnings.append(tr("成果实体 %1 未能恢复到内容树。")
                                .arg(record.name));
            complete = false;
            continue;
        }
        if (!record.assetPath.has_value()
            || !QFileInfo::exists(*record.assetPath))
        {
            warnings.append(tr("成果 %1 的项目资产不存在：%2")
                                .arg(record.name,
                                     record.assetPath.value_or(tr("未记录"))));
            complete = false;
            continue;
        }

        famp::workspace::WorkspaceEntity replacement = *current;
        QString loadError;
        bool loaded = false;
        if (record.kind == famp::workspace::EntityKind::Dem
            || record.kind == famp::workspace::EntityKind::ContourSet)
        {
            famp::terrain::Grid grid;
            loaded = famp::terrainio::loadGrid(
                *record.assetPath, grid, &loadError);
            if (loaded)
            {
                auto result = std::make_shared<famp::terrain::Result>();
                result->grid = std::move(grid);
                result->suggestedResolution = result->grid.resolution;
                if (record.kind
                    == famp::workspace::EntityKind::ContourSet)
                {
                    const QJsonObject parameters = record.provenance.has_value()
                        ? record.provenance->parameters : QJsonObject();
                    const QJsonValue interval = parameters.value(
                        QStringLiteral("contourInterval"));
                    const QJsonValue base = parameters.value(
                        QStringLiteral("contourBase"));
                    const QJsonValue smoothing = parameters.value(
                        QStringLiteral("smoothingIterations"));
                    if (!interval.isDouble() || !base.isDouble()
                        || !smoothing.isDouble())
                    {
                        loaded = false;
                        loadError = tr("等高线重建参数缺失。");
                    }
                    else
                    {
                        famp::terrain::ContourOptions options;
                        options.automaticInterval = false;
                        options.interval = interval.toDouble();
                        options.automaticBase = false;
                        options.baseElevation = base.toDouble();
                        options.smoothingIterations = smoothing.toInt(-1);
                        loaded = famp::terrain::generateContours(
                            result->grid, options, result->contours,
                            &result->contourInterval,
                            &result->contourBase, &loadError);
                    }
                }
                if (loaded)
                    replacement.setPayload(std::move(result));
            }
        }
        else if (record.kind == famp::workspace::EntityKind::Profile)
        {
            auto result = std::make_shared<famp::profile::Result>();
            loaded = famp::profileio::loadResult(
                *record.assetPath, *result, &loadError);
            if (loaded)
                replacement.setPayload(std::move(result));
        }
        else if (record.kind == famp::workspace::EntityKind::CutFill)
        {
            auto result = std::make_shared<famp::cutfill::Result>();
            loaded = famp::cutfillio::loadResult(
                *record.assetPath, *result, &loadError);
            if (loaded)
                replacement.setPayload(std::move(result));
        }

        if (!loaded)
        {
            warnings.append(tr("成果 %1 加载失败：%2")
                                .arg(record.name, loadError));
            complete = false;
            continue;
        }
        replacement.assetPath = *record.assetPath;
        replacement.dirty = false;
        QString replaceError;
        if (!workspaceStore->replaceEntity(replacement, &replaceError))
        {
            warnings.append(tr("成果 %1 恢复失败：%2")
                                .arg(record.name, replaceError));
            complete = false;
        }
    }
    return complete;
}

bool MainWindow::maybeSaveCurrentProject()
{
    if (!projectDirty)
        return true;

    QMessageBox prompt(QMessageBox::Warning,
                       tr("项目尚未保存"),
                       tr("当前项目包含尚未保存的内存成果或编辑。"),
                       QMessageBox::NoButton,
                       this);
    QPushButton* saveButton = prompt.addButton(
        tr("保存项目与资产"), QMessageBox::AcceptRole);
    QPushButton* discardButton = prompt.addButton(
        tr("丢弃更改"), QMessageBox::DestructiveRole);
    QPushButton* cancelButton = prompt.addButton(
        tr("取消"), QMessageBox::RejectRole);
    prompt.setDefaultButton(saveButton);
    prompt.exec();

    if (prompt.clickedButton() == cancelButton || !prompt.clickedButton())
        return false;
    if (prompt.clickedButton() == saveButton)
        return saveProject(false);
    return prompt.clickedButton() == discardButton;
}

void MainWindow::removeCloudFromWorkspace(const MyCloudList& cloud)
{
    if (cloud.cloudactor)
    {
        myVTK->unregisterCloudActor(cloud.cloudactor);
        cloud.cloudactor->Delete();
    }
    if (cloud.AABBactor)
    {
        myVTK->removeAABBDisplay(cloud.AABBactor);
        cloud.AABBactor->Delete();
    }
    pointCloudList.erase(
        std::remove_if(pointCloudList.begin(), pointCloudList.end(),
                       [&cloud](const MyCloudList& candidate) {
                           return candidate.id == cloud.id;
                       }),
        pointCloudList.end());
}

void MainWindow::clearWorkspace()
{
    closeProjectionDecisionDialog();
    while (!pointCloudList.empty())
        removeCloudFromWorkspace(pointCloudList.back());
    workspaceStore->clear(tr("未命名项目"));
    ui.graphicsView->clearSceneAndHistory();
    pendingProjectMeasurements3d.clear();
    pendingWorkspaceState = {};
    activeVtkSourceId = {};
    projectionWorkflow.clearSource();
    ui.graphicsView->clearProjectionInput();
    myVTK->clearProjectionPreview();

    inCloud.reset();
    delete myCloud;
    myCloud = nullptr;
    projectCrs.clear();
    updateCrsStatus();
    isAABB = false;
    ui.actDelete->setEnabled(false);
    ui.actAABB->setEnabled(false);
    ui.actRandomPlane->setEnabled(false);
    ui.actVerticalPlane->setEnabled(false);
    ui.actHorizonalPlane->setEnabled(false);
    scaleCombox->setCurrentIndex(2);
    myVTK->initCamera();
    myVTK->update();
    if (cloudDisplaySettingsAction)
        cloudDisplaySettingsAction->setEnabled(false);
    if (preprocessCloudAction)
        preprocessCloudAction->setEnabled(false);
    if (cropCloudAction)
        cropCloudAction->setEnabled(false);
    if (registerCloudAction)
        registerCloudAction->setEnabled(false);
    if (cloudCoordinateAction)
        cloudCoordinateAction->setEnabled(false);
    if (reprojectCloudAction)
        reprojectCloudAction->setEnabled(false);
    if (archaeologyMetadataAction)
        archaeologyMetadataAction->setEnabled(false);
    if (controlPointsAction)
        controlPointsAction->setEnabled(false);
    if (terrainAnalysisAction)
        terrainAnalysisAction->setEnabled(false);
    if (cutFillAction)
        cutFillAction->setEnabled(false);
    if (cloudProfileAction)
        cloudProfileAction->setEnabled(false);
    updateProjectionActions();
    updateArchaeologyWorkflowGuide();
}

void MainWindow::markProjectDirty()
{
    if (loadingProject || projectDirty)
        return;
    projectDirty = true;
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    QString title = QStringLiteral("FAMP %1")
                        .arg(QCoreApplication::applicationVersion());
    if (!currentProjectPath.isEmpty())
        title += QStringLiteral(" — %1").arg(QFileInfo(currentProjectPath).fileName());
    if (projectDirty)
        title += QStringLiteral(" *");
    setWindowTitle(title);
}

void MainWindow::updateCrsStatus()
{
    if (!crsStatusLabel)
        return;
    if (projectCrs.isEmpty())
    {
        crsStatusLabel->setText(tr("CRS: 未设置"));
        crsStatusLabel->setToolTip(
            tr("未为当前项目声明坐标参考系。"));
        return;
    }

    famp::crs::Info info;
    QString error;
    if (famp::crs::inspect(projectCrs, info, &error))
    {
        crsStatusLabel->setText(QStringLiteral("CRS: %1").arg(info.identifier));
        crsStatusLabel->setToolTip(
            QStringLiteral("%1 — %2 (%3)")
                .arg(info.identifier, info.name, info.type));
    }
    else
    {
        crsStatusLabel->setText(QStringLiteral("CRS: %1").arg(projectCrs));
        crsStatusLabel->setToolTip(error);
    }
}

void MainWindow::removeRecoveryProject()
{
    const QFileInfo recoveryInfo(recoveryProjectPath());
    QFile::remove(recoveryInfo.absoluteFilePath());
    QDir(recoveryInfo.absoluteDir().filePath(
             recoveryInfo.completeBaseName() + QStringLiteral(".famp-assets")))
        .removeRecursively();
    QSettings settings;
    settings.remove(QStringLiteral("MainWindow/recoveryOriginalProject"));
}

void MainWindow::checkForRecoveryProject()
{
    const QString recoveryPath = recoveryProjectPath();
    if (!QFileInfo::exists(recoveryPath))
        return;

    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        tr("恢复自动保存"),
        tr("发现上次未正常关闭时的自动保存，是否恢复？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (choice == QMessageBox::Yes)
        loadProjectFromPath(recoveryPath, true);
    else
        removeRecoveryProject();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (cloudLoadBusy)
    {
        statusBar()->showMessage(tr("点云仍在后台加载，请等待加载完成后再退出"), 8000);
        event->ignore();
        return;
    }

    if (maybeSaveCurrentProject())
    {
        if (projectDirty)
            removeRecoveryProject();
        event->accept();
    }
    else
        event->ignore();
}

void MainWindow::slotNewProject()
{
    if (!maybeSaveCurrentProject())
        return;

    loadingProject = true;
    clearWorkspace();
    loadingProject = false;
    currentProjectPath.clear();
    projectDirty = false;
    removeRecoveryProject();
    updateWindowTitle();
    emit sendStr2Console(tr("已新建空项目"));
}

void MainWindow::slotOpenProject()
{
    if (!maybeSaveCurrentProject())
        return;

    QSettings settings;
    const QString initialDirectory = settings.value(
        QStringLiteral("MainWindow/lastProjectDirectory"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("打开 FAMP 项目"),
        initialDirectory,
        tr("FAMP 项目 (*.famp)"));
    if (path.isEmpty())
        return;

    if (loadProjectFromPath(path))
    {
        settings.setValue(QStringLiteral("MainWindow/lastProjectDirectory"),
                          QFileInfo(path).absolutePath());
        removeRecoveryProject();
    }
}

void MainWindow::slotSaveProject()
{
    saveProject(false);
}

void MainWindow::slotSaveProjectAs()
{
    saveProject(true);
}

void MainWindow::slotAutosaveProject()
{
    if (loadingProject || cloudLoadBusy || !projectDirty)
        return;

    const QString autosavePath = recoveryProjectPath();
    QString error;
    QHash<famp::workspace::EntityId, QString> assetOverrides;
    if (!prepareProjectAssets(autosavePath, assetOverrides, &error))
    {
        statusBar()->showMessage(tr("自动保存失败：%1").arg(error), 8000);
        return;
    }
    famp::project::Document document;
    if (!currentProjectDocument(document, &error, assetOverrides))
    {
        statusBar()->showMessage(tr("自动保存失败：%1").arg(error), 8000);
        return;
    }
    if (!famp::project::save(
            autosavePath,
            document,
            QCoreApplication::applicationVersion(),
            &error))
    {
        statusBar()->showMessage(tr("自动保存失败：%1").arg(error), 8000);
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("MainWindow/recoveryOriginalProject"),
                      currentProjectPath);
    statusBar()->showMessage(tr("项目已自动保存"), 3000);
}

void MainWindow::slotSetProjectCrs()
{
    bool accepted = false;
    const QString input = QInputDialog::getText(
        this,
        tr("项目坐标参考系"),
        tr("输入 EPSG 编码（留空可清除）："),
        QLineEdit::Normal,
        projectCrs.isEmpty() ? QStringLiteral("EPSG:4490") : projectCrs,
        &accepted);
    if (!accepted)
        return;

    if (input.trimmed().isEmpty())
    {
        if (!projectCrs.isEmpty())
        {
            const QString previous = projectCrs;
            ui.graphicsView->commandStack()->push(
                famp::graphics::makeCallbackCommand(
                    [this, previous]() { applyProjectCrs(previous); },
                    [this]() { applyProjectCrs(QString()); },
                    tr("清除项目坐标系")));
        }
        return;
    }

    famp::crs::Info info;
    QString error;
    if (!famp::crs::inspect(input, info, &error))
    {
        QMessageBox::warning(this, tr("坐标系无效"), error);
        return;
    }

    if (projectCrs == info.identifier)
        return;
    const QString previous = projectCrs;
    const QString requested = info.identifier;
    ui.graphicsView->commandStack()->push(
        famp::graphics::makeCallbackCommand(
            [this, previous]() { applyProjectCrs(previous); },
            [this, requested]() { applyProjectCrs(requested); },
            tr("更改项目坐标系")));
    statusBar()->showMessage(
        tr("项目 CRS 已设置为 %1 — %2；已加载点云坐标未被修改。")
            .arg(info.identifier, info.name),
        8000);
}

void MainWindow::applyProjectCrs(const QString& crs)
{
    projectCrs = crs;
    updateCrsStatus();
    markProjectDirty();
}

void MainWindow::slotOpenCoordinateConverter()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("坐标转换器"));
    dialog.resize(560, 330);

    QFormLayout layout(&dialog);
    QLineEdit sourceCrsEdit(
        projectCrs.isEmpty() ? QStringLiteral("EPSG:4326") : projectCrs,
        &dialog);
    QLineEdit targetCrsEdit(QStringLiteral("EPSG:3857"), &dialog);
    QDoubleSpinBox xInput(&dialog);
    QDoubleSpinBox yInput(&dialog);
    QDoubleSpinBox zInput(&dialog);
    for (QDoubleSpinBox* input : {&xInput, &yInput, &zInput})
    {
        input->setDecimals(10);
        input->setRange(-1.0e12, 1.0e12);
    }
    xInput.setValue(118.0);
    yInput.setValue(32.0);

    QLabel hint(tr("地理坐标系按 X=经度、Y=纬度输入。该工具只转换单点，不修改已加载点云。"), &dialog);
    hint.setWordWrap(true);
    QLabel result(tr("输入坐标后点击“转换”。"), &dialog);
    result.setTextInteractionFlags(Qt::TextSelectableByMouse);
    result.setWordWrap(true);

    layout.addRow(tr("源 CRS"), &sourceCrsEdit);
    layout.addRow(tr("目标 CRS"), &targetCrsEdit);
    layout.addRow(tr("X"), &xInput);
    layout.addRow(tr("Y"), &yInput);
    layout.addRow(tr("Z"), &zInput);
    layout.addRow(&hint);
    layout.addRow(tr("结果"), &result);

    QDialogButtonBox buttons(QDialogButtonBox::Close, &dialog);
    QPushButton* transformButton = buttons.addButton(
        tr("转换"), QDialogButtonBox::ActionRole);
    layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(transformButton, &QPushButton::clicked, &dialog, [&]() {
        const famp::crs::Coordinate source{
            xInput.value(), yInput.value(), zInput.value()};
        famp::crs::Coordinate target;
        QString error;
        if (!famp::crs::transform(
                sourceCrsEdit.text(), targetCrsEdit.text(), source, target, &error))
        {
            result.setText(tr("转换失败：%1").arg(error));
            return;
        }
        result.setText(
            QStringLiteral("X = %1\nY = %2\nZ = %3")
                .arg(QString::number(target.x, 'g', 15),
                     QString::number(target.y, 'g', 15),
                     QString::number(target.z, 'g', 15)));
    });
    dialog.exec();
}

void MainWindow::slotReprojectCloud()
{
    MyCloudList cloud;
    if (!selectedCloudData(cloud))
    {
        QMessageBox::information(
            this, tr("点云重投影"), tr("请先在内容列表中选择点云。"));
        return;
    }
    if (cloud.layer.locked)
    {
        QMessageBox::information(
            this, tr("点云重投影"), tr("所选图层已锁定，无法修改。"));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("重投影所选点云"));
    QFormLayout layout(&dialog);
    QLineEdit sourceCrsEdit(
        cloud.layer.crs.isEmpty()
            ? (projectCrs.isEmpty() ? QStringLiteral("EPSG:4490") : projectCrs)
            : cloud.layer.crs,
        &dialog);
    QLineEdit targetCrsEdit(
        !projectCrs.isEmpty() && projectCrs != sourceCrsEdit.text()
            ? projectCrs : QStringLiteral("EPSG:3857"),
        &dialog);
    QLabel explanation(
        tr("程序将逐点转换真实坐标并重新中心化，以保留大坐标下的局部精度。"
           "结果作为新的内存点云加入左侧内容列表，不会自动写入磁盘。"
           "原有逐点属性会按原类型和顺序无损保留；"
           "来源点云会自动隐藏，需要落盘时可对成果执行“保存所选实体”。"),
        &dialog);
    explanation.setWordWrap(true);
    layout.addRow(tr("源 CRS"), &sourceCrsEdit);
    layout.addRow(tr("目标 CRS"), &targetCrsEdit);
    layout.addRow(&explanation);
    QDialogButtonBox buttons(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons.button(QDialogButtonBox::Ok)->setText(tr("生成内存成果"));
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout.addRow(&buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    famp::crs::Info sourceInfo;
    famp::crs::Info targetInfo;
    QString error;
    if (!famp::crs::inspect(sourceCrsEdit.text(), sourceInfo, &error)
        || !famp::crs::inspect(targetCrsEdit.text(), targetInfo, &error))
    {
        QMessageBox::warning(this, tr("点云重投影"), error);
        return;
    }
    if (sourceInfo.identifier == targetInfo.identifier)
    {
        QMessageBox::information(
            this, tr("点云重投影"), tr("源 CRS 与目标 CRS 相同，无需重投影。"));
        return;
    }

    QString targetSuffix = targetInfo.identifier;
    targetSuffix.replace(QLatin1Char(':'), QLatin1Char('_'));
    const QString resultName = derivedEntityName(
        cloud.layer.name,
        QStringLiteral("_reprojected_") + targetSuffix);

    const famp::tasks::Handle task = taskManager->start(
        tr("重投影点云"),
        tr("%1 → %2").arg(sourceInfo.identifier, targetInfo.identifier));
    if (!task.isValid())
    {
        QMessageBox::warning(
            this, tr("点云重投影"), tr("无法创建后台重投影任务。"));
        return;
    }

    QProgressDialog progress(
        tr("正在重投影点云…"), tr("取消"), 0, 100, this);
    progress.setWindowTitle(tr("点云重投影"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    connect(&progress, &QProgressDialog::canceled, this, [this, task]() {
        taskManager->requestCancellation(task.id);
    });
    const QMetaObject::Connection taskProgressConnection = connect(
        taskManager, &famp::tasks::TaskManager::taskChanged,
        &progress, [this, task, &progress](quint64 id) {
            if (id != task.id)
                return;
            famp::tasks::Snapshot snapshot;
            if (!taskManager->snapshot(id, snapshot))
                return;
            progress.setValue(static_cast<int>(std::clamp(
                snapshot.progress * 100.0, 0.0, 100.0)));
            if (!snapshot.message.isEmpty())
                progress.setLabelText(snapshot.message);
        });

    QFutureWatcher<famp::cloud::ReprojectionResult> watcher;
    connect(&watcher,
            &QFutureWatcher<famp::cloud::ReprojectionResult>::finished,
            &progress, &QProgressDialog::accept);
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr input = cloud.layer.points;
    const famp::cloud::SpatialReference inputSpatial = cloud.layer.spatial;
    const QString sourceCrs = sourceInfo.identifier;
    const QString targetCrs = targetInfo.identifier;
    watcher.setFuture(QtConcurrent::run(
        [this, task, input, inputSpatial, sourceCrs, targetCrs]() {
            auto result = famp::cloud::reproject(
                input, inputSpatial, sourceCrs, targetCrs,
                task.cancellationCheck(),
                [this, task](double value) {
                    taskManager->setProgress(
                        task.id, value,
                        tr("正在转换点云坐标：%1%")
                            .arg(static_cast<int>(value * 100.0)));
                });
            return result;
        }));
    if (!watcher.isFinished())
        progress.exec();
    const famp::cloud::ReprojectionResult result = watcher.result();
    disconnect(taskProgressConnection);

    if (result.cancelled)
    {
        const QString message = result.error.isEmpty()
            ? tr("点云重投影已取消") : result.error;
        taskManager->acknowledgeCancellation(task.id, message);
        statusBar()->showMessage(message, 5000);
        emit sendStr2Console(message);
        return;
    }
    if (!result.succeeded())
    {
        taskManager->fail(
            task.id, result.error.isEmpty() ? tr("点云重投影失败") : result.error);
        QMessageBox::warning(
            this, tr("点云重投影"),
            result.error.isEmpty() ? tr("点云重投影失败。") : result.error);
        return;
    }
    const famp::workspace::WorkspaceEntity* sourceEntity =
        workspaceStore->entity(entityIdForCloud(cloud));
    if (!sourceEntity)
    {
        taskManager->fail(task.id, tr("来源点云实体已不存在"));
        return;
    }
    famp::workspace::Provenance provenance;
    provenance.operation = QStringLiteral("reproject");
    provenance.sourceIds = {sourceEntity->id};
    provenance.sourceSnapshot = sourceSnapshot(*sourceEntity, cloud);
    provenance.parameters = QJsonObject{
        {QStringLiteral("sourceCrs"), result.sourceCrs},
        {QStringLiteral("targetCrs"), result.targetCrs}};
    if (!integrateDerivedCloud(
            cloud, resultName, result.points, result.spatial,
            result.targetCrs, cloud.layer.attributes,
            std::move(provenance)))
    {
        taskManager->fail(task.id, tr("无法将重投影成果加入内容列表"));
        return;
    }
    taskManager->succeed(task.id, tr("点云重投影完成"));
    statusBar()->showMessage(
        tr("已生成 %1 重投影内存成果。")
            .arg(result.targetCrs),
        8000);
    emit sendStr2Console(
        tr("点云重投影完成：%1 → %2，%3 个点，成果保留在内存中")
            .arg(result.sourceCrs, result.targetCrs)
            .arg(result.points->size()));
}

bool MainWindow::selectedCloudData(MyCloudList& cloud, QString* path) const
{
    const QModelIndex index = ui.treeView->currentIndex();
    if (!index.isValid())
        return false;

    const famp::workspace::WorkspaceEntity* entity = model->entity(index);
    if (!entity || entity->kind != famp::workspace::EntityKind::PointCloud)
        return false;
    const std::shared_ptr<MyCloudList> payload = entity->payloadAs<MyCloudList>();
    if (!payload)
        return false;
    cloud = *payload;
    cloud.layer.name = entity->name;
    cloud.layer.visible = entity->visible;
    cloud.layer.locked = entity->locked;
    if (!cloud.layer.points || cloud.layer.points->empty() || !cloud.cloudactor)
        return false;

    if (path)
        *path = entity->assetPath.has_value()
            ? *entity->assetPath : cloud.layer.sourcePath;
    return true;
}

bool MainWindow::applyCloudLayerState(
    const QString& layerId,
    const famp::cloud::CloudLayer& state)
{
    QString validationError;
    if (!famp::cloud::validateLayer(state, true, &validationError))
        return false;

    auto found = std::find_if(
        pointCloudList.begin(), pointCloudList.end(),
        [&layerId](const MyCloudList& cloud) {
            return cloud.layer.id == layerId;
        });
    if (found == pointCloudList.end()
        || !myVTK->updateCloudActors(
            found->cloudactor, found->AABBactor, state.points))
    {
        return false;
    }

    if (!myVTK->setCloudActorMetadata(
            found->cloudactor, state.id, state.crs, &validationError))
    {
        return false;
    }
    if (state.display.colorMode == famp::display::ColorMode::Attribute
        && !famp::display::attachAttribute(
            found->cloudactor, state.attributes,
            state.display.attributeName, &validationError))
    {
        return false;
    }

    vtkNew<vtkMatrix4x4> cloudTransform;
    setCenteredSpatialMatrix(cloudTransform, state.spatial);
    found->cloudactor->SetUserMatrix(cloudTransform);
    found->AABBactor->SetUserMatrix(cloudTransform);
    if (!famp::display::apply(
            found->cloudactor, state.display, &validationError))
    {
        return false;
    }

    found->layer = state;
    const MyCloudList updated = *found;
    inCloud = state.points;
    emit sendOrignalCloud(inCloud);
    updateCloudData(updated);
    updateCloudToolActions();
    markProjectDirty();
    myVTK->refresh();
    return true;
}

bool MainWindow::applyCloudMetadataState(
    const QString& layerId,
    const famp::cloud::CloudLayer& state)
{
    QString validationError;
    if (state.id != layerId
        || !famp::cloud::validateLayer(state, true, &validationError))
    {
        return false;
    }
    auto found = std::find_if(
        pointCloudList.begin(), pointCloudList.end(),
        [&layerId](const MyCloudList& cloud) {
            return cloud.layer.id == layerId;
        });
    if (found == pointCloudList.end())
        return false;

    found->layer = state;
    updateCloudData(*found);
    updateCloudToolActions();
    markProjectDirty();
    return true;
}

void MainWindow::updateCloudData(const MyCloudList& cloud)
{
    for (MyCloudList& stored : pointCloudList)
    {
        if (stored.id == cloud.id)
        {
            stored = cloud;
            break;
        }
    }

    const famp::workspace::EntityId id = entityIdForCloud(cloud);
    const famp::workspace::WorkspaceEntity* storedEntity =
        workspaceStore->entity(id);
    if (!storedEntity)
        return;
    famp::workspace::WorkspaceEntity updated = *storedEntity;
    updated.name = cloud.layer.name;
    updated.visible = cloud.layer.visible;
    updated.locked = cloud.layer.locked;
    updated.dirty = true;
    if (cloud.layer.sourcePath.isEmpty())
        updated.assetPath.reset();
    else
        updated.assetPath = cloud.layer.sourcePath;
    updated.setPayload(std::make_shared<MyCloudList>(cloud));
    const QScopedValueRollback<bool> guard(syncingWorkspaceEntity, true);
    workspaceStore->replaceEntity(updated);
}

famp::workspace::EntityId MainWindow::entityIdForCloud(
    const MyCloudList& cloud) const
{
    return QUuid(cloud.layer.id);
}

QVector<famp::workspace::EntityId> MainWindow::selectedEntityIds() const
{
    QVector<famp::workspace::EntityId> result;
    if (!ui.treeView->selectionModel())
        return result;
    const QModelIndexList rows = ui.treeView->selectionModel()->selectedRows();
    for (const QModelIndex& index : rows)
    {
        const famp::workspace::EntityId id = model->entityId(index);
        if (!id.isNull() && !result.contains(id))
            result.append(id);
    }
    if (result.isEmpty())
    {
        const famp::workspace::EntityId id =
            model->entityId(ui.treeView->currentIndex());
        if (!id.isNull())
            result.append(id);
    }
    return result;
}

void MainWindow::synchronizeWorkspaceEntity(
    const famp::workspace::EntityId& id,
    const QVector<int>&)
{
    if (syncingWorkspaceEntity)
        return;
    const famp::workspace::WorkspaceEntity* entity = workspaceStore->entity(id);
    if (!entity)
        return;
    if (entity->kind == famp::workspace::EntityKind::Measurement3D)
    {
        const QScopedValueRollback<bool> guard(syncingMeasurementEntities, true);
        QString renderError;
        if (!entityRenderers.applyVisibility(*entity, &renderError)
            && !renderError.isEmpty())
        {
            emit sendStr2Console(
                tr("三维测量可见性同步失败：%1").arg(renderError));
        }
        markProjectDirty();
        return;
    }
    if (entity->kind == famp::workspace::EntityKind::Measurement2D
        || entity->kind == famp::workspace::EntityKind::GraphicsItem)
    {
        const QScopedValueRollback<bool> guard(syncingGraphicsEntities, true);
        QString renderError;
        if (!entityRenderers.applyVisibility(*entity, &renderError)
            && !renderError.isEmpty())
        {
            emit sendStr2Console(
                tr("二维图元可见性同步失败：%1").arg(renderError));
        }
        markProjectDirty();
        return;
    }
    if (entity->kind != famp::workspace::EntityKind::PointCloud)
    {
        markProjectDirty();
        return;
    }
    const std::shared_ptr<MyCloudList> payload = entity->payloadAs<MyCloudList>();
    if (!payload)
        return;

    MyCloudList updated = *payload;
    updated.layer.name = entity->name;
    updated.layer.visible = entity->visible;
    updated.layer.locked = entity->locked;
    if (entity->assetPath.has_value())
        updated.layer.sourcePath = *entity->assetPath;

    for (MyCloudList& stored : pointCloudList)
    {
        if (stored.layer.id == updated.layer.id)
        {
            stored = updated;
            break;
        }
    }

    QString renderError;
    if (!entityRenderers.applyVisibility(*entity, &renderError)
        && !renderError.isEmpty())
    {
        emit sendStr2Console(tr("实体可见性同步失败：%1").arg(renderError));
    }
    if (myVTK)
        myVTK->update();

    famp::workspace::WorkspaceEntity replacement = *entity;
    replacement.setPayload(std::make_shared<MyCloudList>(updated));
    const QScopedValueRollback<bool> guard(syncingWorkspaceEntity, true);
    workspaceStore->replaceEntity(replacement);
    markProjectDirty();
}

void MainWindow::updateEntityProperties()
{
    if (!entityProperties)
        return;
    const QSignalBlocker blocker(entityProperties);
    entityProperties->clear();
    const famp::workspace::WorkspaceEntity* entity =
        model->entity(ui.treeView->currentIndex());
    if (!entity)
        return;

    auto addProperty = [this](const QString& name, const QString& value) {
        auto* item = new QTreeWidgetItem(entityProperties);
        item->setText(0, name);
        item->setText(1, value);
        item->setToolTip(1, value);
    };
    addProperty(tr("名称"), entity->name);
    addProperty(tr("类型"), famp::workspace::entityKindName(entity->kind));
    addProperty(tr("实体 ID"), entity->id.toString(QUuid::WithoutBraces));
    addProperty(tr("可见"), entity->visible ? tr("是") : tr("否"));
    addProperty(tr("锁定"), entity->locked ? tr("是") : tr("否"));
    addProperty(tr("保存状态"), entity->dirty ? tr("未保存") : tr("已保存"));
    addProperty(tr("资产"), entity->assetPath.has_value()
        ? *entity->assetPath : tr("内存中（尚未落盘）"));
    if (entity->provenance.has_value())
    {
        addProperty(tr("生成操作"), entity->provenance->operation);
        addProperty(tr("来源数量"),
                    QString::number(entity->provenance->sourceIds.size()));
        addProperty(tr("生成时间"),
                    entity->provenance->createdAt.toLocalTime()
                        .toString(Qt::ISODate));
    }
    if (entity->kind == famp::workspace::EntityKind::PointCloud)
    {
        const std::shared_ptr<MyCloudList> cloud = entity->payloadAs<MyCloudList>();
        if (cloud && cloud->layer.points)
        {
            addProperty(tr("点数"),
                        QString::number(cloud->layer.points->size()));
            addProperty(tr("CRS"), cloud->layer.crs.isEmpty()
                ? tr("未声明") : cloud->layer.crs);
            addProperty(tr("逐点属性"),
                        QString::number(cloud->layer.attributes.size()));
        }
    }
    else if (entity->kind == famp::workspace::EntityKind::Dem
            || entity->kind == famp::workspace::EntityKind::ContourSet)
    {
        const std::shared_ptr<famp::terrain::Result> terrain =
            entity->payloadAs<famp::terrain::Result>();
        if (terrain)
        {
            addProperty(tr("网格尺寸"), QStringLiteral("%1 × %2")
                .arg(terrain->grid.columns).arg(terrain->grid.rows));
            addProperty(tr("网格分辨率（米）"), QString::number(
                terrain->grid.resolution
                    * terrain->grid.horizontalUnitToMetre,
                'g', 10));
            addProperty(tr("有效单元"), QString::number(
                terrain->grid.populatedCellCount
                    + terrain->grid.filledCellCount));
            addProperty(tr("等高线数量"),
                        QString::number(terrain->contours.size()));
            addProperty(tr("CRS"), terrain->grid.sourceCrs.isEmpty()
                ? tr("未声明") : terrain->grid.sourceCrs);
        }
    }
    else if (entity->kind == famp::workspace::EntityKind::Profile)
    {
        const std::shared_ptr<famp::profile::Result> profile =
            entity->payloadAs<famp::profile::Result>();
        if (profile)
        {
            addProperty(tr("剖面长度（米）"), QString::number(
                profile->length * profile->horizontalUnitToMetre,
                'g', 10));
            addProperty(tr("走廊点数"),
                        QString::number(profile->selectedPointCount));
            addProperty(tr("有效采样段"), QStringLiteral("%1 / %2")
                .arg(profile->populatedBinCount).arg(profile->bins.size()));
            addProperty(tr("代表高程"),
                        famp::profile::statisticName(profile->statistic));
        }
    }
    else if (entity->kind == famp::workspace::EntityKind::CutFill)
    {
        const std::shared_ptr<famp::cutfill::Result> cutFill =
            entity->payloadAs<famp::cutfill::Result>();
        if (cutFill)
        {
            addProperty(tr("对比单元"),
                        QString::number(cutFill->comparedCellCount));
            addProperty(tr("挖方体积（m³）"), QString::number(
                cutFill->cutVolumeCubicMetres, 'g', 12));
            addProperty(tr("填方体积（m³）"), QString::number(
                cutFill->fillVolumeCubicMetres, 'g', 12));
            addProperty(tr("净体积（m³）"), QString::number(
                cutFill->signedVolumeCubicMetres, 'g', 12));
        }
    }
    else if (entity->kind == famp::workspace::EntityKind::Measurement3D)
    {
        const std::shared_ptr<famp::measurement::Record3D> measurement =
            entity->payloadAs<famp::measurement::Record3D>();
        if (measurement)
        {
            addProperty(tr("测量类型"),
                        famp::measurement::kindName(measurement->kind));
            addProperty(tr("测量结果"),
                        famp::measurement::formatSummary(
                            measurement->kind, measurement->points));
            addProperty(tr("测量点数"),
                        QString::number(measurement->points.size()));
            addProperty(tr("关联点云"), measurement->layerId);
            addProperty(tr("CRS"), measurement->crs.isEmpty()
                ? tr("未声明") : measurement->crs);
        }
    }
    else if (entity->kind == famp::workspace::EntityKind::Measurement2D
             || entity->kind == famp::workspace::EntityKind::GraphicsItem)
    {
        addProperty(tr("画布图元 ID"), entity->display.value(
            QStringLiteral("itemId")).toString());
        const std::shared_ptr<GraphicsEntityPayload> payload =
            entity->payloadAs<GraphicsEntityPayload>();
        QGraphicsItem* item = payload && payload->handle
            ? payload->handle->item : nullptr;
        if (const auto* measurement =
                dynamic_cast<const MeasurementItem*>(item))
        {
            addProperty(tr("测量类型"),
                        famp::measurement::kindName(measurement->kind()));
            addProperty(tr("测量结果"),
                        famp::measurement::formatValue(
                            measurement->kind(), measurement->value()));
            addProperty(tr("测量点数"),
                        QString::number(measurement->meterPoints().size()));
        }
        else if (item)
        {
            const QRectF bounds = item->sceneBoundingRect();
            addProperty(tr("画布范围"),
                        QStringLiteral("%1 × %2")
                            .arg(bounds.width(), 0, 'g', 8)
                            .arg(bounds.height(), 0, 'g', 8));
        }
    }
    entityProperties->resizeColumnToContents(0);
}

void MainWindow::showEntityContextMenu(const QPoint& position)
{
    const QModelIndex clicked = ui.treeView->indexAt(position);
    if (clicked.isValid()
        && !ui.treeView->selectionModel()->isSelected(clicked))
    {
        ui.treeView->setCurrentIndex(clicked);
        ui.treeView->selectionModel()->select(
            clicked, QItemSelectionModel::ClearAndSelect
                | QItemSelectionModel::Rows);
    }

    const QVector<famp::workspace::EntityId> ids = selectedEntityIds();
    const famp::workspace::WorkspaceEntity* current =
        model->entity(ui.treeView->currentIndex());
    QMenu menu(this);
    menu.addAction(newEntityGroupAction);
    if (current)
    {
        menu.addSeparator();
        menu.addAction(renameEntityAction);
        menu.addAction(toggleEntityAction);
        menu.addAction(lockEntityAction);
        menu.addAction(zoomEntityAction);
        menu.addSeparator();
        menu.addAction(saveSelectedEntityAction);
        menu.addAction(ui.actDelete);
    }
    renameEntityAction->setEnabled(current && !current->locked);
    lockEntityAction->setEnabled(current
        && current->kind != famp::workspace::EntityKind::ProjectRoot);
    zoomEntityAction->setEnabled(current
        && entityRenderers.hasRenderer(current->kind));
    saveSelectedEntityAction->setEnabled(std::any_of(
        ids.cbegin(), ids.cend(), [this](const famp::workspace::EntityId& id) {
            const famp::workspace::WorkspaceEntity* entity =
                workspaceStore->entity(id);
            return entity && entityWriters.hasWriter(entity->kind);
        }));
    menu.exec(ui.treeView->viewport()->mapToGlobal(position));
}

bool MainWindow::saveWorkspaceEntity(const famp::workspace::EntityId& id)
{
    const famp::workspace::WorkspaceEntity* entity = workspaceStore->entity(id);
    if (!entity)
        return false;
    const famp::workspace::EntityWriter* writer = entityWriters.writer(entity->kind);
    if (!writer)
        return false;

    const QString initialDirectory = currentProjectPath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(currentProjectPath).absolutePath();
    const QString extension = writer->extensions.constFirst();
    const QString initialPath = QDir(initialDirectory).filePath(
        entity->name + QLatin1Char('.') + extension);
    const QString filter = QStringLiteral("%1 (*.%2)")
        .arg(writer->description, writer->extensions.join(QStringLiteral(" *.")));
    QString outputPath = QFileDialog::getSaveFileName(
        this, tr("保存所选实体"), initialPath, filter);
    if (outputPath.isEmpty())
        return false;
    outputPath = famp::io::pathWithRequiredSuffix(outputPath, extension);
    QString error;
    if (!entityWriters.write(*entity, outputPath, &error))
    {
        QMessageBox::warning(this, tr("保存实体失败"), error);
        return false;
    }

    famp::workspace::WorkspaceEntity saved = *entity;
    saved.assetPath = outputPath;
    saved.dirty = false;
    if (saved.kind == famp::workspace::EntityKind::PointCloud)
    {
        const std::shared_ptr<MyCloudList> payload = saved.payloadAs<MyCloudList>();
        if (payload)
        {
            MyCloudList cloud = *payload;
            cloud.layer.sourcePath = outputPath;
            saved.setPayload(std::make_shared<MyCloudList>(cloud));
            for (MyCloudList& stored : pointCloudList)
            {
                if (stored.layer.id == cloud.layer.id)
                {
                    stored = cloud;
                    break;
                }
            }
        }
    }
    {
        const QScopedValueRollback<bool> guard(syncingWorkspaceEntity, true);
        workspaceStore->replaceEntity(saved);
    }
    updateEntityProperties();
    statusBar()->showMessage(tr("实体已保存到 %1").arg(outputPath), 5000);
    emit sendStr2Console(tr("已保存实体  %1").arg(outputPath));
    return true;
}

void MainWindow::slotSaveSelectedEntity()
{
    QVector<famp::workspace::EntityId> ids = selectedEntityIds();
    QVector<famp::workspace::EntityId> writable;
    for (const famp::workspace::EntityId& id : ids)
    {
        const famp::workspace::WorkspaceEntity* entity = workspaceStore->entity(id);
        if (!entity)
            continue;
        if (entityWriters.hasWriter(entity->kind))
            writable.append(id);
        for (const famp::workspace::EntityId& child : workspaceStore->descendants(id))
        {
            const famp::workspace::WorkspaceEntity* childEntity =
                workspaceStore->entity(child);
            if (childEntity && entityWriters.hasWriter(childEntity->kind)
                && !writable.contains(child))
            {
                writable.append(child);
            }
        }
    }
    if (writable.isEmpty())
    {
        QMessageBox::information(this, tr("保存所选实体"),
                                 tr("所选内容没有可用的实体保存器。"));
        return;
    }
    for (const famp::workspace::EntityId& id : writable)
    {
        if (!saveWorkspaceEntity(id))
            break;
    }
}

void MainWindow::slotCreateEntityGroup()
{
    const famp::workspace::WorkspaceEntity* current =
        model->entity(ui.treeView->currentIndex());
    famp::workspace::EntityId parentId = workspaceStore->rootId();
    if (current)
    {
        parentId = famp::workspace::entityKindCanHaveChildren(current->kind)
            ? current->id : current->parentId;
    }
    const famp::workspace::EntityId id = workspaceStore->addEntity(
        famp::workspace::makeEntity(famp::workspace::EntityKind::Group,
                                    tr("新建组")),
        parentId);
    if (id.isNull())
        return;
    const QModelIndex index = model->indexForId(id);
    ui.treeView->setCurrentIndex(index);
    ui.treeView->edit(index);
    markProjectDirty();
}

void MainWindow::slotRenameEntity()
{
    const QModelIndex index = ui.treeView->currentIndex();
    if (index.isValid() && model->flags(index).testFlag(Qt::ItemIsEditable))
        ui.treeView->edit(index);
}

void MainWindow::slotToggleSelectedEntities()
{
    const QVector<famp::workspace::EntityId> ids = selectedEntityIds();
    if (ids.isEmpty())
        return;
    const famp::workspace::WorkspaceEntity* first = workspaceStore->entity(ids.first());
    const bool visible = first ? !first->visible : true;
    for (const famp::workspace::EntityId& id : ids)
        workspaceStore->setVisible(id, visible, true);
}

void MainWindow::slotLockSelectedEntities()
{
    const QVector<famp::workspace::EntityId> ids = selectedEntityIds();
    if (ids.isEmpty())
        return;
    const famp::workspace::WorkspaceEntity* first = workspaceStore->entity(ids.first());
    const bool locked = first ? !first->locked : true;
    for (const famp::workspace::EntityId& id : ids)
    {
        if (id != workspaceStore->rootId())
            workspaceStore->setLocked(id, locked);
    }
}

void MainWindow::slotZoomSelectedEntity()
{
    const famp::workspace::WorkspaceEntity* entity =
        model->entity(ui.treeView->currentIndex());
    if (entity)
        entityRenderers.zoom(*entity);
}

std::optional<famp::projection::Plane>
MainWindow::projectionPlaneForEntity(
    const famp::workspace::WorkspaceEntity& entity) const
{
    if (entity.kind != famp::workspace::EntityKind::PointCloud
        || !entity.provenance.has_value())
    {
        return std::nullopt;
    }
    const QString operation = entity.provenance->operation.trimmed();
    if (operation != QStringLiteral("plane_projection")
        && operation != QStringLiteral("overlook_projection"))
    {
        return std::nullopt;
    }
    const QJsonObject& parameters = entity.provenance->parameters;
    const bool overlook = operation == QStringLiteral("overlook_projection")
        || parameters.value(QStringLiteral("overlook")).toBool(false);
    return famp::projection::planeFromMetadata(
        parameters.value(QStringLiteral("plane")).toString(), overlook);
}

void MainWindow::synchronizeProjectionWorkflowFromSelection()
{
    if (!workspaceStore || !model || !myVTK || !ui.graphicsView)
        return;

    MyCloudList cloud;
    if (!selectedCloudData(cloud))
    {
        const bool hadSource = projectionWorkflow.hasSource();
        closeProjectionDecisionDialog();
        projectionWorkflow.clearSource();
        activeVtkSourceId = {};
        myVTK->getDBItemCloud({});
        ui.graphicsView->clearProjectionInput();
        if (hadSource)
            myVTK->clearProjectionPreview();
        updateProjectionActions();
        updateArchaeologyWorkflowGuide();
        return;
    }

    const famp::workspace::WorkspaceEntity* entity =
        model->entity(ui.treeView->currentIndex());
    if (!entity)
        return;
    const bool sourceChanged = !projectionWorkflow.hasSource()
        || !projectionWorkflow.source()
        || projectionWorkflow.source()->entityId != entity->id;
    QString error;
    if (!projectionWorkflow.selectSource(
            entity->id, entity->name, cloud.layer.points, &error))
    {
        projectionWorkflow.clearSource();
        activeVtkSourceId = {};
        ui.graphicsView->clearProjectionInput();
        updateProjectionActions();
        updateArchaeologyWorkflowGuide();
        return;
    }

    activeVtkSourceId = entity->id;
    myVTK->getDBItemCloud(
        cloud.layer.points, cloud.layer.display.pointSize);
    if (sourceChanged)
    {
        closeProjectionDecisionDialog();
        myVTK->clearProjectionPreview();
        ui.graphicsView->clearProjectionInput();
    }

    const std::optional<famp::projection::Plane> storedPlane =
        projectionPlaneForEntity(*entity);
    if (storedPlane.has_value())
    {
        if (projectionWorkflow.setPreview(
                cloud.layer.points, *storedPlane, &error))
        {
            ui.graphicsView->setProjectionInput(
                cloud.layer.points, *storedPlane, nullptr);
        }
    }
    else if (sourceChanged)
    {
        projectionWorkflow.clearPreview();
    }

    updateProjectionActions();
    updateArchaeologyWorkflowGuide();
}

void MainWindow::updateProjectionActions()
{
    const famp::projection::Source* source = projectionWorkflow.source();
    const bool hasCloud = !cloudLoadBusy && source && source->points
        && !source->points->empty();
    ui.actProjXOY->setEnabled(hasCloud);
    ui.actProjXOZ->setEnabled(hasCloud);
    ui.actProjYOZ->setEnabled(hasCloud);
    ui.actOverLookProj->setEnabled(hasCloud);

    const famp::projection::Preview* preview = projectionWorkflow.preview();
    const bool planReady = ui.graphicsView
        && ui.graphicsView->hasProjectionDrawing(
            famp::projection::Plane::Overlook);
    const bool orderedPlane = preview
        && (!isArchaeologyProfilePlane(preview->plane) || planReady);
    const bool canDraw = !cloudLoadBusy && preview && preview->points
        && preview->points->size() >= 10 && orderedPlane;
    ui.actProjLine->setEnabled(canDraw);
}

void MainWindow::updateCloudToolActions()
{
    MyCloudList cloud;
    const bool available = selectedCloudData(cloud);
    if (cloudDisplaySettingsAction)
        cloudDisplaySettingsAction->setEnabled(available);
    if (preprocessCloudAction)
        preprocessCloudAction->setEnabled(available && !cloudLoadBusy);
    if (cropCloudAction)
        cropCloudAction->setEnabled(available && !cloudLoadBusy);
    if (registerCloudAction)
        registerCloudAction->setEnabled(pointCloudList.size() >= 2 && !cloudLoadBusy);
    if (cloudCoordinateAction)
        cloudCoordinateAction->setEnabled(available && !cloudLoadBusy);
    if (reprojectCloudAction)
        reprojectCloudAction->setEnabled(
            available && !cloud.layer.locked && !cloudLoadBusy);
    if (archaeologyMetadataAction)
        archaeologyMetadataAction->setEnabled(
            available && !cloud.layer.locked && !cloudLoadBusy);
    if (controlPointsAction)
        controlPointsAction->setEnabled(
            available && !cloud.layer.locked && !cloudLoadBusy);
    if (terrainAnalysisAction)
        terrainAnalysisAction->setEnabled(
            available && !cloud.layer.locked && !cloudLoadBusy);
    if (cutFillAction)
        cutFillAction->setEnabled(
            available && !cloud.layer.locked && !cloudLoadBusy);
    if (cloudProfileAction)
        cloudProfileAction->setEnabled(available && !cloudLoadBusy);
    updateProjectionActions();
}

void MainWindow::slotEditArchaeologyMetadata()
{
    MyCloudList cloud;
    QString path;
    if (!selectedCloudData(cloud, &path))
    {
        QMessageBox::information(
            this, tr("考古图层属性"), tr("请先在内容列表中选择点云。"));
        return;
    }
    if (cloud.layer.locked)
    {
        QMessageBox::information(
            this, tr("考古图层属性"), tr("所选图层已锁定，无法修改。"));
        return;
    }

    QMap<QString, QString> updatedFields;
    if (!famp::archaeology::editFields(
            this, cloud.layer.name, path,
            cloud.layer.archaeologyFields, updatedFields)
        || updatedFields == cloud.layer.archaeologyFields)
    {
        return;
    }

    const famp::cloud::CloudLayer before = cloud.layer;
    famp::cloud::CloudLayer after = before;
    after.archaeologyFields = updatedFields;
    ++after.revision;
    const QString layerId = before.id;
    ui.graphicsView->commandStack()->push(
        famp::graphics::makeCallbackCommand(
            [this, layerId, before]() {
                if (!applyCloudMetadataState(layerId, before))
                {
                    emit sendStr2Console(
                        tr("恢复考古图层属性失败：%1").arg(before.name));
                }
            },
            [this, layerId, after]() {
                if (!applyCloudMetadataState(layerId, after))
                {
                    emit sendStr2Console(
                        tr("更新考古图层属性失败：%1").arg(after.name));
                }
            },
            tr("更新考古图层属性：%1").arg(before.name)));
    statusBar()->showMessage(
        tr("已保存 %1 个考古字段，可撤销。")
            .arg(updatedFields.size()),
        5000);
    emit sendStr2Console(
        tr("已更新考古图层属性：%1（%2 个字段）")
            .arg(before.name)
            .arg(updatedFields.size()));
}

void MainWindow::slotEditControlPoints()
{
    MyCloudList cloud;
    QString path;
    if (!selectedCloudData(cloud, &path))
    {
        QMessageBox::information(
            this, tr("控制点与空间配准"), tr("请先在内容列表中选择点云。"));
        return;
    }
    if (cloud.layer.locked)
    {
        QMessageBox::information(
            this, tr("控制点与空间配准"), tr("所选图层已锁定，无法修改。"));
        return;
    }

    famp::control::EditResult result;
    if (!famp::control::editControlPoints(
            this, cloud.layer.name, path, cloud.layer.spatial,
            cloud.layer.controlPoints, result))
    {
        return;
    }

    const famp::cloud::CloudLayer before = cloud.layer;
    famp::cloud::CloudLayer after = before;
    after.controlPoints = result.points;
    if (result.applySolution)
        after.spatial = result.solution.spatial;
    if (famp::control::pointsEqual(
            before.controlPoints, after.controlPoints)
        && before.spatial.origin == after.spatial.origin
        && before.spatial.transform == after.spatial.transform)
    {
        return;
    }
    ++after.revision;

    const QString layerId = before.id;
    if (!result.applySolution)
    {
        ui.graphicsView->commandStack()->push(
            famp::graphics::makeCallbackCommand(
                [this, layerId, before]() {
                    if (!applyCloudMetadataState(layerId, before))
                        emit sendStr2Console(tr("恢复控制点记录失败。"));
                },
                [this, layerId, after]() {
                    if (!applyCloudMetadataState(layerId, after))
                        emit sendStr2Console(tr("保存控制点记录失败。"));
                },
                tr("更新控制点记录：%1").arg(before.name)));
        statusBar()->showMessage(
            tr("已保存 %1 个控制点，可撤销。")
                .arg(after.controlPoints.size()),
            5000);
        emit sendStr2Console(
            tr("已更新控制点记录：%1（%2 个点）")
                .arg(before.name)
                .arg(after.controlPoints.size()));
        return;
    }

    const QVector<famp::measurement::Record3D> beforeMeasurements =
        myVTK->measurements();
    QVector<famp::measurement::Record3D> afterMeasurements;
    QString measurementError;
    if (!transformLayerMeasurements(
            layerId, before.spatial, after.spatial,
            beforeMeasurements, afterMeasurements, &measurementError))
    {
        QMessageBox::warning(
            this, tr("控制点与空间配准"), measurementError);
        return;
    }
    auto applyState = [this, layerId](
        const famp::cloud::CloudLayer& state,
        const QVector<famp::measurement::Record3D>& measurements) {
        if (!applyCloudLayerState(layerId, state))
            return false;
        QString error;
        if (!myVTK->setMeasurements(measurements, &error))
        {
            emit sendStr2Console(
                tr("更新控制点配准后的测量失败：%1").arg(error));
            return false;
        }
        return true;
    };
    ui.graphicsView->commandStack()->push(
        famp::graphics::makeCallbackCommand(
            [applyState, before, beforeMeasurements]() {
                applyState(before, beforeMeasurements);
            },
            [applyState, after, afterMeasurements]() {
                applyState(after, afterMeasurements);
            },
            tr("应用控制点空间配准：%1").arg(before.name)));
    statusBar()->showMessage(
        tr("控制点刚体配准已应用：RMSE %1（图层坐标单位），可撤销。")
            .arg(result.solution.quality.rootMeanSquare, 0, 'g', 8),
        8000);
    emit sendStr2Console(
        tr("控制点配准完成：%1，启用 %2 点，RMSE %3，最大残差 %4（图层坐标单位）")
            .arg(before.name)
            .arg(result.solution.quality.enabledPointCount)
            .arg(result.solution.quality.rootMeanSquare, 0, 'g', 8)
            .arg(result.solution.quality.maximum, 0, 'g', 8));
}

void MainWindow::slotStartCloudProfile()
{
    MyCloudList cloud;
    if (!selectedCloudData(cloud))
    {
        QMessageBox::information(
            this, tr("点云高程剖面"), tr("请先在内容列表中选择点云。"));
        return;
    }
    if (!cloud.layer.visible)
    {
        QMessageBox::information(
            this, tr("点云高程剖面"),
            tr("所选点云当前已隐藏，请先显示图层后再拾取剖面线。"));
        return;
    }

    QString sourceCrs = cloud.layer.crs.trimmed();
    if (sourceCrs.isEmpty())
        sourceCrs = projectCrs.trimmed();
    double horizontalUnitToMetre = 1.0;
    QString horizontalUnitName = tr("米（用户确认的本地平面坐标）");
    QString crsDescription = tr("未声明 CRS；将按本地米制平面坐标处理");
    if (!sourceCrs.isEmpty())
    {
        famp::crs::Info info;
        QString crsError;
        if (!famp::crs::inspect(sourceCrs, info, &crsError))
        {
            QMessageBox::warning(this, tr("点云高程剖面"), crsError);
            return;
        }
        if (info.geographic)
        {
            QMessageBox::warning(
                this, tr("点云高程剖面"),
                tr("所选点云使用地理经纬度坐标系 %1。剖面走廊和沿线距离不能直接以角度计算；"
                   "请先使用“工具 → 重投影所选点云…”转换到合适的投影坐标系。")
                    .arg(info.identifier));
            return;
        }
        if (!info.projected)
        {
            QMessageBox::warning(
                this, tr("点云高程剖面"),
                tr("坐标系 %1 不是受支持的二维投影坐标系。请先重投影点云。")
                    .arg(info.identifier));
            return;
        }
        sourceCrs = info.identifier;
        horizontalUnitToMetre = info.horizontalUnitToMetre;
        horizontalUnitName = QStringLiteral("%1（1 单位 = %2 米）")
                                 .arg(info.horizontalUnitName)
                                 .arg(info.horizontalUnitToMetre, 0, 'g', 12);
        crsDescription = QStringLiteral("%1 — %2")
                             .arg(info.identifier, info.name);
    }
    else
    {
        const auto confirmation = QMessageBox::question(
            this, tr("确认本地坐标单位"),
            tr("所选点云和项目都未声明 CRS。是否确认其真实 X/Y/Z 坐标是以米为单位的平面坐标？\n\n"
               "如果坐标实际是经纬度或其他单位，请先取消并设置/重投影坐标系。"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (confirmation != QMessageBox::Yes)
            return;
    }

    ui.graphicsView->deactivateMeasurement();
    myVTK->deactivateMeasurement();
    if (QAction* checked = measurementActionGroup->checkedAction())
        checked->setChecked(false);
    pendingProfileLayerId = cloud.layer.id;
    pendingProfileSourceCrs = sourceCrs;
    pendingProfileCrsDescription = crsDescription;
    pendingProfileHorizontalUnitName = horizontalUnitName;
    pendingProfileHorizontalUnitToMetre = horizontalUnitToMetre;
    pendingProfilePointCloud = cloud.layer.points.get();
    pendingProfileSpatial = cloud.layer.spatial;
    if (!myVTK->startProfileLineSelection(cloud.layer.id))
    {
        pendingProfileLayerId.clear();
        pendingProfilePointCloud = nullptr;
    }
}

void MainWindow::generateCloudProfile(
    const QString& layerId,
    const QVector3D& localStartVector,
    const QVector3D& localEndVector,
    const QString& sourceCrs,
    const QString& crsDescription,
    const QString& horizontalUnitName,
    double horizontalUnitToMetre)
{
    const auto found = std::find_if(
        pointCloudList.cbegin(), pointCloudList.cend(),
        [&layerId](const MyCloudList& cloud) {
            return cloud.layer.id.compare(
                layerId, Qt::CaseInsensitive) == 0;
        });
    if (found == pointCloudList.cend() || !found->layer.points
        || found->layer.points->size() < 2)
    {
        QMessageBox::warning(
            this, tr("点云高程剖面"),
            tr("剖面线关联的点云图层已移除或数据不足。"));
        return;
    }
    const MyCloudList cloud = *found;
    const famp::cloud::Point3d localStart{
        static_cast<double>(localStartVector.x()),
        static_cast<double>(localStartVector.y()),
        static_cast<double>(localStartVector.z())};
    const famp::cloud::Point3d localEnd{
        static_cast<double>(localEndVector.x()),
        static_cast<double>(localEndVector.y()),
        static_cast<double>(localEndVector.z())};
    famp::profile::Baseline baseline;
    QString coordinateError;
    if (!famp::cloud::localToReal(
            cloud.layer.spatial, localStart, baseline.start, &coordinateError)
        || !famp::cloud::localToReal(
            cloud.layer.spatial, localEnd, baseline.end, &coordinateError))
    {
        QMessageBox::warning(this, tr("点云高程剖面"), coordinateError);
        return;
    }
    const double baselineLength = std::hypot(
        baseline.end[0] - baseline.start[0],
        baseline.end[1] - baseline.start[1]);
    if (!std::isfinite(baselineLength) || baselineLength <= 1.0e-12)
    {
        QMessageBox::warning(
            this, tr("点云高程剖面"),
            tr("剖面线水平长度为零，请重新拾取两个端点。"));
        return;
    }

    const QFileInfo sourceInfo(cloud.layer.sourcePath);
    const QDir initialDirectory = sourceInfo.absoluteDir().exists()
        ? sourceInfo.absoluteDir()
        : QDir(QFileInfo(currentProjectPath).absolutePath());
    QString baseName = sourceInfo.completeBaseName();
    if (baseName.isEmpty())
        baseName = cloud.layer.name;
    if (baseName.trimmed().isEmpty())
        baseName = QStringLiteral("profile");
    const QString initialSidecarPath = initialDirectory.filePath(
        baseName + QStringLiteral("_profile.famp-profile"));
    famp::profileui::ProfileDialog dialog(
        cloud.layer.name, crsDescription, horizontalUnitName,
        horizontalUnitToMetre, baseline, initialSidecarPath, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    famp::profileui::Options options = dialog.options();
    if (!options.sidecarPath.isEmpty())
        options.sidecarPath = QFileInfo(options.sidecarPath).absoluteFilePath();
    QString validationError;
    if (!famp::profileui::validateOptions(options, &validationError))
    {
        QMessageBox::warning(
            this, tr("点云高程剖面"), validationError);
        return;
    }
    const famp::profileui::ExportPaths exportPaths =
        famp::profileui::derivedExportPaths(options.sidecarPath);

    QStringList existingOutputs;
    const auto addExisting = [&existingOutputs](bool selected,
                                                const QString& path) {
        if (selected && QFileInfo::exists(path))
            existingOutputs.append(path);
    };
    addExisting(true, exportPaths.sidecar);
    addExisting(options.exportBinsCsv, exportPaths.binsCsv);
    addExisting(options.exportSamplesCsv, exportPaths.samplesCsv);
    addExisting(options.exportSvg, exportPaths.svg);
    if (!existingOutputs.isEmpty())
    {
        const auto overwrite = QMessageBox::question(
            this, tr("覆盖点云剖面成果"),
            tr("以下成果文件已经存在，将被原子替换：\n%1\n\n是否继续？")
                .arg(existingOutputs.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (overwrite != QMessageBox::Yes)
            return;
    }

    const famp::tasks::Handle task = taskManager->start(
        tr("生成点云高程剖面"), cloud.layer.name);
    if (!task.isValid())
    {
        QMessageBox::warning(
            this, tr("点云高程剖面"), tr("无法创建后台剖面分析任务。"));
        return;
    }
    QProgressDialog progress(
        tr("正在提取点云高程剖面…"), tr("取消"), 0, 100, this);
    progress.setWindowTitle(tr("点云高程剖面"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    connect(&progress, &QProgressDialog::canceled, this, [this, task]() {
        taskManager->requestCancellation(task.id);
    });
    const QMetaObject::Connection progressConnection = connect(
        taskManager, &famp::tasks::TaskManager::taskChanged,
        &progress, [this, task, &progress](quint64 id) {
            if (id != task.id)
                return;
            famp::tasks::Snapshot snapshot;
            if (!taskManager->snapshot(id, snapshot))
                return;
            progress.setValue(static_cast<int>(std::clamp(
                snapshot.progress * 100.0, 0.0, 100.0)));
            if (!snapshot.message.isEmpty())
                progress.setLabelText(snapshot.message);
        });

    QFutureWatcher<ProfileTaskOutput> watcher;
    connect(&watcher, &QFutureWatcher<ProfileTaskOutput>::finished,
            &progress, &QProgressDialog::accept);
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr input =
        cloud.layer.points;
    const famp::cloud::SpatialReference spatial = cloud.layer.spatial;
    const QString sourcePath = cloud.layer.sourcePath;
    const QString layerName = cloud.layer.name;
    watcher.setFuture(QtConcurrent::run(
        [this, task, input, spatial, localStart, localEnd, options,
         exportPaths, sourcePath, sourceCrs, horizontalUnitName,
         layerId, layerName]() {
            ProfileTaskOutput output;
            output.saveRequested = !options.sidecarPath.isEmpty();
            const auto shouldCancel = task.cancellationCheck();
            output.analysis = famp::profile::extract(
                input, spatial, localStart, localEnd, options.analysis,
                shouldCancel,
                [this, task](double value) {
                    taskManager->setProgress(
                        task.id, value * 0.82,
                        tr("正在提取剖面：%1%")
                            .arg(static_cast<int>(value * 100.0)));
                });
            if (output.analysis.cancelled
                || famp::tasks::isCancellationRequested(shouldCancel))
            {
                output.cancelled = true;
                output.error = tr("点云剖面分析已取消，未写入未完成文件。");
                return output;
            }
            if (!output.analysis.succeeded())
            {
                output.error = output.analysis.error.isEmpty()
                    ? tr("点云剖面分析失败。") : output.analysis.error;
                return output;
            }
            output.analysis.sourceLayerId = layerId;
            output.analysis.sourceLayerName = layerName;
            output.analysis.sourcePath = sourcePath;
            output.analysis.sourceCrs = sourceCrs;
            output.analysis.horizontalUnitName = horizontalUnitName;
            if (!output.saveRequested)
            {
                taskManager->setProgress(
                    task.id, 1.0, tr("点云剖面内存成果已生成"));
                return output;
            }
            taskManager->setProgress(
                task.id, 0.84, tr("正在原子保存点云剖面项目边车…"));
            QString saveError;
            if (!famp::profileio::saveResultAtomically(
                    exportPaths.sidecar, output.analysis,
                    &saveError, shouldCancel))
            {
                output.cancelled =
                    famp::tasks::isCancellationRequested(shouldCancel);
                output.error = saveError;
                return output;
            }
            output.sidecarSaved = true;
            output.savedPaths.append(exportPaths.sidecar);

            const int exportCount = static_cast<int>(options.exportBinsCsv)
                + static_cast<int>(options.exportSamplesCsv)
                + static_cast<int>(options.exportSvg);
            int exportIndex = 0;
            const auto exportOne = [&](const QString& label,
                                       const QString& path,
                                       const auto& operation) {
                taskManager->setProgress(
                    task.id,
                    0.86 + 0.13 * exportIndex / std::max(1, exportCount),
                    tr("正在导出%1…").arg(label));
                ++exportIndex;
                QString exportError;
                if (operation(&exportError))
                {
                    output.savedPaths.append(path);
                    return true;
                }
                if (famp::tasks::isCancellationRequested(shouldCancel))
                {
                    output.cancelled = true;
                    output.error = exportError;
                    return false;
                }
                output.warnings.append(
                    tr("%1 导出失败：%2").arg(label, exportError));
                return true;
            };
            if (options.exportBinsCsv
                && !exportOne(
                    tr("采样段 CSV"), exportPaths.binsCsv,
                    [&](QString* error) {
                        return famp::profileio::exportBinsCsvAtomically(
                            exportPaths.binsCsv, output.analysis,
                            error, shouldCancel);
                    }))
            {
                return output;
            }
            if (options.exportSamplesCsv
                && !exportOne(
                    tr("原始点 CSV"), exportPaths.samplesCsv,
                    [&](QString* error) {
                        return famp::profileio::exportSamplesCsvAtomically(
                            exportPaths.samplesCsv, output.analysis,
                            error, shouldCancel);
                    }))
            {
                return output;
            }
            if (options.exportSvg
                && !exportOne(
                    tr("剖面 SVG"), exportPaths.svg,
                    [&](QString* error) {
                        return famp::profileio::exportSvgAtomically(
                            exportPaths.svg, output.analysis,
                            error, shouldCancel);
                    }))
            {
                return output;
            }
            taskManager->setProgress(task.id, 1.0, tr("点云剖面成果已生成"));
            return output;
        }));
    if (!watcher.isFinished())
        progress.exec();
    const ProfileTaskOutput result = watcher.result();
    disconnect(progressConnection);

    if (result.cancelled)
    {
        QString message = result.error.isEmpty()
            ? tr("点云剖面分析已取消。") : result.error;
        if (!result.savedPaths.isEmpty())
        {
            message += tr(" 已完成的成果文件已保留：%1")
                           .arg(result.savedPaths.join(QStringLiteral("；")));
        }
        taskManager->acknowledgeCancellation(task.id, message);
        statusBar()->showMessage(message, 8000);
        emit sendStr2Console(message);
        return;
    }
    if (!result.succeeded())
    {
        const QString message = result.error.isEmpty()
            ? tr("点云高程剖面生成失败。") : result.error;
        taskManager->fail(task.id, message);
        QMessageBox::warning(
            this, tr("点云高程剖面生成失败"), message);
        emit sendStr2Console(message);
        return;
    }
    const famp::workspace::EntityId sourceId(layerId);
    const famp::workspace::WorkspaceEntity* sourceEntity =
        workspaceStore->entity(sourceId);
    if (!sourceEntity)
    {
        taskManager->fail(task.id, tr("来源点云实体已不存在"));
        return;
    }
    famp::workspace::Provenance provenance;
    provenance.operation = QStringLiteral("elevation_profile");
    provenance.sourceIds = {sourceId};
    provenance.sourceSnapshot = sourceSnapshot(*sourceEntity, cloud);
    provenance.parameters = QJsonObject{
        {QStringLiteral("corridorWidth"), result.analysis.corridorWidth},
        {QStringLiteral("binSize"), result.analysis.binSize},
        {QStringLiteral("minimumPointsPerBin"),
         result.analysis.minimumPointsPerBin},
        {QStringLiteral("statistic"),
         static_cast<int>(result.analysis.statistic)},
        {QStringLiteral("startX"), result.analysis.baseline.start[0]},
        {QStringLiteral("startY"), result.analysis.baseline.start[1]},
        {QStringLiteral("startZ"), result.analysis.baseline.start[2]},
        {QStringLiteral("endX"), result.analysis.baseline.end[0]},
        {QStringLiteral("endY"), result.analysis.baseline.end[1]},
        {QStringLiteral("endZ"), result.analysis.baseline.end[2]}};
    provenance.metrics = QJsonObject{
        {QStringLiteral("lengthMetres"),
         result.analysis.length * result.analysis.horizontalUnitToMetre},
        {QStringLiteral("selectedPointCount"),
         static_cast<double>(result.analysis.selectedPointCount)},
        {QStringLiteral("populatedBinCount"),
         result.analysis.populatedBinCount}};
    famp::workspace::WorkspaceEntity profileEntity =
        famp::workspace::makeEntity(
            famp::workspace::EntityKind::Profile,
            derivedEntityName(sourceEntity->name, QStringLiteral("_profile")));
    profileEntity.provenance = std::move(provenance);
    profileEntity.setPayload(
        std::make_shared<famp::profile::Result>(result.analysis));
    if (result.sidecarSaved)
        profileEntity.assetPath = exportPaths.sidecar;
    if (!integrateDerivedEntity(sourceId, std::move(profileEntity)))
    {
        taskManager->fail(task.id, tr("无法将剖面成果加入内容列表"));
        return;
    }
    taskManager->succeed(task.id, tr("点云高程剖面生成完成"));
    const QString resultLocation = result.savedPaths.isEmpty()
        ? tr("内存内容树（未自动落盘）")
        : result.savedPaths.join(QStringLiteral("；"));
    const QString summary = tr(
        "点云剖面完成：长度 %1 米，走廊宽度 %2 米，从 %3 个源点中提取 %4 点，"
        "%5/%6 个采样段有效，代表高程为%7。成果：%8")
        .arg(result.analysis.length
                 * result.analysis.horizontalUnitToMetre, 0, 'g', 10)
        .arg(result.analysis.corridorWidth
                 * result.analysis.horizontalUnitToMetre, 0, 'g', 10)
        .arg(result.analysis.sourcePointCount)
        .arg(result.analysis.selectedPointCount)
        .arg(result.analysis.populatedBinCount)
        .arg(result.analysis.bins.size())
        .arg(famp::profile::statisticName(result.analysis.statistic))
        .arg(resultLocation);
    statusBar()->showMessage(tr("点云高程剖面生成完成。"), 8000);
    emit sendStr2Console(summary);
    if (!result.warnings.isEmpty())
    {
        const QString warningText = result.warnings.join(QLatin1Char('\n'));
        QMessageBox::warning(
            this, tr("剖面成果已生成，但有提示"), warningText);
        emit sendStr2Console(warningText);
    }
    famp::profileui::ProfileResultDialog resultDialog(
        result.analysis, result.savedPaths, this);
    resultDialog.exec();
}

void MainWindow::slotCalculateCutFill()
{
    MyCloudList cloud;
    QString sourcePath;
    if (!selectedCloudData(cloud, &sourcePath))
    {
        QMessageBox::information(
            this, tr("挖填方与体积"), tr("请先在内容列表中选择点云。"));
        return;
    }
    if (cloud.layer.locked)
    {
        QMessageBox::information(
            this, tr("挖填方与体积"), tr("所选图层已锁定，无法生成挖填方成果。"));
        return;
    }

    QString sourceCrs = cloud.layer.crs.trimmed();
    if (sourceCrs.isEmpty())
        sourceCrs = projectCrs.trimmed();
    double horizontalUnitToMetre = 1.0;
    QString horizontalUnitName = tr("米（用户确认的本地 X/Y/Z 坐标）");
    QString crsDescription = tr("未声明 CRS；将按本地米制三维坐标处理");
    if (!sourceCrs.isEmpty())
    {
        famp::crs::Info info;
        QString crsError;
        if (!famp::crs::inspect(sourceCrs, info, &crsError))
        {
            QMessageBox::warning(this, tr("挖填方与体积"), crsError);
            return;
        }
        if (info.geographic)
        {
            QMessageBox::warning(
                this, tr("挖填方与体积"),
                tr("所选点云使用地理经纬度坐标系 %1。不能直接以角度计算面积和体积；"
                   "请先使用“工具 → 重投影所选点云…”转换到合适的投影坐标系。")
                    .arg(info.identifier));
            return;
        }
        if (!info.projected)
        {
            QMessageBox::warning(
                this, tr("挖填方与体积"),
                tr("坐标系 %1 不是受支持的二维投影坐标系。请先重投影点云。")
                    .arg(info.identifier));
            return;
        }
        sourceCrs = info.identifier;
        horizontalUnitToMetre = info.horizontalUnitToMetre;
        horizontalUnitName = QStringLiteral("%1（1 单位 = %2 米；Z 按同一单位）")
                                 .arg(info.horizontalUnitName)
                                 .arg(info.horizontalUnitToMetre, 0, 'g', 12);
        crsDescription = QStringLiteral("%1 — %2")
                             .arg(info.identifier, info.name);
    }
    else
    {
        const auto confirmation = QMessageBox::question(
            this, tr("确认本地坐标单位"),
            tr("所选点云和项目都未声明 CRS。是否确认其真实 X/Y/Z 坐标都以米为单位？\n\n"
               "挖填方体积需要 X、Y 和高程 Z 使用同一长度单位。如果实际是经纬度或其他单位，请先取消并设置/重投影坐标系。"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (confirmation != QMessageBox::Yes)
            return;
    }

    const QFileInfo sourceInfo(sourcePath);
    const QDir initialDirectory = sourceInfo.absoluteDir().exists()
        ? sourceInfo.absoluteDir()
        : QDir(QFileInfo(currentProjectPath).absolutePath());
    QString baseName = sourceInfo.completeBaseName();
    if (baseName.isEmpty())
        baseName = cloud.layer.name;
    if (baseName.trimmed().isEmpty())
        baseName = QStringLiteral("cut_fill");
    const QString initialSidecarPath = initialDirectory.filePath(
        baseName + QStringLiteral("_volume.famp-volume"));
    famp::cutfillui::CutFillDialog dialog(
        cloud.layer.name, crsDescription, horizontalUnitName,
        horizontalUnitToMetre, initialSidecarPath, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    famp::cutfillui::Options options = dialog.options();
    if (!options.sidecarPath.isEmpty())
        options.sidecarPath = QFileInfo(options.sidecarPath).absoluteFilePath();
    if (!options.referenceDemPath.isEmpty())
    {
        options.referenceDemPath = QFileInfo(
            options.referenceDemPath).absoluteFilePath();
    }
    QString validationError;
    if (!famp::cutfillui::validateOptions(options, &validationError))
    {
        QMessageBox::warning(
            this, tr("挖填方与体积"), validationError);
        return;
    }
    const famp::cutfillui::ExportPaths exportPaths =
        famp::cutfillui::derivedExportPaths(options.sidecarPath);

    QStringList existingOutputs;
    const auto addExisting = [&existingOutputs](bool selected,
                                                const QString& path) {
        if (selected && QFileInfo::exists(path))
            existingOutputs.append(path);
    };
    addExisting(true, exportPaths.sidecar);
    addExisting(options.exportSummaryCsv, exportPaths.summaryCsv);
    addExisting(options.exportCellsCsv, exportPaths.cellsCsv);
    addExisting(options.exportSvg, exportPaths.svg);
    if (!existingOutputs.isEmpty())
    {
        const auto overwrite = QMessageBox::question(
            this, tr("覆盖挖填方成果"),
            tr("以下成果文件已经存在，将被原子替换：\n%1\n\n是否继续？")
                .arg(existingOutputs.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (overwrite != QMessageBox::Yes)
            return;
    }

    const famp::tasks::Handle task = taskManager->start(
        tr("计算挖填方与体积"), cloud.layer.name);
    if (!task.isValid())
    {
        QMessageBox::warning(
            this, tr("挖填方与体积"), tr("无法创建后台挖填方任务。"));
        return;
    }

    QProgressDialog progress(
        tr("正在计算挖填方与体积…"), tr("取消"), 0, 100, this);
    progress.setWindowTitle(tr("挖填方与体积"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    connect(&progress, &QProgressDialog::canceled, this, [this, task]() {
        taskManager->requestCancellation(task.id);
    });
    const QMetaObject::Connection progressConnection = connect(
        taskManager, &famp::tasks::TaskManager::taskChanged,
        &progress, [this, task, &progress](quint64 id) {
            if (id != task.id)
                return;
            famp::tasks::Snapshot snapshot;
            if (!taskManager->snapshot(id, snapshot))
                return;
            progress.setValue(static_cast<int>(std::clamp(
                snapshot.progress * 100.0, 0.0, 100.0)));
            if (!snapshot.message.isEmpty())
                progress.setLabelText(snapshot.message);
        });

    QFutureWatcher<CutFillTaskOutput> watcher;
    connect(&watcher, &QFutureWatcher<CutFillTaskOutput>::finished,
            &progress, &QProgressDialog::accept);
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr input =
        cloud.layer.points;
    const famp::cloud::SpatialReference spatial = cloud.layer.spatial;
    const QString layerId = cloud.layer.id;
    const QString layerName = cloud.layer.name;
    watcher.setFuture(QtConcurrent::run(
        [this, task, input, spatial, options, exportPaths,
         sourceCrs, horizontalUnitName, horizontalUnitToMetre,
         layerId, layerName]() mutable {
            CutFillTaskOutput output;
            output.saveRequested = !options.sidecarPath.isEmpty();
            const auto shouldCancel = task.cancellationCheck();
            famp::terrain::Grid referenceGrid;
            if (options.analysis.referenceMode
                == famp::cutfill::ReferenceMode::DemGrid)
            {
                taskManager->setProgress(
                    task.id, 0.02, tr("正在后台读取参考 DEM…"));
                QString referenceError;
                if (!famp::terrainio::loadGrid(
                        options.referenceDemPath, referenceGrid,
                        &referenceError))
                {
                    output.error = tr("参考 DEM 读取失败：%1")
                                       .arg(referenceError);
                    return output;
                }
                if (famp::tasks::isCancellationRequested(shouldCancel))
                {
                    output.cancelled = true;
                    output.error = tr("挖填方计算已取消。");
                    return output;
                }
                if (!famp::cutfillui::applyReferenceGrid(
                        referenceGrid, sourceCrs,
                        horizontalUnitToMetre, options,
                        &referenceError))
                {
                    output.error = referenceError;
                    return output;
                }
            }

            famp::terrain::Grid currentGrid;
            QString gridError;
            const double gridStart = options.analysis.referenceMode
                    == famp::cutfill::ReferenceMode::DemGrid
                ? 0.08 : 0.02;
            if (!famp::terrain::buildGridFromCloud(
                    input, spatial, options.grid, currentGrid,
                    nullptr, &gridError, shouldCancel,
                    [this, task, gridStart](double value) {
                        taskManager->setProgress(
                            task.id,
                            gridStart + (0.55 - gridStart) * value,
                            tr("正在生成当前地表 DEM：%1%")
                                .arg(static_cast<int>(value * 100.0)));
                    }))
            {
                output.cancelled =
                    famp::tasks::isCancellationRequested(shouldCancel);
                output.error = gridError.isEmpty()
                    ? tr("当前地表 DEM 生成失败。") : gridError;
                return output;
            }
            currentGrid.sourceLayerId = layerId;
            currentGrid.sourceLayerName = layerName;
            currentGrid.sourceCrs = sourceCrs;
            currentGrid.horizontalUnitName = horizontalUnitName;

            taskManager->setProgress(
                task.id, 0.56, tr("正在累计挖方、填方和净体积…"));
            const auto analysisProgress = [this, task](double value) {
                taskManager->setProgress(
                    task.id, 0.56 + 0.24 * value,
                    tr("正在计算网格高差：%1%")
                        .arg(static_cast<int>(value * 100.0)));
            };
            if (options.analysis.referenceMode
                == famp::cutfill::ReferenceMode::DemGrid)
            {
                output.analysis = famp::cutfill::compareToGrid(
                    std::move(currentGrid), referenceGrid,
                    options.analysis, shouldCancel, analysisProgress);
                output.analysis.referencePath = options.referenceDemPath;
            }
            else
            {
                output.analysis = famp::cutfill::compareToConstant(
                    std::move(currentGrid), options.analysis,
                    shouldCancel, analysisProgress);
                output.analysis.referencePath = tr("固定设计高程 %1 米")
                    .arg(options.analysis.referenceElevation
                             * horizontalUnitToMetre,
                         0, 'g', 12);
            }
            if (output.analysis.cancelled
                || famp::tasks::isCancellationRequested(shouldCancel))
            {
                output.cancelled = true;
                output.error = output.analysis.error.isEmpty()
                    ? tr("挖填方计算已取消，未写入未完成文件。")
                    : output.analysis.error;
                return output;
            }
            if (!output.analysis.error.isEmpty()
                || output.analysis.differences.isEmpty())
            {
                output.error = output.analysis.error.isEmpty()
                    ? tr("挖填方计算失败。")
                    : output.analysis.error;
                return output;
            }

            if (!output.saveRequested)
            {
                taskManager->setProgress(
                    task.id, 1.0, tr("挖填方内存成果已生成"));
                return output;
            }

            taskManager->setProgress(
                task.id, 0.82, tr("正在原子保存挖填方成果边车…"));
            QString saveError;
            if (!famp::cutfillio::saveResultAtomically(
                    exportPaths.sidecar, output.analysis,
                    &saveError, shouldCancel))
            {
                output.cancelled =
                    famp::tasks::isCancellationRequested(shouldCancel);
                output.error = saveError;
                return output;
            }
            output.sidecarSaved = true;
            output.savedPaths.append(exportPaths.sidecar);

            const int exportCount =
                static_cast<int>(options.exportSummaryCsv)
                + static_cast<int>(options.exportCellsCsv)
                + static_cast<int>(options.exportSvg);
            int exportIndex = 0;
            const auto exportOne = [&](const QString& label,
                                       const QString& path,
                                       const auto& operation) {
                taskManager->setProgress(
                    task.id,
                    0.85 + 0.14 * exportIndex
                        / std::max(1, exportCount),
                    tr("正在导出%1…").arg(label));
                ++exportIndex;
                QString exportError;
                if (operation(&exportError))
                {
                    output.savedPaths.append(path);
                    return true;
                }
                if (famp::tasks::isCancellationRequested(shouldCancel))
                {
                    output.cancelled = true;
                    output.error = exportError;
                    return false;
                }
                output.warnings.append(
                    tr("%1 导出失败：%2").arg(label, exportError));
                return true;
            };
            if (options.exportSummaryCsv
                && !exportOne(
                    tr("汇总 CSV"), exportPaths.summaryCsv,
                    [&](QString* error) {
                        return famp::cutfillio::exportSummaryCsvAtomically(
                            exportPaths.summaryCsv, output.analysis,
                            error, shouldCancel);
                    }))
            {
                return output;
            }
            if (options.exportCellsCsv
                && !exportOne(
                    tr("逐格 CSV"), exportPaths.cellsCsv,
                    [&](QString* error) {
                        return famp::cutfillio::exportCellsCsvAtomically(
                            exportPaths.cellsCsv, output.analysis,
                            error, shouldCancel);
                    }))
            {
                return output;
            }
            if (options.exportSvg
                && !exportOne(
                    tr("概览 SVG"), exportPaths.svg,
                    [&](QString* error) {
                        return famp::cutfillio::exportSvgAtomically(
                            exportPaths.svg, output.analysis,
                            error, shouldCancel);
                    }))
            {
                return output;
            }
            taskManager->setProgress(
                task.id, 1.0, tr("挖填方与体积成果已生成"));
            return output;
        }));
    if (!watcher.isFinished())
        progress.exec();
    const CutFillTaskOutput result = watcher.result();
    disconnect(progressConnection);

    if (result.cancelled)
    {
        QString message = result.error.isEmpty()
            ? tr("挖填方计算已取消。") : result.error;
        if (!result.savedPaths.isEmpty())
        {
            message += tr(" 已完成的成果文件已保留：%1")
                           .arg(result.savedPaths.join(QStringLiteral("；")));
        }
        taskManager->acknowledgeCancellation(task.id, message);
        statusBar()->showMessage(message, 8000);
        emit sendStr2Console(message);
        return;
    }
    if (!result.succeeded())
    {
        const QString message = result.error.isEmpty()
            ? tr("挖填方与体积成果生成失败。") : result.error;
        taskManager->fail(task.id, message);
        QMessageBox::warning(
            this, tr("挖填方与体积生成失败"), message);
        emit sendStr2Console(message);
        return;
    }
    const famp::workspace::EntityId sourceId(cloud.layer.id);
    const famp::workspace::WorkspaceEntity* sourceEntity =
        workspaceStore->entity(sourceId);
    if (!sourceEntity)
    {
        taskManager->fail(task.id, tr("来源点云实体已不存在"));
        return;
    }
    famp::workspace::Provenance provenance;
    provenance.operation = QStringLiteral("cut_fill");
    provenance.sourceIds = {sourceId};
    provenance.sourceSnapshot = sourceSnapshot(*sourceEntity, cloud);
    provenance.parameters = QJsonObject{
        {QStringLiteral("referenceMode"),
         static_cast<int>(result.analysis.referenceMode)},
        {QStringLiteral("referenceElevation"),
         result.analysis.constantReferenceElevation},
        {QStringLiteral("zeroTolerance"), result.analysis.zeroTolerance},
        {QStringLiteral("gridResolution"),
         result.analysis.currentGrid.resolution}};
    provenance.metrics = QJsonObject{
        {QStringLiteral("cutVolumeCubicMetres"),
         result.analysis.cutVolumeCubicMetres},
        {QStringLiteral("fillVolumeCubicMetres"),
         result.analysis.fillVolumeCubicMetres},
        {QStringLiteral("signedVolumeCubicMetres"),
         result.analysis.signedVolumeCubicMetres},
        {QStringLiteral("comparedCellCount"),
         static_cast<double>(result.analysis.comparedCellCount)}};
    famp::workspace::WorkspaceEntity cutFillEntity =
        famp::workspace::makeEntity(
            famp::workspace::EntityKind::CutFill,
            derivedEntityName(sourceEntity->name, QStringLiteral("_cut_fill")));
    cutFillEntity.provenance = std::move(provenance);
    cutFillEntity.setPayload(
        std::make_shared<famp::cutfill::Result>(result.analysis));
    if (result.sidecarSaved)
        cutFillEntity.assetPath = exportPaths.sidecar;
    if (!integrateDerivedEntity(sourceId, std::move(cutFillEntity)))
    {
        taskManager->fail(task.id, tr("无法将挖填方成果加入内容列表"));
        return;
    }
    taskManager->succeed(task.id, tr("挖填方与体积计算完成"));
    const QString resultLocation = result.savedPaths.isEmpty()
        ? tr("内存内容树（未自动落盘）")
        : result.savedPaths.join(QStringLiteral("；"));
    const QString referenceDescription =
        result.analysis.referenceMode
            == famp::cutfill::ReferenceMode::ConstantElevation
        ? tr("固定设计高程 %1 米")
              .arg(result.analysis.constantReferenceElevation
                       * result.analysis.currentGrid.horizontalUnitToMetre,
                   0, 'g', 12)
        : result.analysis.referencePath;
    const QString summary = tr(
        "挖填方计算完成：参考=%1，%2 × %3 网格，有效对比 %4 格；"
        "挖方 %5 立方米/%6 平方米，填方 %7 立方米/%8 平方米，"
        "净体积（挖-填）%9 立方米，缺少参考 %10 格。成果：%11")
        .arg(referenceDescription)
        .arg(result.analysis.currentGrid.columns)
        .arg(result.analysis.currentGrid.rows)
        .arg(result.analysis.comparedCellCount)
        .arg(result.analysis.cutVolumeCubicMetres, 0, 'g', 12)
        .arg(result.analysis.cutAreaSquareMetres, 0, 'g', 12)
        .arg(result.analysis.fillVolumeCubicMetres, 0, 'g', 12)
        .arg(result.analysis.fillAreaSquareMetres, 0, 'g', 12)
        .arg(result.analysis.signedVolumeCubicMetres, 0, 'g', 12)
        .arg(result.analysis.missingReferenceCellCount)
        .arg(resultLocation);
    statusBar()->showMessage(tr("挖填方与体积计算完成。"), 8000);
    emit sendStr2Console(summary);
    if (!result.warnings.isEmpty())
    {
        const QString warningText = result.warnings.join(QLatin1Char('\n'));
        QMessageBox::warning(
            this, tr("主成果已生成，但有导出提示"), warningText);
        emit sendStr2Console(warningText);
    }
    famp::cutfillui::CutFillResultDialog resultDialog(
        result.analysis, result.savedPaths, this);
    resultDialog.exec();
}

void MainWindow::slotGenerateTerrain()
{
    MyCloudList cloud;
    QString sourcePath;
    if (!selectedCloudData(cloud, &sourcePath))
    {
        QMessageBox::information(
            this, tr("DEM 与等高线"), tr("请先在内容列表中选择点云。"));
        return;
    }
    if (cloud.layer.locked)
    {
        QMessageBox::information(
            this, tr("DEM 与等高线"), tr("所选图层已锁定，无法生成地形成果。"));
        return;
    }

    QString sourceCrs = cloud.layer.crs.trimmed();
    if (sourceCrs.isEmpty())
        sourceCrs = projectCrs.trimmed();
    double horizontalUnitToMetre = 1.0;
    QString horizontalUnitName = tr("米（用户确认的本地平面坐标）");
    QString crsDescription = tr("未声明 CRS；将按本地米制平面坐标处理");
    if (!sourceCrs.isEmpty())
    {
        famp::crs::Info info;
        QString crsError;
        if (!famp::crs::inspect(sourceCrs, info, &crsError))
        {
            QMessageBox::warning(this, tr("DEM 与等高线"), crsError);
            return;
        }
        if (info.geographic)
        {
            QMessageBox::warning(
                this, tr("DEM 与等高线"),
                tr("所选点云使用地理经纬度坐标系 %1。DEM 网格不能直接以角度生成；"
                   "请先使用“工具 → 重投影所选点云…”转换到合适的投影坐标系。")
                    .arg(info.identifier));
            return;
        }
        if (!info.projected)
        {
            QMessageBox::warning(
                this, tr("DEM 与等高线"),
                tr("坐标系 %1 不是受支持的二维投影坐标系。请先重投影点云。")
                    .arg(info.identifier));
            return;
        }
        sourceCrs = info.identifier;
        horizontalUnitToMetre = info.horizontalUnitToMetre;
        horizontalUnitName = QStringLiteral("%1（1 单位 = %2 米）")
                                 .arg(info.horizontalUnitName)
                                 .arg(info.horizontalUnitToMetre, 0, 'g', 12);
        crsDescription = QStringLiteral("%1 — %2")
                             .arg(info.identifier, info.name);
    }
    else
    {
        const auto confirmation = QMessageBox::question(
            this, tr("确认本地坐标单位"),
            tr("所选点云和项目都未声明 CRS。是否确认其真实 X/Y 坐标是以米为单位的平面坐标？\n\n"
               "如果坐标实际是经纬度或其他单位，请先取消并设置/重投影坐标系。"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (confirmation != QMessageBox::Yes)
            return;
    }

    const QFileInfo sourceInfo(sourcePath);
    const QDir initialDirectory = sourceInfo.absoluteDir().exists()
        ? sourceInfo.absoluteDir()
        : QDir(QFileInfo(currentProjectPath).absolutePath());
    QString baseName = sourceInfo.completeBaseName();
    if (baseName.isEmpty())
        baseName = cloud.layer.name;
    if (baseName.trimmed().isEmpty())
        baseName = QStringLiteral("terrain");
    const QString initialSidecarPath = initialDirectory.filePath(
        baseName + QStringLiteral("_dem.famp-dem"));
    famp::terrainui::TerrainDialog dialog(
        cloud.layer.name, crsDescription, horizontalUnitName,
        horizontalUnitToMetre, initialSidecarPath, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    famp::terrainui::Options options = dialog.options();
    if (!options.sidecarPath.isEmpty())
        options.sidecarPath = QFileInfo(options.sidecarPath).absoluteFilePath();
    QString validationError;
    if (!famp::terrainui::validateOptions(options, &validationError))
    {
        QMessageBox::warning(this, tr("DEM 与等高线"), validationError);
        return;
    }
    const famp::terrainui::ExportPaths exportPaths =
        famp::terrainui::derivedExportPaths(options.sidecarPath);

    QStringList existingOutputs;
    const auto addExisting = [&existingOutputs](bool selected,
                                                const QString& path) {
        if (selected && QFileInfo::exists(path))
            existingOutputs.append(path);
    };
    addExisting(true, exportPaths.sidecar);
    addExisting(options.exportAsciiGrid, exportPaths.asciiGrid);
    addExisting(options.exportGridCsv, exportPaths.gridCsv);
    addExisting(options.exportContourCsv, exportPaths.contourCsv);
    addExisting(options.exportContourSvg, exportPaths.contourSvg);
    if (!existingOutputs.isEmpty())
    {
        const auto overwrite = QMessageBox::question(
            this, tr("覆盖地形成果"),
            tr("以下成果文件已经存在，将被原子替换：\n%1\n\n是否继续？")
                .arg(existingOutputs.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (overwrite != QMessageBox::Yes)
            return;
    }

    const famp::tasks::Handle task = taskManager->start(
        tr("生成 DEM 与等高线"), cloud.layer.name);
    if (!task.isValid())
    {
        QMessageBox::warning(
            this, tr("DEM 与等高线"), tr("无法创建后台地形分析任务。"));
        return;
    }

    QProgressDialog progress(
        tr("正在生成 DEM 与等高线…"), tr("取消"), 0, 100, this);
    progress.setWindowTitle(tr("DEM 与等高线"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    connect(&progress, &QProgressDialog::canceled, this, [this, task]() {
        taskManager->requestCancellation(task.id);
    });
    const QMetaObject::Connection progressConnection = connect(
        taskManager, &famp::tasks::TaskManager::taskChanged,
        &progress, [this, task, &progress](quint64 id) {
            if (id != task.id)
                return;
            famp::tasks::Snapshot snapshot;
            if (!taskManager->snapshot(id, snapshot))
                return;
            progress.setValue(static_cast<int>(std::clamp(
                snapshot.progress * 100.0, 0.0, 100.0)));
            if (!snapshot.message.isEmpty())
                progress.setLabelText(snapshot.message);
        });

    QFutureWatcher<TerrainTaskOutput> watcher;
    connect(&watcher, &QFutureWatcher<TerrainTaskOutput>::finished,
            &progress, &QProgressDialog::accept);
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr input =
        cloud.layer.points;
    const famp::cloud::SpatialReference spatial = cloud.layer.spatial;
    const QString layerId = cloud.layer.id;
    const QString layerName = cloud.layer.name;
    watcher.setFuture(QtConcurrent::run(
        [this, task, input, spatial, options, exportPaths,
         sourceCrs, horizontalUnitName, layerId, layerName]() {
            TerrainTaskOutput output;
            output.saveRequested = !options.sidecarPath.isEmpty();
            const auto shouldCancel = task.cancellationCheck();
            output.analysis = famp::terrain::analyze(
                input, spatial, options.grid, options.contours,
                shouldCancel,
                [this, task](double value) {
                    taskManager->setProgress(
                        task.id, value * 0.84,
                        tr("正在分析地形：%1%")
                            .arg(static_cast<int>(value * 100.0)));
                });
            if (output.analysis.cancelled
                || famp::tasks::isCancellationRequested(shouldCancel))
            {
                output.cancelled = true;
                output.error = tr("地形分析已取消，未写入未完成文件。");
                return output;
            }
            if (!output.analysis.succeeded())
            {
                output.error = output.analysis.error.isEmpty()
                    ? tr("地形分析失败。") : output.analysis.error;
                return output;
            }

            output.analysis.grid.sourceLayerId = layerId;
            output.analysis.grid.sourceLayerName = layerName;
            output.analysis.grid.sourceCrs = sourceCrs;
            output.analysis.grid.horizontalUnitName = horizontalUnitName;
            if (!output.saveRequested)
            {
                taskManager->setProgress(
                    task.id, 1.0, tr("DEM 与等高线内存成果已生成"));
                return output;
            }
            taskManager->setProgress(
                task.id, 0.86, tr("正在原子保存 DEM 项目边车…"));
            QString saveError;
            if (!famp::terrainio::saveGridAtomically(
                    exportPaths.sidecar, output.analysis.grid,
                    &saveError, shouldCancel))
            {
                output.cancelled =
                    famp::tasks::isCancellationRequested(shouldCancel);
                output.error = saveError;
                return output;
            }
            output.sidecarSaved = true;
            output.savedPaths.append(exportPaths.sidecar);

            const int exportCount = static_cast<int>(options.exportAsciiGrid)
                + static_cast<int>(options.exportGridCsv)
                + static_cast<int>(options.exportContourCsv)
                + static_cast<int>(options.exportContourSvg);
            int exportIndex = 0;
            const auto exportOne = [&](const QString& label,
                                       const QString& path,
                                       const auto& operation) {
                taskManager->setProgress(
                    task.id,
                    0.88 + 0.11 * exportIndex / std::max(1, exportCount),
                    tr("正在导出%1…").arg(label));
                ++exportIndex;
                QString exportError;
                if (operation(&exportError))
                {
                    output.savedPaths.append(path);
                    return true;
                }
                if (famp::tasks::isCancellationRequested(shouldCancel))
                {
                    output.cancelled = true;
                    output.error = exportError;
                    return false;
                }
                output.warnings.append(
                    tr("%1 导出失败：%2").arg(label, exportError));
                return true;
            };
            if (options.exportAsciiGrid
                && !exportOne(
                    tr("ASCII Grid"), exportPaths.asciiGrid,
                    [&](QString* error) {
                        return famp::terrainio::exportAsciiGridAtomically(
                            exportPaths.asciiGrid, output.analysis.grid,
                            error, shouldCancel);
                    }))
            {
                return output;
            }
            if (options.exportGridCsv
                && !exportOne(
                    tr("DEM CSV"), exportPaths.gridCsv,
                    [&](QString* error) {
                        return famp::terrainio::exportGridCsvAtomically(
                            exportPaths.gridCsv, output.analysis.grid,
                            error, shouldCancel);
                    }))
            {
                return output;
            }
            if (options.exportContourCsv
                && !exportOne(
                    tr("等高线 CSV"), exportPaths.contourCsv,
                    [&](QString* error) {
                        return famp::terrainio::exportContoursCsvAtomically(
                            exportPaths.contourCsv, output.analysis.contours,
                            error, shouldCancel);
                    }))
            {
                return output;
            }
            if (options.exportContourSvg
                && !exportOne(
                    tr("等高线 SVG"), exportPaths.contourSvg,
                    [&](QString* error) {
                        return famp::terrainio::exportContoursSvgAtomically(
                            exportPaths.contourSvg, output.analysis.contours,
                            sourceCrs, error, shouldCancel);
                    }))
            {
                return output;
            }
            taskManager->setProgress(task.id, 1.0, tr("地形成果已生成"));
            return output;
        }));
    if (!watcher.isFinished())
        progress.exec();
    const TerrainTaskOutput result = watcher.result();
    disconnect(progressConnection);

    if (result.cancelled)
    {
        QString message = result.error.isEmpty()
            ? tr("地形分析已取消。") : result.error;
        if (!result.savedPaths.isEmpty())
        {
            message += tr(" 已完成的成果文件已保留：%1")
                           .arg(result.savedPaths.join(QStringLiteral("；")));
        }
        taskManager->acknowledgeCancellation(task.id, message);
        statusBar()->showMessage(message, 8000);
        emit sendStr2Console(message);
        return;
    }
    if (!result.succeeded())
    {
        const QString message = result.error.isEmpty()
            ? tr("DEM 与等高线生成失败。") : result.error;
        taskManager->fail(task.id, message);
        QMessageBox::warning(this, tr("DEM 与等高线生成失败"), message);
        emit sendStr2Console(message);
        return;
    }
    const famp::workspace::EntityId sourceId(cloud.layer.id);
    const famp::workspace::WorkspaceEntity* sourceEntity =
        workspaceStore->entity(sourceId);
    if (!sourceEntity)
    {
        taskManager->fail(task.id, tr("来源点云实体已不存在"));
        return;
    }
    famp::workspace::Provenance terrainProvenance;
    terrainProvenance.operation = QStringLiteral("terrain_dem");
    terrainProvenance.sourceIds = {sourceId};
    terrainProvenance.sourceSnapshot = sourceSnapshot(*sourceEntity, cloud);
    terrainProvenance.parameters = QJsonObject{
        {QStringLiteral("automaticResolution"),
         options.grid.automaticResolution},
        {QStringLiteral("resolution"), result.analysis.grid.resolution},
        {QStringLiteral("statistic"),
         static_cast<int>(result.analysis.grid.statistic)},
        {QStringLiteral("fillSmallHoles"), options.grid.fillSmallHoles},
        {QStringLiteral("maximumHoleCells"), options.grid.maximumHoleCells},
        {QStringLiteral("contourInterval"),
         result.analysis.contourInterval},
        {QStringLiteral("contourBase"), result.analysis.contourBase},
        {QStringLiteral("smoothingIterations"),
         options.contours.smoothingIterations}};
    terrainProvenance.metrics = QJsonObject{
        {QStringLiteral("columns"), result.analysis.grid.columns},
        {QStringLiteral("rows"), result.analysis.grid.rows},
        {QStringLiteral("populatedCellCount"),
         result.analysis.grid.populatedCellCount},
        {QStringLiteral("filledCellCount"),
         result.analysis.grid.filledCellCount},
        {QStringLiteral("contourCount"), result.analysis.contours.size()}};

    famp::workspace::WorkspaceEntity demEntity =
        famp::workspace::makeEntity(
            famp::workspace::EntityKind::Dem,
            derivedEntityName(sourceEntity->name, QStringLiteral("_dem")));
    demEntity.provenance = terrainProvenance;
    demEntity.setPayload(
        std::make_shared<famp::terrain::Result>(result.analysis));
    if (result.sidecarSaved)
        demEntity.assetPath = exportPaths.sidecar;
    famp::workspace::EntityId demId;
    if (!integrateDerivedEntity(sourceId, std::move(demEntity), &demId))
    {
        taskManager->fail(task.id, tr("无法将 DEM 成果加入内容列表"));
        return;
    }

    if (!result.analysis.contours.isEmpty())
    {
        famp::workspace::Provenance contourProvenance = terrainProvenance;
        contourProvenance.operation = QStringLiteral("terrain_contours");
        famp::workspace::WorkspaceEntity contourEntity =
            famp::workspace::makeEntity(
                famp::workspace::EntityKind::ContourSet,
                derivedEntityName(
                    sourceEntity->name, QStringLiteral("_contours")));
        contourEntity.provenance = std::move(contourProvenance);
        contourEntity.setPayload(
            std::make_shared<famp::terrain::Result>(result.analysis));
        if (result.savedPaths.contains(exportPaths.contourCsv))
            contourEntity.assetPath = exportPaths.contourCsv;
        else if (result.savedPaths.contains(exportPaths.contourSvg))
            contourEntity.assetPath = exportPaths.contourSvg;
        if (!integrateDerivedEntity(sourceId, std::move(contourEntity)))
        {
            workspaceStore->removeEntity(demId);
            taskManager->fail(
                task.id, tr("无法将等高线成果加入内容列表"));
            return;
        }
    }
    taskManager->succeed(task.id, tr("DEM 与等高线生成完成"));

    QStringList warnings = result.warnings;
    bool addedToCanvas = false;
    if (options.addToCanvas)
    {
        if (result.analysis.contours.isEmpty())
        {
            warnings.append(tr("DEM 高程范围不足，未生成可加入画布的等高线。"));
        }
        else
        {
            QString canvasError;
            addedToCanvas = ui.graphicsView->addTerrainContours(
                result.analysis.contours,
                result.analysis.grid.horizontalUnitToMetre,
                result.analysis.grid.sourceCrs,
                result.analysis.grid.sourceLayerId,
                result.analysis.grid.sourceLayerName,
                result.sidecarSaved ? exportPaths.sidecar : QString(),
                result.analysis.contourInterval,
                result.analysis.contourBase,
                &canvasError);
            if (!addedToCanvas)
                warnings.append(tr("二维画布添加失败：%1").arg(canvasError));
        }
    }

    quint64 contourPointCount = 0;
    for (const auto& line : result.analysis.contours)
        contourPointCount += static_cast<quint64>(line.points.size());
    const QString resultLocation = result.savedPaths.isEmpty()
        ? tr("内存内容树（未自动落盘）")
        : result.savedPaths.join(QStringLiteral("；"));
    const QString summary = tr(
        "地形分析完成：%1 × %2 网格，分辨率 %3 米，%4 个有效单元"
        "（填补 %5），%6 条等高线/%7 个线点，等高距 %8。成果：%9%10")
        .arg(result.analysis.grid.columns)
        .arg(result.analysis.grid.rows)
        .arg(result.analysis.grid.resolution
                 * result.analysis.grid.horizontalUnitToMetre,
             0, 'g', 8)
        .arg(result.analysis.grid.populatedCellCount
             + result.analysis.grid.filledCellCount)
        .arg(result.analysis.grid.filledCellCount)
        .arg(result.analysis.contours.size())
        .arg(contourPointCount)
        .arg(result.analysis.contourInterval, 0, 'g', 8)
        .arg(resultLocation)
        .arg(addedToCanvas ? tr("；已加入二维画布") : QString());
    statusBar()->showMessage(tr("DEM 与等高线生成完成。"), 8000);
    emit sendStr2Console(summary);
    if (!warnings.isEmpty())
    {
        const QString warningText = warnings.join(QLatin1Char('\n'));
        QMessageBox::warning(
            this, tr("地形成果已生成，但有提示"), warningText);
        emit sendStr2Console(warningText);
    }
}

void MainWindow::slotOpenCloudCoordinateViewer()
{
    MyCloudList cloud;
    QString path;
    if (!selectedCloudData(cloud, &path))
    {
        QMessageBox::information(
            this, tr("点云坐标"), tr("请先在内容列表中选择点云。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("点云局部/真实坐标"));
    QFormLayout layout(&dialog);
    QLabel sourceLabel(QFileInfo(path).fileName(), &dialog);
    sourceLabel.setToolTip(path);
    QLabel originLabel(
        QStringLiteral("X=%1, Y=%2, Z=%3")
            .arg(cloud.layer.spatial.origin[0], 0, 'g', 15)
            .arg(cloud.layer.spatial.origin[1], 0, 'g', 15)
            .arg(cloud.layer.spatial.origin[2], 0, 'g', 15),
        &dialog);
    QLabel crsLabel(
        cloud.layer.crs.isEmpty() ? tr("未声明") : cloud.layer.crs, &dialog);
    layout.addRow(tr("点云"), &sourceLabel);
    layout.addRow(tr("原始坐标原点"), &originLabel);
    layout.addRow(tr("图层 CRS"), &crsLabel);

    QDoubleSpinBox localX(&dialog), localY(&dialog), localZ(&dialog);
    QDoubleSpinBox realX(&dialog), realY(&dialog), realZ(&dialog);
    const auto configureCoordinate = [](QDoubleSpinBox& spin) {
        spin.setRange(-1.0e12, 1.0e12);
        spin.setDecimals(8);
        spin.setSingleStep(0.1);
    };
    configureCoordinate(localX);
    configureCoordinate(localY);
    configureCoordinate(localZ);
    configureCoordinate(realX);
    configureCoordinate(realY);
    configureCoordinate(realZ);
    layout.addRow(tr("局部 X"), &localX);
    layout.addRow(tr("局部 Y"), &localY);
    layout.addRow(tr("局部 Z"), &localZ);
    layout.addRow(tr("真实 X"), &realX);
    layout.addRow(tr("真实 Y"), &realY);
    layout.addRow(tr("真实 Z"), &realZ);

    QDialogButtonBox buttons(QDialogButtonBox::Close, &dialog);
    QPushButton* toReal = buttons.addButton(
        tr("局部 → 真实"), QDialogButtonBox::ActionRole);
    QPushButton* toLocal = buttons.addButton(
        tr("真实 → 局部"), QDialogButtonBox::ActionRole);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(toReal, &QPushButton::clicked, &dialog, [&]() {
        famp::cloud::Point3d output{};
        QString error;
        if (!famp::cloud::localToReal(
                cloud.layer.spatial,
                {localX.value(), localY.value(), localZ.value()},
                output,
                &error))
        {
            QMessageBox::warning(&dialog, tr("坐标转换失败"), error);
            return;
        }
        realX.setValue(output[0]);
        realY.setValue(output[1]);
        realZ.setValue(output[2]);
    });
    connect(toLocal, &QPushButton::clicked, &dialog, [&]() {
        famp::cloud::Point3d output{};
        QString error;
        if (!famp::cloud::realToLocal(
                cloud.layer.spatial,
                {realX.value(), realY.value(), realZ.value()},
                output,
                &error))
        {
            QMessageBox::warning(&dialog, tr("坐标转换失败"), error);
            return;
        }
        localX.setValue(output[0]);
        localY.setValue(output[1]);
        localZ.setValue(output[2]);
    });
    layout.addRow(&buttons);
    toReal->click();
    dialog.exec();
}

void MainWindow::slotCloudDisplaySettings()
{
    MyCloudList cloud;
    if (!selectedCloudData(cloud))
    {
        QMessageBox::information(this, tr("点云显示设置"),
                                 tr("请先在内容列表中选择一个点云。"));
        return;
    }

    vtkActor* actor = cloud.cloudactor;
    vtkMapper* mapper = actor->GetMapper();
    if (!mapper)
    {
        QMessageBox::warning(this, tr("点云显示设置"),
                             tr("所选点云没有可用的渲染器。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("点云显示设置"));
    QFormLayout layout(&dialog);

    QDoubleSpinBox pointSize(&dialog);
    pointSize.setRange(1.0, 20.0);
    pointSize.setDecimals(1);
    pointSize.setSingleStep(0.5);
    pointSize.setValue(cloud.layer.display.pointSize);

    QDoubleSpinBox opacity(&dialog);
    opacity.setRange(0.05, 1.0);
    opacity.setDecimals(2);
    opacity.setSingleStep(0.05);
    opacity.setValue(cloud.layer.display.opacity);

    QComboBox colorMode(&dialog);
    colorMode.addItem(
        tr("使用点云 RGB"),
        static_cast<int>(famp::display::ColorMode::PointRgb));
    colorMode.addItem(
        tr("使用统一颜色"),
        static_cast<int>(famp::display::ColorMode::Uniform));
    colorMode.addItem(
        tr("按局部高程 Z 渐变"),
        static_cast<int>(famp::display::ColorMode::Elevation));
    QComboBox attributeName(&dialog);
    for (const QString& name : cloud.layer.attributes.names())
    {
        const auto* channel = cloud.layer.attributes.channel(name);
        const QString label = channel && !channel->unit.isEmpty()
            ? tr("%1 [%2]").arg(name, channel->unit) : name;
        attributeName.addItem(label, name);
    }
    if (attributeName.count() > 0)
    {
        colorMode.addItem(
            tr("按逐点属性渐变"),
            static_cast<int>(famp::display::ColorMode::Attribute));
        const int storedAttribute = attributeName.findData(
            cloud.layer.display.attributeName, Qt::UserRole,
            Qt::MatchFixedString);
        attributeName.setCurrentIndex(storedAttribute >= 0 ? storedAttribute : 0);
    }
    const int storedColorMode = colorMode.findData(
        static_cast<int>(cloud.layer.display.colorMode));
    colorMode.setCurrentIndex(storedColorMode >= 0 ? storedColorMode : 0);

    QColor selectedColor = QColor::fromRgbF(
        cloud.layer.display.red,
        cloud.layer.display.green,
        cloud.layer.display.blue);
    QPushButton colorButton(tr("选择颜色…"), &dialog);
    QCheckBox automaticRange(tr("自动使用数据范围"), &dialog);
    automaticRange.setChecked(cloud.layer.display.automaticScalarRange);
    QDoubleSpinBox scalarMinimum(&dialog);
    QDoubleSpinBox scalarMaximum(&dialog);
    for (QDoubleSpinBox* spinBox : {&scalarMinimum, &scalarMaximum})
    {
        spinBox->setRange(-1.0e100, 1.0e100);
        spinBox->setDecimals(6);
    }
    scalarMinimum.setValue(cloud.layer.display.scalarMinimum);
    scalarMaximum.setValue(cloud.layer.display.scalarMaximum);
    QLabel attributeInfo(&dialog);
    attributeInfo.setWordWrap(true);
    QHash<QString, famp::cloud::AttributeSummary> attributeSummaryCache;
    auto cachedAttributeSummary = [&](const QString& name,
                                      famp::cloud::AttributeSummary& summary) {
        const QString key = name.trimmed().toCaseFolded();
        auto found = attributeSummaryCache.constFind(key);
        if (found == attributeSummaryCache.cend())
        {
            const auto* channel = cloud.layer.attributes.channel(name);
            if (!channel)
                return false;
            attributeSummaryCache.insert(key, channel->summary());
            found = attributeSummaryCache.constFind(key);
        }
        summary = found.value();
        return true;
    };
    auto updateColorButton = [&]() {
        colorButton.setStyleSheet(
            QStringLiteral("background-color: %1;").arg(selectedColor.name()));
    };
    updateColorButton();
    auto updateColorControls = [&]() {
        const auto mode = static_cast<famp::display::ColorMode>(
            colorMode.currentData().toInt());
        const bool elevation = mode == famp::display::ColorMode::Elevation;
        const bool attribute = mode == famp::display::ColorMode::Attribute;
        const bool scalar = elevation || attribute;
        colorButton.setEnabled(mode == famp::display::ColorMode::Uniform);
        attributeName.setEnabled(attribute);
        automaticRange.setEnabled(scalar);
        scalarMinimum.setEnabled(scalar && !automaticRange.isChecked());
        scalarMaximum.setEnabled(scalar && !automaticRange.isChecked());

        QString unit;
        if (elevation)
            unit = QStringLiteral("m");
        else if (attribute)
        {
            const auto* channel = cloud.layer.attributes.channel(
                attributeName.currentData().toString());
            if (channel)
                unit = channel->unit;
        }
        const QString suffix = unit.isEmpty()
            ? QString() : QStringLiteral(" ") + unit;
        scalarMinimum.setSuffix(suffix);
        scalarMaximum.setSuffix(suffix);

        double dataMinimum = 0.0;
        double dataMaximum = 0.0;
        famp::cloud::AttributeSummary summary;
        const bool hasSummary = attribute
            && cachedAttributeSummary(
                attributeName.currentData().toString(), summary);
        bool hasRange = false;
        if (elevation)
        {
            hasRange = famp::display::elevationRange(
                actor, dataMinimum, dataMaximum, nullptr);
        }
        else if (hasSummary && summary.hasFiniteRange)
        {
            dataMinimum = summary.minimum;
            dataMaximum = summary.maximum;
            hasRange = true;
        }
        if (automaticRange.isChecked() && hasRange)
        {
            scalarMinimum.setValue(dataMinimum);
            scalarMaximum.setValue(dataMaximum);
        }
        if (attribute)
        {
            attributeInfo.setText(
                hasSummary && hasRange
                    ? tr("类型：%1；值数：%2；范围：%3 – %4%5")
                          .arg(famp::cloud::attributeValueTypeName(summary.type))
                          .arg(summary.valueCount)
                          .arg(dataMinimum, 0, 'g', 12)
                          .arg(dataMaximum, 0, 'g', 12)
                          .arg(suffix)
                    : tr("所选属性没有可用数值。"));
        }
        else
        {
            attributeInfo.clear();
        }
    };
    connect(&colorMode, qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog, [&](int) { updateColorControls(); });
    connect(&automaticRange, &QCheckBox::toggled,
            &dialog, [&](bool) { updateColorControls(); });
    connect(&attributeName, qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog, [&](int) { updateColorControls(); });
    updateColorControls();
    connect(&colorButton, &QPushButton::clicked, &dialog, [&]() {
        const QColor color = QColorDialog::getColor(
            selectedColor, &dialog, tr("选择点云颜色"));
        if (color.isValid())
        {
            selectedColor = color;
            updateColorButton();
        }
    });

    layout.addRow(tr("点大小"), &pointSize);
    layout.addRow(tr("不透明度"), &opacity);
    layout.addRow(tr("颜色模式"), &colorMode);
    layout.addRow(tr("统一颜色"), &colorButton);
    layout.addRow(tr("逐点属性"), &attributeName);
    layout.addRow(tr("属性信息"), &attributeInfo);
    layout.addRow(QString(), &automaticRange);
    layout.addRow(tr("色带最小值"), &scalarMinimum);
    layout.addRow(tr("色带最大值"), &scalarMaximum);
    QDialogButtonBox buttons(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout.addRow(&buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    famp::display::Settings settings;
    settings.pointSize = pointSize.value();
    settings.opacity = opacity.value();
    settings.colorMode = static_cast<famp::display::ColorMode>(
        colorMode.currentData().toInt());
    settings.red = selectedColor.redF();
    settings.green = selectedColor.greenF();
    settings.blue = selectedColor.blueF();
    settings.automaticScalarRange = automaticRange.isChecked();
    settings.scalarMinimum = scalarMinimum.value();
    settings.scalarMaximum = scalarMaximum.value();
    settings.attributeName = attributeName.currentData().toString();
    QString error;
    if (!famp::display::validateSettings(settings, &error))
    {
        QMessageBox::warning(this, tr("点云显示设置"), error);
        return;
    }
    if (settings.colorMode == famp::display::ColorMode::Attribute)
    {
        famp::cloud::AttributeSummary summary;
        if (!cachedAttributeSummary(settings.attributeName, summary)
            || !summary.hasFiniteRange)
        {
            QMessageBox::warning(
                this, tr("点云显示设置"),
                tr("所选逐点属性不包含可用的有限数值。"));
            return;
        }
        if (!famp::display::attachAttribute(
                actor, cloud.layer.attributes, settings.attributeName, &error))
        {
            QMessageBox::warning(this, tr("点云显示设置"), error);
            return;
        }
    }
    if (!famp::display::apply(actor, settings, &error))
    {
        QMessageBox::warning(this, tr("点云显示设置"), error);
        return;
    }
    cloud.layer.display = settings;
    ++cloud.layer.revision;
    updateCloudData(cloud);
    markProjectDirty();
    myVTK->refresh();
    emit sendStr2Console(tr("已更新所选点云的显示设置。"));
}

void MainWindow::slotPreprocessCloud()
{
    MyCloudList cloud;
    QString sourcePath;
    if (!selectedCloudData(cloud, &sourcePath))
    {
        QMessageBox::information(this, tr("点云预处理"),
                                 tr("请先在内容列表中选择一个点云。"));
        return;
    }
    if (cloud.layer.locked)
    {
        QMessageBox::information(
            this, tr("点云预处理"), tr("所选点云已锁定。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("点云预处理"));
    QFormLayout layout(&dialog);

    QComboBox method(&dialog);
    method.addItem(tr("体素降采样"),
                   static_cast<int>(famp::processing::Method::VoxelDownsample));
    method.addItem(
        tr("统计离群点去噪"),
        static_cast<int>(famp::processing::Method::StatisticalOutlierRemoval));

    QDoubleSpinBox leafSize(&dialog);
    leafSize.setDecimals(6);
    leafSize.setRange(0.000001, 1000.0);
    leafSize.setValue(0.01);
    leafSize.setSuffix(tr(" m"));

    QSpinBox meanNeighbors(&dialog);
    const std::size_t availableNeighbors = cloud.layer.points->size() > 1
        ? cloud.layer.points->size() - 1
        : 0;
    const int maximumNeighbors = std::max(
        2, static_cast<int>(std::min<std::size_t>(10000, availableNeighbors)));
    meanNeighbors.setRange(2, maximumNeighbors);
    meanNeighbors.setValue(std::min(20, maximumNeighbors));

    QDoubleSpinBox deviationMultiplier(&dialog);
    deviationMultiplier.setRange(0.01, 100.0);
    deviationMultiplier.setDecimals(2);
    deviationMultiplier.setValue(1.0);

    QLabel note(
        tr("处理结果作为内存点云加入左侧内容列表，"
           "默认不写入磁盘。来源点云会自动隐藏；"
           "体素成果的逐点属性取距质心最近的原点值。"),
        &dialog);
    note.setWordWrap(true);

    QPushButton loadRecipeButton(tr("载入处理方案…"), &dialog);
    QPushButton saveRecipeButton(tr("保存处理方案…"), &dialog);
    QHBoxLayout recipeButtons;
    recipeButtons.addWidget(&loadRecipeButton);
    recipeButtons.addWidget(&saveRecipeButton);

    layout.addRow(tr("方法"), &method);
    layout.addRow(tr("体素边长"), &leafSize);
    layout.addRow(tr("邻域点数"), &meanNeighbors);
    layout.addRow(tr("标准差倍数"), &deviationMultiplier);
    layout.addRow(tr("可复现方案"), &recipeButtons);
    layout.addRow(&note);
    auto updateParameterAvailability = [&]() {
        const bool voxel = static_cast<famp::processing::Method>(
            method.currentData().toInt())
            == famp::processing::Method::VoxelDownsample;
        leafSize.setEnabled(voxel);
        meanNeighbors.setEnabled(!voxel);
        deviationMultiplier.setEnabled(!voxel);
    };
    connect(&method, qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog, [&](int) { updateParameterAvailability(); });
    updateParameterAvailability();

    auto currentOptions = [&]() {
        famp::processing::Options current;
        current.method = static_cast<famp::processing::Method>(
            method.currentData().toInt());
        current.voxelLeafSizeMeters = leafSize.value();
        current.meanNeighbors = meanNeighbors.value();
        current.standardDeviationMultiplier = deviationMultiplier.value();
        return current;
    };
    connect(&loadRecipeButton, &QPushButton::clicked, &dialog, [&]() {
        const QString path = QFileDialog::getOpenFileName(
            &dialog, tr("载入处理方案"), QFileInfo(sourcePath).absolutePath(),
            tr("FAMP 处理方案 (*.famp-process.json *.json)"));
        if (path.isEmpty())
            return;
        famp::recipe::Recipe recipe;
        QString recipeError;
        if (!famp::recipe::load(path, recipe, &recipeError))
        {
            QMessageBox::warning(&dialog, tr("处理方案无效"), recipeError);
            return;
        }
        if (recipe.operation == famp::recipe::Operation::RangeCrop)
        {
            QMessageBox::warning(
                &dialog, tr("处理方案不兼容"),
                tr("该方案用于范围裁剪，请在“按坐标范围裁剪”中载入。"));
            return;
        }
        if (!famp::processing::validateOptions(
                recipe.processing, cloud.layer.points->size(), &recipeError))
        {
            QMessageBox::warning(&dialog, tr("处理方案不适用"), recipeError);
            return;
        }
        QString sourceWarning;
        if (!famp::recipe::sourceMatches(recipe, sourcePath, &sourceWarning))
            QMessageBox::warning(&dialog, tr("处理方案来源不同"), sourceWarning);
        method.setCurrentIndex(method.findData(
            static_cast<int>(recipe.processing.method)));
        leafSize.setValue(recipe.processing.voxelLeafSizeMeters);
        meanNeighbors.setValue(recipe.processing.meanNeighbors);
        deviationMultiplier.setValue(
            recipe.processing.standardDeviationMultiplier);
        updateParameterAvailability();
    });
    connect(&saveRecipeButton, &QPushButton::clicked, &dialog, [&]() {
        const auto current = currentOptions();
        QString recipeError;
        if (!famp::processing::validateOptions(
                current, cloud.layer.points->size(), &recipeError))
        {
            QMessageBox::warning(&dialog, tr("处理参数无效"), recipeError);
            return;
        }
        const QFileInfo sourceInfo(sourcePath);
        const QString initialRecipePath = sourceInfo.absoluteDir().filePath(
            sourceInfo.completeBaseName() + QStringLiteral(".famp-process.json"));
        const QString path = QFileDialog::getSaveFileName(
            &dialog, tr("保存处理方案"), initialRecipePath,
            tr("FAMP 处理方案 (*.famp-process.json *.json)"));
        if (path.isEmpty())
            return;
        if (!famp::recipe::save(
                path, famp::recipe::forProcessing(current, sourcePath),
                nullptr, &recipeError))
        {
            QMessageBox::warning(&dialog, tr("保存处理方案失败"), recipeError);
        }
    });

    QDialogButtonBox buttons(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons.button(QDialogButtonBox::Ok)->setText(tr("生成内存成果"));
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout.addRow(&buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const famp::processing::Options options = currentOptions();
    QString validationError;
    if (!famp::processing::validateOptions(
            options, cloud.layer.points->size(), &validationError))
    {
        QMessageBox::warning(this, tr("点云预处理"), validationError);
        return;
    }

    const QString suffix = options.method
            == famp::processing::Method::VoxelDownsample
        ? QStringLiteral("_voxel")
        : QStringLiteral("_denoised");
    const QString resultName = derivedEntityName(cloud.layer.name, suffix);

    QProgressDialog progress(
        tr("正在后台生成预处理点云…"), tr("取消"), 0, 0, this);
    progress.setWindowTitle(tr("点云预处理"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);

    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    connect(&progress, &QProgressDialog::canceled, this, [cancellation]() {
        cancellation->store(true, std::memory_order_relaxed);
    });
    QFutureWatcher<famp::processing::Result> watcher;
    connect(&watcher, &QFutureWatcher<famp::processing::Result>::finished,
            &progress, &QProgressDialog::accept);
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr input = cloud.layer.points;
    watcher.setFuture(QtConcurrent::run(
        [input, options, cancellation]() {
            return famp::processing::process(
                input, options, [cancellation]() {
                    return cancellation->load(std::memory_order_relaxed);
                });
        }));
    if (!watcher.isFinished())
        progress.exec();

    const famp::processing::Result result = watcher.result();
    if (result.cancelled)
    {
        statusBar()->showMessage(tr("点云预处理已取消，未生成成果。"), 5000);
        emit sendStr2Console(tr("点云预处理已取消  %1").arg(sourcePath));
        return;
    }
    if (!result.succeeded())
    {
        QMessageBox::warning(this, tr("点云预处理失败"), result.error);
        return;
    }

    famp::cloud::CloudAttributes resultAttributes;
    QString attributeError;
    if (!cloud.layer.attributes.select(
            result.sourceIndices, resultAttributes, &attributeError))
    {
        QMessageBox::warning(this, tr("点云预处理失败"), attributeError);
        return;
    }
    const famp::workspace::WorkspaceEntity* sourceEntity =
        workspaceStore->entity(entityIdForCloud(cloud));
    if (!sourceEntity)
        return;
    famp::workspace::Provenance provenance;
    provenance.operation = options.method
            == famp::processing::Method::VoxelDownsample
        ? QStringLiteral("voxel_downsample")
        : QStringLiteral("statistical_outlier_removal");
    provenance.sourceIds = {sourceEntity->id};
    provenance.sourceSnapshot = sourceSnapshot(*sourceEntity, cloud);
    provenance.parameters = QJsonObject{
        {QStringLiteral("voxelLeafSizeMeters"), options.voxelLeafSizeMeters},
        {QStringLiteral("meanNeighbors"), options.meanNeighbors},
        {QStringLiteral("standardDeviationMultiplier"),
         options.standardDeviationMultiplier}};
    if (!integrateDerivedCloud(
            cloud, resultName, result.cloud, cloud.layer.spatial,
            cloud.layer.crs, resultAttributes, std::move(provenance)))
    {
        return;
    }
    emit sendStr2Console(
        tr("点云预处理完成：%1 → %2 个点，成果保留在内存中")
            .arg(result.inputPointCount)
            .arg(result.outputPointCount));
}

void MainWindow::slotCropCloud()
{
    MyCloudList cloud;
    QString sourcePath;
    if (!selectedCloudData(cloud, &sourcePath))
    {
        QMessageBox::information(this, tr("点云范围裁剪"),
                                 tr("请先在内容列表中选择一个点云。"));
        return;
    }
    if (cloud.layer.locked)
    {
        QMessageBox::information(
            this, tr("点云范围裁剪"), tr("所选点云已锁定。"));
        return;
    }

    famp::crop::Options options;
    QString error;
    if (!famp::crop::dataBounds(cloud.layer.points, options, &error))
    {
        QMessageBox::warning(this, tr("点云范围裁剪"), error);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("按局部坐标范围裁剪"));
    QFormLayout layout(&dialog);
    QDoubleSpinBox minimumX(&dialog), maximumX(&dialog);
    QDoubleSpinBox minimumY(&dialog), maximumY(&dialog);
    QDoubleSpinBox minimumZ(&dialog), maximumZ(&dialog);
    for (QDoubleSpinBox* spinBox : {
             &minimumX, &maximumX, &minimumY,
             &maximumY, &minimumZ, &maximumZ})
    {
        spinBox->setRange(-1.0e9, 1.0e9);
        // Point coordinates are floats. Nine decimal places preserve their
        // extrema while still allowing practical metre-based editing.
        spinBox->setDecimals(9);
        spinBox->setSuffix(tr(" m"));
    }
    minimumX.setValue(options.minimumX);
    maximumX.setValue(options.maximumX);
    minimumY.setValue(options.minimumY);
    maximumY.setValue(options.maximumY);
    minimumZ.setValue(options.minimumZ);
    maximumZ.setValue(options.maximumZ);
    QComboBox keepMode(&dialog);
    keepMode.addItem(tr("保留范围内部"), true);
    keepMode.addItem(tr("保留范围外部"), false);
    QPushButton loadRecipeButton(tr("载入处理方案…"), &dialog);
    QPushButton saveRecipeButton(tr("保存处理方案…"), &dialog);
    QHBoxLayout recipeButtons;
    recipeButtons.addWidget(&loadRecipeButton);
    recipeButtons.addWidget(&saveRecipeButton);
    QLabel note(tr("范围使用当前点云的局部坐标；结果作为内存点云"
                   "加入左侧内容列表，默认不写入磁盘。"
                   "逐点属性按裁剪保留的原始索引精确复制。"),
                &dialog);
    note.setWordWrap(true);
    layout.addRow(tr("X 最小值"), &minimumX);
    layout.addRow(tr("X 最大值"), &maximumX);
    layout.addRow(tr("Y 最小值"), &minimumY);
    layout.addRow(tr("Y 最大值"), &maximumY);
    layout.addRow(tr("Z 最小值"), &minimumZ);
    layout.addRow(tr("Z 最大值"), &maximumZ);
    layout.addRow(tr("保留模式"), &keepMode);
    layout.addRow(tr("可复现方案"), &recipeButtons);
    layout.addRow(&note);
    auto currentCropOptions = [&]() {
        famp::crop::Options current;
        current.minimumX = minimumX.value();
        current.maximumX = maximumX.value();
        current.minimumY = minimumY.value();
        current.maximumY = maximumY.value();
        current.minimumZ = minimumZ.value();
        current.maximumZ = maximumZ.value();
        current.keepInside = keepMode.currentData().toBool();
        return current;
    };
    connect(&loadRecipeButton, &QPushButton::clicked, &dialog, [&]() {
        const QString path = QFileDialog::getOpenFileName(
            &dialog, tr("载入处理方案"), QFileInfo(sourcePath).absolutePath(),
            tr("FAMP 处理方案 (*.famp-process.json *.json)"));
        if (path.isEmpty())
            return;
        famp::recipe::Recipe recipe;
        QString recipeError;
        if (!famp::recipe::load(path, recipe, &recipeError))
        {
            QMessageBox::warning(&dialog, tr("处理方案无效"), recipeError);
            return;
        }
        if (recipe.operation != famp::recipe::Operation::RangeCrop)
        {
            QMessageBox::warning(
                &dialog, tr("处理方案不兼容"),
                tr("该方案用于点云预处理，请在“点云预处理”中载入。"));
            return;
        }
        QString sourceWarning;
        if (!famp::recipe::sourceMatches(recipe, sourcePath, &sourceWarning))
            QMessageBox::warning(&dialog, tr("处理方案来源不同"), sourceWarning);
        minimumX.setValue(recipe.crop.minimumX);
        maximumX.setValue(recipe.crop.maximumX);
        minimumY.setValue(recipe.crop.minimumY);
        maximumY.setValue(recipe.crop.maximumY);
        minimumZ.setValue(recipe.crop.minimumZ);
        maximumZ.setValue(recipe.crop.maximumZ);
        keepMode.setCurrentIndex(recipe.crop.keepInside ? 0 : 1);
    });
    connect(&saveRecipeButton, &QPushButton::clicked, &dialog, [&]() {
        const auto current = currentCropOptions();
        QString recipeError;
        if (!famp::crop::validateOptions(current, &recipeError))
        {
            QMessageBox::warning(&dialog, tr("裁剪参数无效"), recipeError);
            return;
        }
        const QFileInfo sourceInfo(sourcePath);
        const QString initialRecipePath = sourceInfo.absoluteDir().filePath(
            sourceInfo.completeBaseName()
            + QStringLiteral("_crop.famp-process.json"));
        const QString path = QFileDialog::getSaveFileName(
            &dialog, tr("保存处理方案"), initialRecipePath,
            tr("FAMP 处理方案 (*.famp-process.json *.json)"));
        if (path.isEmpty())
            return;
        if (!famp::recipe::save(
                path, famp::recipe::forCrop(current, sourcePath),
                nullptr, &recipeError))
        {
            QMessageBox::warning(&dialog, tr("保存处理方案失败"), recipeError);
        }
    });
    QDialogButtonBox buttons(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons.button(QDialogButtonBox::Ok)->setText(tr("生成内存成果"));
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout.addRow(&buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    options = currentCropOptions();
    if (!famp::crop::validateOptions(options, &error))
    {
        QMessageBox::warning(this, tr("点云范围裁剪"), error);
        return;
    }

    const QString resultName = derivedEntityName(
        cloud.layer.name, QStringLiteral("_cropped"));

    QProgressDialog progress(
        tr("正在后台生成裁剪点云…"), tr("取消"), 0, 0, this);
    progress.setWindowTitle(tr("点云范围裁剪"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    connect(&progress, &QProgressDialog::canceled, this, [cancellation]() {
        cancellation->store(true, std::memory_order_relaxed);
    });
    QFutureWatcher<famp::crop::Result> watcher;
    connect(&watcher, &QFutureWatcher<famp::crop::Result>::finished,
            &progress, &QProgressDialog::accept);
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr input = cloud.layer.points;
    watcher.setFuture(QtConcurrent::run(
        [input, options, cancellation]() {
            return famp::crop::process(
                input, options, [cancellation]() {
                    return cancellation->load(std::memory_order_relaxed);
                });
        }));
    if (!watcher.isFinished())
        progress.exec();
    const famp::crop::Result result = watcher.result();
    if (result.cancelled)
    {
        statusBar()->showMessage(tr("点云范围裁剪已取消，未生成成果。"), 5000);
        return;
    }
    if (!result.succeeded())
    {
        QMessageBox::warning(this, tr("点云范围裁剪失败"), result.error);
        return;
    }

    famp::cloud::CloudAttributes resultAttributes;
    QString attributeError;
    if (!cloud.layer.attributes.select(
            result.sourceIndices, resultAttributes, &attributeError))
    {
        QMessageBox::warning(this, tr("点云范围裁剪失败"), attributeError);
        return;
    }
    const famp::workspace::WorkspaceEntity* sourceEntity =
        workspaceStore->entity(entityIdForCloud(cloud));
    if (!sourceEntity)
        return;
    famp::workspace::Provenance provenance;
    provenance.operation = QStringLiteral("range_crop");
    provenance.sourceIds = {sourceEntity->id};
    provenance.sourceSnapshot = sourceSnapshot(*sourceEntity, cloud);
    provenance.parameters = QJsonObject{
        {QStringLiteral("minimumX"), options.minimumX},
        {QStringLiteral("maximumX"), options.maximumX},
        {QStringLiteral("minimumY"), options.minimumY},
        {QStringLiteral("maximumY"), options.maximumY},
        {QStringLiteral("minimumZ"), options.minimumZ},
        {QStringLiteral("maximumZ"), options.maximumZ},
        {QStringLiteral("keepInside"), options.keepInside}};
    if (!integrateDerivedCloud(
            cloud, resultName, result.cloud, cloud.layer.spatial,
            cloud.layer.crs, resultAttributes, std::move(provenance), false))
    {
        return;
    }
    emit sendStr2Console(
        tr("点云范围裁剪完成：%1 → %2 个点，成果保留在内存中")
            .arg(result.inputPointCount)
            .arg(result.outputPointCount));
}

void MainWindow::slotRegisterCloud()
{
    if (pointCloudList.size() < 2)
    {
        QMessageBox::information(this, tr("点云 ICP 配准"),
                                 tr("请先加载至少两个点云。"));
        return;
    }

    struct Entry
    {
        MyCloudList cloud;
        QString path;
        QString label;
    };
    std::vector<Entry> entries;
    for (const MyCloudList& candidate : pointCloudList)
    {
        if (!candidate.layer.points || candidate.layer.points->empty())
            continue;
        const famp::workspace::WorkspaceEntity* entity =
            workspaceStore->entity(entityIdForCloud(candidate));
        Entry entry;
        entry.cloud = candidate;
        entry.path = entity && entity->assetPath.has_value()
            ? *entity->assetPath : candidate.layer.sourcePath;
        entry.label = QFileInfo(entry.path).fileName();
        if (entry.label.isEmpty())
            entry.label = entity ? entity->name : tr("点云 %1").arg(candidate.id);
        entries.push_back(entry);
    }
    if (entries.size() < 2)
    {
        QMessageBox::information(this, tr("点云 ICP 配准"),
                                 tr("当前没有两个可用点云。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("点云 ICP 配准"));
    QFormLayout layout(&dialog);
    QComboBox sourceCombo(&dialog);
    QComboBox targetCombo(&dialog);
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        sourceCombo.addItem(entries[index].label, static_cast<int>(index));
        targetCombo.addItem(entries[index].label, static_cast<int>(index));
    }
    MyCloudList selected;
    if (selectedCloudData(selected))
    {
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            if (entries[index].cloud.id == selected.id)
            {
                sourceCombo.setCurrentIndex(static_cast<int>(index));
                break;
            }
        }
    }
    targetCombo.setCurrentIndex(sourceCombo.currentIndex() == 0 ? 1 : 0);

    QSpinBox iterations(&dialog);
    iterations.setRange(1, 10000);
    iterations.setValue(60);
    QDoubleSpinBox correspondenceDistance(&dialog);
    correspondenceDistance.setDecimals(6);
    correspondenceDistance.setRange(0.000001, 100000.0);
    correspondenceDistance.setValue(1.0);
    correspondenceDistance.setSuffix(tr(" m"));
    QDoubleSpinBox samplingVoxelSize(&dialog);
    samplingVoxelSize.setDecimals(4);
    samplingVoxelSize.setRange(0.0, 1000.0);
    samplingVoxelSize.setSingleStep(0.01);
    samplingVoxelSize.setValue(0.05);
    samplingVoxelSize.setSpecialValueText(tr("不降采样"));
    samplingVoxelSize.setSuffix(tr(" m"));
    QDoubleSpinBox transformationEpsilon(&dialog);
    transformationEpsilon.setDecimals(12);
    transformationEpsilon.setRange(1.0e-12, 1.0);
    transformationEpsilon.setValue(1.0e-8);
    QDoubleSpinBox fitnessEpsilon(&dialog);
    fitnessEpsilon.setDecimals(12);
    fitnessEpsilon.setRange(1.0e-12, 1.0);
    fitnessEpsilon.setValue(1.0e-8);
    QDoubleSpinBox minimumOverlap(&dialog);
    minimumOverlap.setDecimals(1);
    minimumOverlap.setRange(1.0, 100.0);
    minimumOverlap.setSingleStep(5.0);
    minimumOverlap.setValue(25.0);
    minimumOverlap.setSuffix(tr(" %"));
    QLabel note(
        tr("将源点云换算到目标点云的局部坐标框架后执行刚性 ICP。"
           "两者必须声明相同的米制投影 CRS。"
           "双向重叠率用于阻止少量点偶然匹配被误判为成功；"
           "成果作为目标坐标框架下的内存点云加入内容列表；"
           "源点云自动隐藏，目标点云保持可见。"),
        &dialog);
    note.setWordWrap(true);
    layout.addRow(tr("源点云（移动）"), &sourceCombo);
    layout.addRow(tr("目标点云（固定）"), &targetCombo);
    layout.addRow(tr("最大迭代次数"), &iterations);
    layout.addRow(tr("最大对应距离"), &correspondenceDistance);
    layout.addRow(tr("配准体素边长"), &samplingVoxelSize);
    layout.addRow(tr("变换收敛阈值"), &transformationEpsilon);
    layout.addRow(tr("适应度收敛阈值"), &fitnessEpsilon);
    layout.addRow(tr("最小双向重叠率"), &minimumOverlap);
    layout.addRow(&note);
    QDialogButtonBox buttons(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons.button(QDialogButtonBox::Ok)->setText(tr("生成内存成果"));
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout.addRow(&buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (sourceCombo.currentIndex() == targetCombo.currentIndex())
    {
        QMessageBox::warning(this, tr("点云 ICP 配准"),
                             tr("源点云和目标点云必须不同。"));
        return;
    }

    const Entry source = entries[static_cast<std::size_t>(sourceCombo.currentIndex())];
    const Entry target = entries[static_cast<std::size_t>(targetCombo.currentIndex())];
    if (source.cloud.layer.locked)
    {
        QMessageBox::information(
            this, tr("点云 ICP 配准"), tr("源点云已锁定。"));
        return;
    }

    famp::crs::Info sourceCrsInfo;
    famp::crs::Info targetCrsInfo;
    QString crsError;
    if (source.cloud.layer.crs.trimmed().isEmpty()
        || target.cloud.layer.crs.trimmed().isEmpty()
        || !famp::crs::inspect(
            source.cloud.layer.crs, sourceCrsInfo, &crsError)
        || !famp::crs::inspect(
            target.cloud.layer.crs, targetCrsInfo, &crsError))
    {
        QMessageBox::warning(
            this, tr("点云 ICP 配准"),
            crsError.isEmpty()
                ? tr("ICP 要求源点云和目标点云都声明有效 CRS。")
                : crsError);
        return;
    }
    if (sourceCrsInfo.identifier != targetCrsInfo.identifier
        || !sourceCrsInfo.projected || !targetCrsInfo.projected
        || std::abs(sourceCrsInfo.horizontalUnitToMetre - 1.0) > 1.0e-12
        || std::abs(targetCrsInfo.horizontalUnitToMetre - 1.0) > 1.0e-12)
    {
        QMessageBox::warning(
            this, tr("点云 ICP 配准"),
            tr("ICP 仅接受相同的米制投影 CRS。\n源：%1\n目标：%2")
                .arg(sourceCrsInfo.identifier, targetCrsInfo.identifier));
        return;
    }
    famp::registration::Options options;
    options.maximumIterations = iterations.value();
    options.maximumCorrespondenceDistance = correspondenceDistance.value();
    options.samplingVoxelSizeMeters = samplingVoxelSize.value();
    options.transformationEpsilon = transformationEpsilon.value();
    options.fitnessEpsilon = fitnessEpsilon.value();
    options.minimumOverlapRatio = minimumOverlap.value() / 100.0;
    QString validationError;
    if (!famp::registration::validateOptions(options, &validationError))
    {
        QMessageBox::warning(this, tr("点云 ICP 配准"), validationError);
        return;
    }
    QString targetStem = QFileInfo(target.label).completeBaseName();
    if (targetStem.isEmpty())
        targetStem = target.label;
    const QString resultName = derivedEntityName(
        source.cloud.layer.name,
        QStringLiteral("_registered_to_") + targetStem);

    QProgressDialog progress(tr("正在后台生成 ICP 配准点云…"), tr("取消"), 0, 0, this);
    progress.setWindowTitle(tr("点云 ICP 配准"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    connect(&progress, &QProgressDialog::canceled, this, [cancellation]() {
        cancellation->store(true, std::memory_order_relaxed);
    });
    QFutureWatcher<famp::registration::Result> watcher;
    connect(&watcher, &QFutureWatcher<famp::registration::Result>::finished,
            &progress, &QProgressDialog::accept);
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr sourceCloud = source.cloud.layer.points;
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr targetCloud = target.cloud.layer.points;
    const famp::cloud::SpatialReference sourceSpatial = source.cloud.layer.spatial;
    const famp::cloud::SpatialReference targetSpatial = target.cloud.layer.spatial;
    watcher.setFuture(QtConcurrent::run(
        [sourceCloud, sourceSpatial, targetCloud, targetSpatial,
         options, cancellation]() {
            return famp::registration::alignInTargetFrame(
                sourceCloud, sourceSpatial, targetCloud, targetSpatial,
                options, [cancellation]() {
                    return cancellation->load(std::memory_order_relaxed);
                });
        }));
    if (!watcher.isFinished())
        progress.exec();
    const famp::registration::Result result = watcher.result();
    if (result.cancelled)
    {
        statusBar()->showMessage(tr("点云配准已取消，未生成成果。"), 5000);
        return;
    }
    if (!result.succeeded())
    {
        QMessageBox::warning(this, tr("点云配准失败"), result.error);
        return;
    }

    famp::cloud::CloudAttributes resultAttributes;
    QString attributeError;
    if (!source.cloud.layer.attributes.select(
            result.sourceIndices, resultAttributes, &attributeError))
    {
        QMessageBox::warning(this, tr("点云配准失败"), attributeError);
        return;
    }
    const famp::workspace::WorkspaceEntity* sourceEntity =
        workspaceStore->entity(entityIdForCloud(source.cloud));
    const famp::workspace::WorkspaceEntity* targetEntity =
        workspaceStore->entity(entityIdForCloud(target.cloud));
    if (!sourceEntity || !targetEntity)
        return;
    famp::workspace::Provenance provenance;
    provenance.operation = QStringLiteral("icp_registration");
    provenance.sourceIds = {sourceEntity->id, targetEntity->id};
    provenance.sourceSnapshot = QJsonObject{
        {QStringLiteral("source"), sourceSnapshot(*sourceEntity, source.cloud)},
        {QStringLiteral("target"), sourceSnapshot(*targetEntity, target.cloud)}};
    provenance.parameters = QJsonObject{
        {QStringLiteral("maximumIterations"), options.maximumIterations},
        {QStringLiteral("maximumCorrespondenceDistance"),
         options.maximumCorrespondenceDistance},
        {QStringLiteral("samplingVoxelSizeMeters"),
         options.samplingVoxelSizeMeters},
        {QStringLiteral("transformationEpsilon"),
         options.transformationEpsilon},
        {QStringLiteral("fitnessEpsilon"), options.fitnessEpsilon},
        {QStringLiteral("minimumOverlapRatio"), options.minimumOverlapRatio}};
    provenance.metrics = QJsonObject{
        {QStringLiteral("fitnessScore"), result.fitnessScore},
        {QStringLiteral("sourceOverlapRatio"), result.sourceOverlapRatio},
        {QStringLiteral("targetOverlapRatio"), result.targetOverlapRatio},
        {QStringLiteral("overlapRatio"), result.overlapRatio},
        {QStringLiteral("overlappingSourcePointCount"),
         static_cast<double>(result.overlappingSourcePointCount)},
        {QStringLiteral("overlappingTargetPointCount"),
         static_cast<double>(result.overlappingTargetPointCount)},
        {QStringLiteral("registrationSourcePointCount"),
         static_cast<double>(result.registrationSourcePointCount)},
        {QStringLiteral("registrationTargetPointCount"),
         static_cast<double>(result.registrationTargetPointCount)}};
    const Eigen::Matrix4d icpTransform = result.transform.cast<double>();
    provenance.transforms = {
        workspaceMatrix(result.sourceToTargetFrame),
        workspaceMatrix(icpTransform),
        workspaceMatrix(result.combinedTransform)};
    if (!integrateDerivedCloud(
            source.cloud, resultName, result.cloud,
            target.cloud.layer.spatial, targetCrsInfo.identifier,
            resultAttributes, std::move(provenance)))
    {
        return;
    }
    QStringList matrixRows;
    for (int row = 0; row < 4; ++row)
    {
        matrixRows << QStringLiteral("[%1 %2 %3 %4]")
            .arg(result.transform(row, 0), 0, 'g', 9)
            .arg(result.transform(row, 1), 0, 'g', 9)
            .arg(result.transform(row, 2), 0, 'g', 9)
            .arg(result.transform(row, 3), 0, 'g', 9);
    }
    emit sendStr2Console(
        tr("ICP 配准完成：适应度 %1，双向重叠率 %2%（源 %3%，目标 %4%），"
           "配准采样 %5/%6 点，成果保留在内存中\nICP 变换矩阵：\n%7")
            .arg(result.fitnessScore, 0, 'g', 10)
            .arg(result.overlapRatio * 100.0, 0, 'f', 1)
            .arg(result.sourceOverlapRatio * 100.0, 0, 'f', 1)
            .arg(result.targetOverlapRatio * 100.0, 0, 'f', 1)
            .arg(result.registrationSourcePointCount)
            .arg(result.registrationTargetPointCount)
            .arg(matrixRows.join(QLatin1Char('\n'))));
}

void MainWindow::slotIntegratePlaneClip(
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr points,
    QVector<qint64> sourceIndices,
    QVector3D planeOrigin,
    QVector3D planeNormal,
    double threshold)
{
    const famp::workspace::WorkspaceEntity* sourceEntity =
        workspaceStore->entity(activeVtkSourceId);
    if (!sourceEntity
        || sourceEntity->kind != famp::workspace::EntityKind::PointCloud)
    {
        QMessageBox::warning(
            this, tr("平面裁切"), tr("无法确定裁切的来源点云。"));
        return;
    }
    const std::shared_ptr<MyCloudList> source =
        sourceEntity->payloadAs<MyCloudList>();
    if (!source || sourceEntity->locked)
    {
        QMessageBox::warning(
            this, tr("平面裁切"), tr("来源点云不可用或已锁定。"));
        return;
    }

    famp::cloud::CloudAttributes attributes;
    QString error;
    if (!source->layer.attributes.select(
            sourceIndices, attributes, &error))
    {
        QMessageBox::warning(this, tr("平面裁切失败"), error);
        return;
    }

    famp::workspace::Provenance provenance;
    provenance.operation = QStringLiteral("plane_clip");
    provenance.sourceIds = {sourceEntity->id};
    provenance.sourceSnapshot = sourceSnapshot(*sourceEntity, *source);
    provenance.parameters = QJsonObject{
        {QStringLiteral("originX"), planeOrigin.x()},
        {QStringLiteral("originY"), planeOrigin.y()},
        {QStringLiteral("originZ"), planeOrigin.z()},
        {QStringLiteral("normalX"), planeNormal.x()},
        {QStringLiteral("normalY"), planeNormal.y()},
        {QStringLiteral("normalZ"), planeNormal.z()},
        {QStringLiteral("distanceThresholdMeters"), threshold}};
    provenance.metrics = QJsonObject{
        {QStringLiteral("outputPointCount"),
         static_cast<double>(points ? points->size() : 0)}};

    famp::workspace::EntityId resultId;
    if (!integrateDerivedCloud(
            *source,
            derivedEntityName(sourceEntity->name, QStringLiteral("_plane_clip")),
            points, source->layer.spatial, source->layer.crs, attributes,
            std::move(provenance), false, &resultId))
    {
        return;
    }
    activeVtkSourceId = resultId;
    synchronizeProjectionWorkflowFromSelection();
    emit sendStr2Console(
        tr("平面裁切成果已加入内容列表：%1 个点，未自动落盘")
            .arg(points->size()));
}

void MainWindow::slotHandleProjectedCloudPreview(
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr points,
    famp::projection::Plane plane)
{
    // MyVTK has already rendered the newly requested preview before emitting
    // this signal. Close an older decision window without erasing that new
    // actor; the new window will own the preview lifetime from here on.
    closeProjectionDecisionDialog(false);
    const famp::workspace::WorkspaceEntity* sourceEntity =
        workspaceStore->entity(activeVtkSourceId);
    if (!sourceEntity
        || sourceEntity->kind != famp::workspace::EntityKind::PointCloud)
    {
        clearTransientProjectionPreview();
        QMessageBox::warning(
            this, tr("投影预览"), tr("无法确定投影的来源点云。"));
        return;
    }
    const std::shared_ptr<MyCloudList> source =
        sourceEntity->payloadAs<MyCloudList>();
    if (!source || !points || points->empty()
        || points->size() != source->layer.pointCount())
    {
        clearTransientProjectionPreview();
        QMessageBox::warning(
            this, tr("投影预览"),
            tr("投影预览与来源点云的点数不一致。"));
        return;
    }

    QString error;
    if (!projectionWorkflow.selectSource(
            sourceEntity->id, sourceEntity->name,
            source->layer.points, &error)
        || !projectionWorkflow.setPreview(points, plane, &error)
        || !ui.graphicsView->setProjectionInput(points, plane, &error))
    {
        clearTransientProjectionPreview();
        QMessageBox::warning(this, tr("投影预览"), error);
        return;
    }
    updateProjectionActions();
    updateArchaeologyWorkflowGuide();

    emit sendStr2Console(
        tr("%1 投影预览已就绪：%2 个点，未加入内容列表，未落盘。")
            .arg(famp::projection::displayName(plane))
            .arg(points->size()));

    auto* decision = new QMessageBox(this);
    decision->setObjectName(QStringLiteral("projectionCommitPrompt"));
    decision->setAttribute(Qt::WA_DeleteOnClose);
    decision->setWindowModality(Qt::NonModal);
    decision->setModal(false);
    decision->setWindowFlag(Qt::WindowStaysOnTopHint, false);
    decision->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    decision->setIcon(QMessageBox::Question);
    decision->setWindowTitle(tr("投影预览已生成"));
    decision->setText(
        tr("来源：%1\n预览：%2\n点数：%3")
            .arg(sourceEntity->name,
                 famp::projection::displayName(plane))
            .arg(points->size()));
    const bool planReady = ui.graphicsView->hasProjectionDrawing(
        famp::projection::Plane::Overlook);
    const bool orderedPlane = !isArchaeologyProfilePlane(plane)
        || planReady;
    if (!planReady)
    {
        decision->setInformativeText(
            tr("请先完成【俯视投影二维制图】，再生成 XOZ/YOZ 剖面图。"));
        decision->setIcon(orderedPlane
                              ? QMessageBox::Information
                              : QMessageBox::Warning);
        if (QLabel* orderNotice = decision->findChild<QLabel*>(
                QStringLiteral("qt_msgbox_informativelabel")))
        {
            orderNotice->setObjectName(
                QStringLiteral("projectionPlanFirstNotice"));
            orderNotice->setWordWrap(true);
            orderNotice->setStyleSheet(QStringLiteral(
                "QLabel{color:#9a3412;background:#fff7ed;"
                "border:2px solid #f97316;border-radius:5px;"
                "padding:8px 10px;font-weight:800;}"));
        }
    }
    QPushButton* drawButton = decision->addButton(
        tr("自动绘图"), QMessageBox::ActionRole);
    drawButton->setObjectName(QStringLiteral("projectionAutoDrawButton"));
    drawButton->setEnabled(points->size() >= 10 && orderedPlane);
    QString drawToolTip = tr(
        "把当前投影生成到右侧考古制图画布。俯视、XOZ、YOZ 均会先打开 "
        "0°/90°/180°/270° 方向预览，只有确认后才生成正式线图。");
    if (!orderedPlane)
    {
        drawToolTip += tr(
            " 当前不可用：请先完成俯视投影二维制图，再生成并对齐 XOZ/YOZ 剖面图。");
    }
    drawButton->setToolTip(drawToolTip);
    drawButton->setWhatsThis(drawToolTip);
    if (!planReady && plane == famp::projection::Plane::Overlook)
    {
        drawButton->setStyleSheet(QStringLiteral(
            "QPushButton{color:white;background:#ea580c;"
            "border:2px solid #c2410c;border-radius:4px;"
            "padding:5px 12px;font-weight:800;}"
            "QPushButton:hover{background:#c2410c;}"));
    }
    QPushButton* addButton = decision->addButton(
        tr("加入内容列表"), QMessageBox::AcceptRole);
    addButton->setObjectName(QStringLiteral("projectionAddToTreeButton"));
    const QString addToolTip = tr(
        "仅把当前投影保留为左侧内容列表中的内存点云；不会生成右侧考古线图，"
        "也不会自动另存落盘。");
    addButton->setToolTip(addToolTip);
    addButton->setWhatsThis(addToolTip);
    QPushButton* closePreviewButton = decision->addButton(
        tr("关闭预览"), QMessageBox::RejectRole);
    closePreviewButton->setObjectName(
        QStringLiteral("projectionClosePreviewButton"));
    const QString closeToolTip = tr(
        "关闭当前预览，不新建点云或二维成果；中央 VTK 中的临时投影会自动消失。");
    closePreviewButton->setToolTip(closeToolTip);
    closePreviewButton->setWhatsThis(closeToolTip);
    decision->setDefaultButton(closePreviewButton);
    decision->setEscapeButton(closePreviewButton);
    projectionDecisionDialog = decision;

    connect(drawButton, &QPushButton::clicked, this, [this]() {
        if (projectionWorkflow.hasPreview())
            slotAutoDrawProjection();
        updateProjectionActions();
        updateArchaeologyWorkflowGuide();
    });
    connect(addButton, &QPushButton::clicked, this, [this]() {
        integrateProjectionPreview();
        updateProjectionActions();
        updateArchaeologyWorkflowGuide();
    });
    connect(closePreviewButton, &QPushButton::clicked,
            this, [this, plane]() {
        statusBar()->showMessage(
            tr("已关闭 %1 投影预览，未新建内容列表实体。")
                .arg(famp::projection::displayName(plane)),
            7000);
    });
    connect(decision, &QMessageBox::finished,
            this, [this, decision](int) {
                lastProjectionDecisionPosition = decision->pos();
                if (projectionDecisionDialog != decision)
                    return;
                projectionDecisionDialog.clear();

                // QMessageBox emits finished while processing the clicked
                // button. Let the corresponding auto-draw/add-to-tree slot
                // consume the preview first, then retire the transient state.
                // If another projection window opens meanwhile, it owns the
                // newly rendered preview and must not be cleared here.
                QTimer::singleShot(0, this, [this]() {
                    if (!projectionDecisionDialog)
                        clearTransientProjectionPreview();
                });
            });
    decision->ensurePolished();
    decision->adjustSize();
    decision->move(projectionDecisionPopupPosition(decision->size()));
    decision->show();
}

void MainWindow::slotAutoDrawProjection()
{
    const famp::projection::Preview* preview = projectionWorkflow.preview();
    if (!preview || !preview->points || preview->points->size() < 10
        || !ui.graphicsView->hasProjectionInput())
    {
        QMessageBox::warning(
            this, tr("自动绘图"),
            tr("请先生成至少 10 个点的有效投影预览。"));
        return;
    }
    const famp::projection::Preview previewSnapshot = *preview;

    if (isArchaeologyProfilePlane(previewSnapshot.plane)
        && !ui.graphicsView->hasProjectionDrawing(
            famp::projection::Plane::Overlook))
    {
        QMessageBox::information(
            this, tr("自动绘图顺序"),
            tr("请先生成俯视投影二维制图，再生成 XOZ/YOZ 剖面图。"
               "系统需要用俯视图确定剖面切割线和对齐位置。"));
        return;
    }

    if (isRequiredArchaeologyPlane(previewSnapshot.plane))
    {
        if (!ui.graphicsView->confirmProjectionRotation(this))
        {
            statusBar()->showMessage(
                tr("已取消 %1 方向确认，投影预览未生成正式二维成果。")
                    .arg(archaeologyPlaneName(previewSnapshot.plane)),
                7000);
            return;
        }
        emit sendStr2Console(
            tr("%1 方向已确认：顺时针 %2°；确认后按俯视剖面切割线自动对齐。")
                .arg(
                    archaeologyPlaneName(previewSnapshot.plane))
                .arg(ui.graphicsView->projectionRotationDegrees(
                    previewSnapshot.plane)));
    }

    // Clicking a QMessageBox action closes the projection decision window.
    // Each required-direction dialog runs a nested event loop, so closing the
    // decision box can retire the transient input before the user confirms.
    // Restore the copied in-memory preview solely for the drawing operation;
    // the VTK preview remains correctly closed and no cloud entity is created.
    if (!ui.graphicsView->hasProjectionInput())
    {
        QString error;
        if (!projectionWorkflow.setPreview(
                previewSnapshot.points, previewSnapshot.plane, &error)
            || !ui.graphicsView->setProjectionInput(
                previewSnapshot.points, previewSnapshot.plane, &error))
        {
            QMessageBox::warning(this, tr("自动绘图"), error);
            return;
        }
    }
    ui.graphicsView->getDBItemCloud(previewSnapshot.source.points);
    const famp::workspace::WorkspaceEntity* drawingSource =
        workspaceStore->entity(previewSnapshot.source.entityId);
    if (drawingSource && drawingSource->provenance.has_value()
        && drawingSource->provenance->operation
            == QStringLiteral("plane_clip"))
    {
        const QJsonObject& parameters =
            drawingSource->provenance->parameters;
        const QVector3D origin(
            static_cast<float>(parameters.value(
                QStringLiteral("originX")).toDouble()),
            static_cast<float>(parameters.value(
                QStringLiteral("originY")).toDouble()),
            static_cast<float>(parameters.value(
                QStringLiteral("originZ")).toDouble()));
        const QVector3D normal(
            static_cast<float>(parameters.value(
                QStringLiteral("normalX")).toDouble()),
            static_cast<float>(parameters.value(
                QStringLiteral("normalY")).toDouble()),
            static_cast<float>(parameters.value(
                QStringLiteral("normalZ")).toDouble()));
        ui.graphicsView->setSectionPlaneReference(origin, normal);
    }
    ui.graphicsView->slotOn_actProjLine_triggered();
    updateProjectionActions();
    updateArchaeologyWorkflowGuide();
}

bool MainWindow::integrateProjectionPreview()
{
    const famp::projection::Preview* currentPreview =
        projectionWorkflow.preview();
    if (!currentPreview)
        return false;
    const famp::projection::Preview preview = *currentPreview;
    const famp::workspace::WorkspaceEntity* sourceEntity =
        workspaceStore->entity(preview.source.entityId);
    if (!sourceEntity
        || sourceEntity->kind != famp::workspace::EntityKind::PointCloud)
    {
        QMessageBox::warning(
            this, tr("加入投影点云"),
            tr("投影预览的来源点云已不存在。"));
        return false;
    }
    const std::shared_ptr<MyCloudList> source =
        sourceEntity->payloadAs<MyCloudList>();
    if (!source || !preview.points || preview.points->empty())
        return false;

    const QString planeName = famp::projection::axisName(preview.plane);
    const bool overlook = famp::projection::isOverlook(preview.plane);
    famp::workspace::Provenance provenance;
    provenance.operation = overlook
        ? QStringLiteral("overlook_projection")
        : QStringLiteral("plane_projection");
    provenance.sourceIds = {sourceEntity->id};
    provenance.sourceSnapshot = sourceSnapshot(*sourceEntity, *source);
    provenance.parameters = QJsonObject{
        {QStringLiteral("plane"), planeName},
        {QStringLiteral("overlook"), overlook}};
    provenance.metrics = QJsonObject{
        {QStringLiteral("outputPointCount"),
         static_cast<double>(preview.points->size())}};

    famp::workspace::EntityId resultId;
    const QString suffix = overlook
        ? QStringLiteral("_projected_overlook_XOY")
        : QStringLiteral("_projected_") + planeName;
    if (!integrateDerivedCloud(
            *source, derivedEntityName(sourceEntity->name, suffix),
            preview.points, source->layer.spatial, source->layer.crs,
            source->layer.attributes, std::move(provenance), false,
            &resultId))
    {
        return false;
    }

    const famp::workspace::WorkspaceEntity* added =
        workspaceStore->entity(resultId);
    const std::shared_ptr<MyCloudList> addedCloud = added
        ? added->payloadAs<MyCloudList>() : std::shared_ptr<MyCloudList>();
    if (added && addedCloud)
    {
        activeVtkSourceId = resultId;
        projectionWorkflow.selectSource(
            resultId, added->name, addedCloud->layer.points, nullptr);
        projectionWorkflow.setPreview(
            addedCloud->layer.points, preview.plane, nullptr);
        ui.graphicsView->setProjectionInput(
            addedCloud->layer.points, preview.plane, nullptr);
        myVTK->getDBItemCloud(
            addedCloud->layer.points,
            addedCloud->layer.display.pointSize);
    }
    emit sendStr2Console(
        tr("%1 投影点云已紧跟来源点云加入内容列表；来源仍保持显示，未落盘。")
            .arg(famp::projection::displayName(preview.plane)));
    updateProjectionActions();
    updateArchaeologyWorkflowGuide();
    return true;
}

void MainWindow::initializeRecentFilesMenu()
{
    recentFilesMenu = new QMenu(tr("最近打开"), ui.menu_4);
    ui.menu_4->insertMenu(ui.actSave, recentFilesMenu);
    ui.menu_4->insertSeparator(ui.actSave);

    QSettings settings;
    const QStringList storedFiles = settings.value(
        QStringLiteral("MainWindow/recentCloudFiles")).toStringList();
    recentFiles = famp::recent::availableFiles(storedFiles);
    if (recentFiles != storedFiles)
        settings.setValue(QStringLiteral("MainWindow/recentCloudFiles"), recentFiles);
    updateRecentFilesMenu();
}

void MainWindow::addRecentFile(const QString& path)
{
    recentFiles = famp::recent::updatedFiles(recentFiles, path);
    QSettings settings;
    settings.setValue(QStringLiteral("MainWindow/recentCloudFiles"), recentFiles);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu()
{
    recentFilesMenu->clear();
    if (recentFiles.isEmpty())
    {
        QAction* emptyAction = recentFilesMenu->addAction(tr("暂无最近文件"));
        emptyAction->setEnabled(false);
        return;
    }

    for (int index = 0; index < recentFiles.size(); ++index)
    {
        const QString path = recentFiles.at(index);
        QString displayPath = QDir::toNativeSeparators(path);
        displayPath.replace(QLatin1Char('&'), QStringLiteral("&&"));
        displayPath = QFontMetrics(recentFilesMenu->font()).elidedText(
            displayPath, Qt::ElideMiddle, 520);
        QAction* action = recentFilesMenu->addAction(
            QStringLiteral("&%1 %2").arg(index + 1).arg(displayPath));
        action->setData(path);
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, action]() {
            const QString selectedPath = action->data().toString();
            const bool fileIsMissing = !QFileInfo::exists(selectedPath);
            openCloudFile(selectedPath);
            if (fileIsMissing)
            {
                recentFiles = famp::recent::availableFiles(recentFiles);
                QSettings settings;
                settings.setValue(QStringLiteral("MainWindow/recentCloudFiles"), recentFiles);
                QTimer::singleShot(0, this, [this]() { updateRecentFilesMenu(); });
            }
        });
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls())
        return;

    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile()
            && famp::recent::isSupportedCloudFile(url.toLocalFile()))
        {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (cloudLoadBusy)
    {
        statusBar()->showMessage(tr("请等待当前点云加载完成"), 5000);
        return;
    }

    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (!url.isLocalFile())
            continue;

        const QString path = famp::recent::normalizedPath(url.toLocalFile());
        if (famp::recent::isSupportedCloudFile(path) && !paths.contains(path))
            paths.append(path);
    }

    if (paths.isEmpty())
        return;

    event->acceptProposedAction();
    if (beginCloudLoadBatch(paths))
    {
        emit sendStr2Console(tr("已将 %1 个拖放点云加入后台加载队列")
                                 .arg(paths.size()));
    }
}

void MainWindow::showHelpDialog(const QString& title, const QString& html)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(680, 520);

    QVBoxLayout layout(&dialog);
    QTextBrowser browser(&dialog);
    browser.setOpenExternalLinks(true);
    browser.setHtml(html);
    layout.addWidget(&browser);

    QDialogButtonBox buttons(QDialogButtonBox::Close, &dialog);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout.addWidget(&buttons);
    dialog.exec();
}

void MainWindow::slotShowQuickStart()
{
    showHelpDialog(tr("FAMP 快速入门"), famp::help::quickStartHtml());
}

void MainWindow::slotShowShortcuts()
{
    showHelpDialog(tr("FAMP 快捷键"), famp::help::shortcutsHtml());
}

void MainWindow::slotShowAbout()
{
    const QString version = QCoreApplication::applicationVersion().isEmpty()
        ? tr("开发版本")
        : QCoreApplication::applicationVersion();
    showHelpDialog(
        tr("关于 FAMP"),
        famp::help::aboutHtml(
            version,
            QString::fromLatin1(qVersion()),
            QString::fromLatin1(VTK_VERSION),
            QString::fromLatin1(PCL_VERSION_PRETTY),
            famp::crs::runtimeVersion()));
}

void MainWindow::slotOn_actGraViewVisible_triggered(bool checked)
{
    ui.dockWidget2->setVisible(checked);
}

//添加XOY标签
void MainWindow::addXOYLabel()
{
    xoy_label = new QLabel(this);
    QImage img;
    img.load(":/images/images/xymap.bmp");
    xoy_label->setPixmap(QPixmap::fromImage(img));
    layout = new QHBoxLayout(ui.graphicsView);

    //设置布局
    layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(xoy_label);
    setXOYLabelVisible(true);
}

void MainWindow::setXOYLabelVisible(bool enable)
{
    this->xoy_label->setVisible(enable);
}

//添加比例尺
void MainWindow::addScaleWidget()
{
    scaleCombox = new QComboBox(this);
    QStringList scaleList;
    scaleList << "1:10" << "1:20" << "1:50" << "1:100";
    scaleCombox->addItems(scaleList);
    scaleCombox->setCurrentIndex(2);
    scaleCombox->setCursor(Qt::ArrowCursor);

    scaleLabel = new QLabel("比例尺：");
    scaleLabel->adjustSize();

    //设置布局
    //layout->setAlignment(Qt::AlignRight | Qt::AlignTop);
    layout->addWidget(scaleLabel);
    layout->addWidget(scaleCombox);

}

//设置比例尺的可见性
void MainWindow::setScaleVisible(bool enable)
{
    this->scaleCombox->setVisible(enable);
    this->scaleLabel->setVisible(enable);
}

void MainWindow::slotOn_actGraViewVisible_visibilityChanged(bool visible)
{
    ui.acGraViewVisible->setChecked(visible);
}

void MainWindow::slotOn_actGraViewFloat_triggered(bool checked)
{
    ui.dockWidget2->setFloating(checked);
}

void MainWindow::slotOn_actGraViewFloat_topLevelChanged(bool topLevel)
{
    ui.actGRaViewFloat->setChecked(topLevel);
}

void MainWindow::slotOn_actVTKVisible_triggered(bool checked)
{
    centerDock->setVisible(checked);
}

void MainWindow::slotOn_actVTKViewVisible_visibilityChanged(bool visible)
{
    ui.actVTKVisible->setChecked(visible);
}

void MainWindow::slotOn_actConsoleVisible_triggered(bool checked)
{
    ui.dockWidget3->setVisible(checked);
}

void MainWindow::slotOn_actConsoleVisible_visibilityChanged(bool visible)
{
    ui.actConsoleVisible->setChecked(visible);
}

void MainWindow::slotOn_actConsoleFloat_triggered(bool checked)
{
    ui.dockWidget3->setFloating(checked);
}

void MainWindow::slotOn_actConsoleFloat_topLevelChanged(bool topLevel)
{
    ui.actConsoleFloat->setChecked(topLevel);
}

void MainWindow::slotOn_actDBTreeVisible_triggered(bool checked)
{
    ui.dockWidget1->setVisible(checked);
}

void MainWindow::slotOn_actDBTreeVisible_visibilityChanged(bool visible)
{
    ui.actDBTreeVisible->setChecked(visible);
}

void MainWindow::slotOn_actDBTreeFloat_triggered(bool checked)
{
    ui.dockWidget1->setFloating(checked);
}

void MainWindow::slotOn_actDBTreeFloat_topLevelChanged(bool topLevel)
{
    ui.actDBTreeFloat->setChecked(topLevel);
}

void MainWindow::slotFullScreen()
{
    this->setWindowState(Qt::WindowMaximized);
}

void MainWindow::slotFrontView()
{
    myVTK->setFrontView();
    emit sendStr2Console("前视图");
}

void MainWindow::slotTopView()
{
    myVTK->setTopView();
    emit sendStr2Console("顶视图");
}

void MainWindow::slotBottomView()
{
    myVTK->setBottomView();
    emit sendStr2Console("底视图");
}

void MainWindow::slotLeftView()
{
    myVTK->setLeftView();
    emit sendStr2Console("左视图");
}

void MainWindow::slotRightView()
{
    myVTK->setRightView();
    emit sendStr2Console("右视图");
}

void MainWindow::slotBackView()
{
    myVTK->setBackView();
    emit sendStr2Console("后视图");
}

//打开文件
void MainWindow::slotOpenCloud()
{
    const QString initialDirectory = recentFiles.isEmpty()
        ? QCoreApplication::applicationDirPath()
        : QFileInfo(recentFiles.front()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("打开点云文件"),
        initialDirectory,
        tr("点云文件 (*.pcd *.las *.laz *.ply *.xyz);;PCD (*.pcd);;LAS/LAZ (*.las *.laz);;PLY (*.ply);;XYZ (*.xyz)"));
    if (!path.isEmpty())
        openCloudFile(path);
}

bool MainWindow::openCloudFile(const QString& requestedPath)
{
    if (cloudLoadBusy)
    {
        statusBar()->showMessage(tr("请等待当前点云加载完成"), 5000);
        return false;
    }
    return beginCloudLoadBatch(QStringList{requestedPath});
}

bool MainWindow::beginCloudLoadBatch(const QStringList& paths,
                                     bool projectBatch,
                                     const QString& projectPath,
                                     bool projectRecovery)
{
    if (cloudLoadBusy)
        return false;

    pendingCloudFiles.clear();
    cloudLoadFailurePaths.clear();
    cloudLoadFailureMessages.clear();
    currentCloudLoadPath.clear();
    cloudLoadProjectPath = projectPath;
    cloudLoadTotal = paths.size();
    cloudLoadCompleted = 0;
    cloudLoadSucceeded = 0;
    cloudLoadProjectBatch = projectBatch;
    cloudLoadProjectRecovery = projectRecovery;
    cloudLoadCancelled = false;
    cloudLoadTask = taskManager->start(
        projectBatch ? tr("加载项目点云") : tr("加载点云"),
        tr("正在检查输入文件"));
    if (!cloudLoadTask.isValid())
        return false;

    for (const QString& requestedPath : paths)
    {
        QString normalizedPath;
        QString error;
        if (!famp::cloud::validatePath(requestedPath, &normalizedPath, &error))
        {
            ++cloudLoadCompleted;
            cloudLoadFailurePaths.append(normalizedPath);
            cloudLoadFailureMessages.append(error);
            continue;
        }
        if (!pendingCloudFiles.contains(normalizedPath))
            pendingCloudFiles.append(normalizedPath);
        else
            --cloudLoadTotal;
    }

    cloudLoadBusy = true;
    setCloudLoadUiBusy(true);
    cloudLoadProgress->setRange(0, std::max(1, cloudLoadTotal));
    cloudLoadProgress->setValue(cloudLoadCompleted);
    taskManager->setProgress(
        cloudLoadTask.id,
        cloudLoadTotal > 0
            ? static_cast<double>(cloudLoadCompleted) / cloudLoadTotal : 0.0,
        tr("等待加载 %1 个点云").arg(pendingCloudFiles.size()));

    if (pendingCloudFiles.isEmpty())
        finishCloudLoadBatch();
    else
        startNextCloudLoad();

    return projectBatch || cloudLoadTotal > cloudLoadCompleted;
}

void MainWindow::startNextCloudLoad()
{
    if (!cloudLoadBusy || pendingCloudFiles.isEmpty())
    {
        finishCloudLoadBatch();
        return;
    }

    currentCloudLoadPath = pendingCloudFiles.takeFirst();
    statusBar()->showMessage(
        tr("正在后台加载点云 %1/%2：%3")
            .arg(cloudLoadCompleted + 1)
            .arg(cloudLoadTotal)
            .arg(QFileInfo(currentCloudLoadPath).fileName()));
    emit sendStr2Console(tr("后台读取点云  %1").arg(currentCloudLoadPath));

    const QString path = currentCloudLoadPath;
    const auto shouldCancel = cloudLoadTask.cancellationCheck();
    cloudLoadWatcher->setFuture(QtConcurrent::run([path, shouldCancel]() {
        return famp::cloud::load(path, shouldCancel);
    }));
}

void MainWindow::slotCloudLoadFinished()
{
    if (!cloudLoadBusy)
        return;

    const famp::cloud::LoadResult result = cloudLoadWatcher->result();
    ++cloudLoadCompleted;
    if (result.succeeded())
    {
        integrateLoadedCloud(result);
        ++cloudLoadSucceeded;
    }
    else if (!result.cancelled)
    {
        cloudLoadFailurePaths.append(
            result.path.isEmpty() ? currentCloudLoadPath : result.path);
        cloudLoadFailureMessages.append(result.error);
        emit sendStr2Console(tr("点云加载失败  %1").arg(result.error));
    }
    cloudLoadProgress->setValue(cloudLoadCompleted);
    if (cloudLoadTask.isValid())
    {
        taskManager->setProgress(
            cloudLoadTask.id,
            cloudLoadTotal > 0
                ? std::min(1.0,
                           static_cast<double>(cloudLoadCompleted)
                               / cloudLoadTotal)
                : 1.0,
            tr("已处理 %1/%2 个点云")
                .arg(cloudLoadCompleted)
                .arg(cloudLoadTotal));
    }

    if (pendingCloudFiles.isEmpty())
        finishCloudLoadBatch();
    else
        QTimer::singleShot(0, this, [this]() { startNextCloudLoad(); });
}

void MainWindow::integrateLoadedCloud(const famp::cloud::LoadResult& result)
{
    inCloud = result.displayCloud;

    if (result.sourceWasPcd && result.sourceCloud)
    {
        delete myCloud;
        myCloud = new Cloud(result.sourceCloud);
    }

    emit sendOrignalCloud(inCloud);

    //pcl::io::savePCDFileASCII("inCloud.pcd", *inCloud);

    //存储在DB Tree 的data中
    MyCloudList pointCloud;

    //添加点云演员
    vtkActor * cloud_actor = myVTK->appendCloudActor();

    //添AABB云演员
    vtkActor * AABB_actor = myVTK->appendAABBActor();

    //将各种数据储存到自定义结构体中
    pointCloud.layer = famp::cloud::makeLayer(
        result.path, inCloud, result.spatial, result.sourceCloud);
    pointCloud.layer.attributes = result.attributes;
    pointCloud.layer.crs = projectCrs;
    pointCloud.id = iCount;
    pointCloud.cloudactor = cloud_actor;
    pointCloud.AABBactor = AABB_actor;
    bool cloudVisible = true;
    const auto storedReference = projectCloudReferences.constFind(result.path);
    if (storedReference != projectCloudReferences.cend())
    {
        pointCloud.layer.id = storedReference->layerId;
        pointCloud.layer.name = storedReference->name;
        pointCloud.layer.crs = storedReference->crs;
        pointCloud.layer.spatial = storedReference->spatial;
        pointCloud.layer.display = storedReference->display;
        pointCloud.layer.archaeologyFields = storedReference->archaeologyFields;
        pointCloud.layer.controlPoints = storedReference->controlPoints;
        pointCloud.layer.locked = storedReference->locked;
        cloudVisible = storedReference->visible;
    }
    pointCloud.layer.visible = cloudVisible;
    vtkNew<vtkMatrix4x4> cloudTransform;
    setCenteredSpatialMatrix(cloudTransform, pointCloud.layer.spatial);
    pointCloud.cloudactor->SetUserMatrix(cloudTransform);
    pointCloud.AABBactor->SetUserMatrix(cloudTransform);
    QString displayError;
    if (pointCloud.layer.display.colorMode == famp::display::ColorMode::Attribute
        && !famp::display::attachAttribute(
            pointCloud.cloudactor,
            pointCloud.layer.attributes,
            pointCloud.layer.display.attributeName,
            &displayError))
    {
        pointCloud.layer.display = {};
        emit sendStr2Console(
            tr("点云属性着色恢复失败，已改用默认显示：%1")
                .arg(displayError));
    }
    if (!famp::display::apply(
            pointCloud.cloudactor, pointCloud.layer.display, &displayError))
    {
        pointCloud.layer.display = {};
        famp::display::apply(
            pointCloud.cloudactor, pointCloud.layer.display, nullptr);
        emit sendStr2Console(
            tr("点云显示设置已恢复默认值：%1").arg(displayError));
    }
    QString actorMetadataError;
    if (!myVTK->setCloudActorMetadata(
            pointCloud.cloudactor,
            pointCloud.layer.id,
            pointCloud.layer.crs,
            &actorMetadataError))
    {
        emit sendStr2Console(
            tr("点云测量元数据初始化失败：%1")
                .arg(actorMetadataError));
    }
    pointCloudList.push_back(pointCloud);

    //发送给Console消息
    emit sendStr2Console(tr("打开点云  %1").arg(result.path));
    if (!pointCloud.layer.attributes.isEmpty())
    {
        emit sendStr2Console(
            tr("已加载 %1 个逐点属性：%2")
                .arg(pointCloud.layer.attributes.size())
                .arg(pointCloud.layer.attributes.names().join(
                    QStringLiteral(", "))));
    }

    /////-------------------Entity tree---------------------
    famp::workspace::WorkspaceEntity entity = famp::workspace::makeEntity(
        famp::workspace::EntityKind::PointCloud, pointCloud.layer.name);
    entity.id = QUuid(pointCloud.layer.id);
    entity.visible = cloudVisible;
    entity.locked = pointCloud.layer.locked;
    entity.dirty = true;
    if (!result.path.isEmpty())
        entity.assetPath = QFileInfo(result.path).absoluteFilePath();
    entity.setPayload(std::make_shared<MyCloudList>(pointCloudList.back()));
    QString workspaceError;
    const famp::workspace::EntityId entityId = workspaceStore->addEntity(
        entity, workspaceStore->rootId(), -1, &workspaceError);
    if (entityId.isNull())
    {
        const MyCloudList failedCloud = pointCloudList.back();
        removeCloudFromWorkspace(failedCloud);
        QMessageBox::warning(this, tr("添加点云失败"), workspaceError);
        return;
    }
    const QModelIndex entityIndex = model->indexForId(entityId);
    ui.treeView->expand(model->indexForId(workspaceStore->rootId()));
    ui.treeView->setCurrentIndex(entityIndex);

    if (cloudVisible)
        myVTK->display(pointCloudList.back().cloudactor);       //显示点云
    else
        myVTK->removeCloudDisplay(pointCloudList.back().cloudactor);
    //myVTK->display(pointCloudList.back().AABBactor);     //显示AABB
    myVTK->initCamera();
    myVTK->update();
    ++iCount;

    //设置AABB是否启用
    ui.actAABB->setEnabled(true);
    ui.actAABB->setChecked(false);
    ui.actAABB->setText(tr("包围盒"));
    ui.actAABB->setToolTip(tr("选择可见点云后显示其包围盒"));
    addRecentFile(result.path);
    markProjectDirty();
}

bool MainWindow::integrateDerivedCloud(
    const MyCloudList& source,
    const QString& requestedName,
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& points,
    const famp::cloud::SpatialReference& spatial,
    const QString& crs,
    const famp::cloud::CloudAttributes& attributes,
    famp::workspace::Provenance provenance,
    bool hideSource,
    famp::workspace::EntityId* resultId)
{
    const famp::workspace::EntityId sourceId = entityIdForCloud(source);
    const famp::workspace::WorkspaceEntity* sourceEntity =
        workspaceStore->entity(sourceId);
    if (!sourceEntity
        || sourceEntity->kind != famp::workspace::EntityKind::PointCloud)
    {
        QMessageBox::warning(
            this, tr("生成点云成果"), tr("来源点云实体已不存在。"));
        return false;
    }
    if (!points || points->empty())
    {
        QMessageBox::warning(
            this, tr("生成点云成果"), tr("算法返回了空点云。"));
        return false;
    }

    const famp::workspace::EntityId parentId = sourceEntity->parentId;
    const QString name = workspaceStore->uniqueSiblingName(
        parentId, requestedName);
    MyCloudList derived;
    derived.id = iCount;
    derived.layer = famp::cloud::makeLayer(QString(), points, spatial);
    derived.layer.name = name;
    derived.layer.crs = crs.trimmed();
    derived.layer.attributes = attributes;
    derived.layer.display = source.layer.display;
    const QString operation = provenance.operation.trimmed();
    if (operation == QStringLiteral("plane_clip")
        || operation == QStringLiteral("range_crop"))
    {
        // Keep the original per-point RGB data, but make a visible crop
        // result immediately distinguishable from its source layer.
        derived.layer.display.colorMode = famp::display::ColorMode::Uniform;
        derived.layer.display.red = 1.0;
        derived.layer.display.green = 0.58;
        derived.layer.display.blue = 0.0;
        derived.layer.display.pointSize = std::clamp(
            source.layer.display.pointSize + 2.0, 4.0, 20.0);
        derived.layer.display.attributeName.clear();
    }
    else if (operation == QStringLiteral("plane_projection")
             || operation == QStringLiteral("overlook_projection"))
    {
        derived.layer.display.colorMode = famp::display::ColorMode::Uniform;
        derived.layer.display.red = 1.0;
        derived.layer.display.green = 0.15;
        derived.layer.display.blue = 0.65;
        derived.layer.display.pointSize = std::clamp(
            source.layer.display.pointSize + 2.0, 4.0, 20.0);
        derived.layer.display.attributeName.clear();
    }
    derived.layer.archaeologyFields = source.layer.archaeologyFields;
    derived.layer.visible = true;
    derived.layer.locked = false;

    QString validationError;
    if (!famp::cloud::validateLayer(derived.layer, true, &validationError))
    {
        QMessageBox::warning(this, tr("生成点云成果"), validationError);
        return false;
    }

    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr previousCloud = inCloud;
    inCloud = points;
    emit sendOrignalCloud(inCloud);
    derived.cloudactor = myVTK->appendCloudActor();
    derived.AABBactor = myVTK->appendAABBActor();
    auto rollbackActors = [this, &derived, &previousCloud]() {
        if (derived.cloudactor)
        {
            myVTK->unregisterCloudActor(derived.cloudactor);
            derived.cloudactor->Delete();
            derived.cloudactor = nullptr;
        }
        if (derived.AABBactor)
        {
            myVTK->removeAABBDisplay(derived.AABBactor);
            derived.AABBactor->Delete();
            derived.AABBactor = nullptr;
        }
        inCloud = previousCloud;
        emit sendOrignalCloud(inCloud);
    };
    if (!derived.cloudactor || !derived.AABBactor)
    {
        rollbackActors();
        QMessageBox::warning(
            this, tr("生成点云成果"), tr("无法创建点云渲染对象。"));
        return false;
    }

    vtkNew<vtkMatrix4x4> cloudTransform;
    setCenteredSpatialMatrix(cloudTransform, derived.layer.spatial);
    derived.cloudactor->SetUserMatrix(cloudTransform);
    derived.AABBactor->SetUserMatrix(cloudTransform);
    if (derived.layer.display.colorMode
            == famp::display::ColorMode::Attribute
        && !famp::display::attachAttribute(
            derived.cloudactor, derived.layer.attributes,
            derived.layer.display.attributeName, &validationError))
    {
        derived.layer.display = {};
        emit sendStr2Console(
            tr("派生点云的属性着色无法继承，已使用默认显示：%1")
                .arg(validationError));
    }
    if (!famp::display::apply(
            derived.cloudactor, derived.layer.display, &validationError))
    {
        derived.layer.display = {};
        famp::display::apply(
            derived.cloudactor, derived.layer.display, nullptr);
    }
    if (!myVTK->setCloudActorMetadata(
            derived.cloudactor, derived.layer.id, derived.layer.crs,
            &validationError))
    {
        rollbackActors();
        QMessageBox::warning(this, tr("生成点云成果"), validationError);
        return false;
    }

    if (!provenance.sourceIds.contains(sourceId))
        provenance.sourceIds.prepend(sourceId);
    famp::workspace::WorkspaceEntity entity = famp::workspace::makeEntity(
        famp::workspace::EntityKind::PointCloud, name);
    entity.id = QUuid(derived.layer.id);
    entity.visible = true;
    entity.locked = false;
    entity.dirty = true;
    if (entity.assetPath.has_value() && entity.assetPath->trimmed().isEmpty())
        entity.assetPath.reset();
    entity.provenance = std::move(provenance);
    entity.setPayload(std::make_shared<MyCloudList>(derived));

    pointCloudList.push_back(derived);
    QString workspaceError;
    const famp::workspace::EntityId derivedId =
        workspaceStore->addDerivedEntity(entity, sourceId, &workspaceError);
    if (derivedId.isNull())
    {
        const MyCloudList failed = pointCloudList.back();
        removeCloudFromWorkspace(failed);
        inCloud = previousCloud;
        emit sendOrignalCloud(inCloud);
        QMessageBox::warning(this, tr("生成点云成果"), workspaceError);
        return false;
    }

    ++iCount;
    if (hideSource)
        workspaceStore->setVisible(sourceId, false, false);
    const QModelIndex derivedIndex = model->indexForId(derivedId);
    ui.treeView->expand(model->indexForId(parentId));
    ui.treeView->setCurrentIndex(derivedIndex);
    ui.treeView->scrollTo(derivedIndex);
    myVTK->display(pointCloudList.back().cloudactor);
    myVTK->update();
    ui.actAABB->setEnabled(true);
    ui.actAABB->setChecked(false);
    ui.actAABB->setText(tr("包围盒"));
    ui.actAABB->setToolTip(tr("选择可见点云后显示其包围盒"));
    markProjectDirty();
    updateCloudToolActions();
    updateEntityProperties();
    if (resultId)
        *resultId = derivedId;
    return true;
}

bool MainWindow::integrateDerivedEntity(
    const famp::workspace::EntityId& sourceId,
    famp::workspace::WorkspaceEntity entity,
    famp::workspace::EntityId* resultId)
{
    const famp::workspace::WorkspaceEntity* source =
        workspaceStore->entity(sourceId);
    if (!source)
    {
        QMessageBox::warning(
            this, tr("生成分析成果"), tr("来源实体已不存在。"));
        return false;
    }
    if (!entity.hasPayload())
    {
        QMessageBox::warning(
            this, tr("生成分析成果"), tr("分析成果没有可用的内存数据。"));
        return false;
    }
    if (!entity.provenance.has_value())
    {
        QMessageBox::warning(
            this, tr("生成分析成果"), tr("分析成果缺少来源记录。"));
        return false;
    }

    entity.visible = true;
    entity.locked = false;
    entity.dirty = true;
    entity.assetPath.reset();
    if (!entity.provenance->sourceIds.contains(sourceId))
        entity.provenance->sourceIds.prepend(sourceId);

    QString workspaceError;
    const famp::workspace::EntityId id = workspaceStore->addDerivedEntity(
        std::move(entity), sourceId, &workspaceError);
    if (id.isNull())
    {
        QMessageBox::warning(this, tr("生成分析成果"), workspaceError);
        return false;
    }

    const famp::workspace::WorkspaceEntity* added = workspaceStore->entity(id);
    const QModelIndex index = model->indexForId(id);
    if (added)
        ui.treeView->expand(model->indexForId(added->parentId));
    ui.treeView->setCurrentIndex(index);
    ui.treeView->scrollTo(index);
    markProjectDirty();
    updateEntityProperties();
    if (resultId)
        *resultId = id;
    return true;
}

void MainWindow::setCloudLoadUiBusy(bool busy)
{
    ui.actOpenCloud->setEnabled(!busy);
    if (newProjectAction)
        newProjectAction->setEnabled(!busy);
    if (openProjectAction)
        openProjectAction->setEnabled(!busy);
    if (saveProjectAction)
        saveProjectAction->setEnabled(!busy);
    if (saveProjectAsAction)
        saveProjectAsAction->setEnabled(!busy);
    if (exportReportAction)
        exportReportAction->setEnabled(!busy);
    if (recentFilesMenu)
        recentFilesMenu->setEnabled(!busy);
    setAcceptDrops(!busy);
    cloudLoadProgress->setVisible(busy);
    cloudLoadCancelButton->setVisible(busy);
    cloudLoadCancelButton->setEnabled(busy);
    if (busy && preprocessCloudAction)
        preprocessCloudAction->setEnabled(false);
    if (busy && cropCloudAction)
        cropCloudAction->setEnabled(false);
    if (busy && registerCloudAction)
        registerCloudAction->setEnabled(false);
    if (busy && reprojectCloudAction)
        reprojectCloudAction->setEnabled(false);
    if (busy && archaeologyMetadataAction)
        archaeologyMetadataAction->setEnabled(false);
    if (busy && controlPointsAction)
        controlPointsAction->setEnabled(false);
    if (busy && terrainAnalysisAction)
        terrainAnalysisAction->setEnabled(false);
    if (busy && cutFillAction)
        cutFillAction->setEnabled(false);
    if (busy && cloudProfileAction)
        cloudProfileAction->setEnabled(false);
    else if (!busy)
        updateCloudToolActions();
    updateProjectionActions();
    updateArchaeologyWorkflowGuide();
}

void MainWindow::finishCloudLoadBatch()
{
    if (!cloudLoadBusy)
        return;

    const bool projectBatch = cloudLoadProjectBatch;
    const bool projectRecovery = cloudLoadProjectRecovery;
    const QString projectPath = cloudLoadProjectPath;
    const int total = cloudLoadTotal;
    const int succeeded = cloudLoadSucceeded;
    const QStringList failurePaths = cloudLoadFailurePaths;
    const QStringList failureMessages = cloudLoadFailureMessages;
    const bool cancelled = cloudLoadCancelled;
    bool measurementRestoreIncomplete = false;
    bool workspaceRestoreIncomplete = false;

    if (projectBatch)
    {
        if (!pendingWorkspaceState.isEmpty())
        {
            famp::workspace::WorkspaceSnapshot snapshot;
            QString workspaceError;
            const bool snapshotApplied =
                famp::workspace::deserializeSnapshot(
                    pendingWorkspaceState, snapshot, &workspaceError)
                && famp::workspace::applySnapshot(
                    snapshot, *workspaceStore, &workspaceError);
            if (!snapshotApplied)
            {
                workspaceRestoreIncomplete = true;
                emit sendStr2Console(
                    tr("恢复项目内容树失败：%1").arg(workspaceError));
            }
            else
            {
                QStringList assetWarnings;
                if (!restoreWorkspaceAnalysisEntities(snapshot, assetWarnings))
                {
                    workspaceRestoreIncomplete = true;
                    for (const QString& warning : assetWarnings)
                        emit sendStr2Console(warning);
                }
            }
        }
        pendingWorkspaceState = {};

        QSet<QString> loadedLayerIds;
        for (const MyCloudList& cloud : pointCloudList)
            loadedLayerIds.insert(cloud.layer.id.trimmed().toLower());
        QVector<famp::measurement::Record3D> restorableMeasurements;
        restorableMeasurements.reserve(pendingProjectMeasurements3d.size());
        for (const auto& measurement : pendingProjectMeasurements3d)
        {
            if (loadedLayerIds.contains(
                    measurement.layerId.trimmed().toLower()))
            {
                restorableMeasurements.append(measurement);
            }
            else
            {
                measurementRestoreIncomplete = true;
            }
        }
        QString measurementError;
        if (!myVTK->setMeasurements(
                restorableMeasurements, &measurementError))
        {
            measurementRestoreIncomplete = true;
            emit sendStr2Console(
                tr("恢复项目三维测量失败：%1")
                    .arg(measurementError));
        }
        else if (measurementRestoreIncomplete)
        {
            emit sendStr2Console(
                tr("部分三维测量所属点云未加载，已跳过这些测量。"));
        }
        pendingProjectMeasurements3d.clear();
        synchronizeMeasurementEntities();
        synchronizeGraphicsEntities();
    }

    cloudLoadBusy = false;
    cloudLoadProjectBatch = false;
    cloudLoadProjectRecovery = false;
    cloudLoadCancelled = false;
    if (cloudLoadTask.isValid())
    {
        if (cancelled)
        {
            taskManager->acknowledgeCancellation(
                cloudLoadTask.id, tr("点云加载已取消"));
        }
        else if (!failurePaths.isEmpty())
        {
            taskManager->fail(
                cloudLoadTask.id,
                tr("%1 个点云加载失败").arg(failurePaths.size()));
        }
        else
        {
            taskManager->succeed(
                cloudLoadTask.id,
                tr("已加载 %1 个点云").arg(succeeded));
        }
    }
    cloudLoadTask = {};
    projectCloudReferences.clear();
    pendingCloudFiles.clear();
    currentCloudLoadPath.clear();
    setCloudLoadUiBusy(false);

    if (projectBatch)
    {
        loadingProject = false;
        projectDirty = projectRecovery || cancelled || !failurePaths.isEmpty()
            || measurementRestoreIncomplete || workspaceRestoreIncomplete;
        updateWindowTitle();
        emit sendStr2Console(
            cancelled
                ? tr("项目加载已取消  %1（点云 %2/%3）")
                      .arg(projectPath)
                      .arg(succeeded)
                      .arg(total)
                : tr("已打开项目  %1（点云 %2/%3）")
                      .arg(projectPath)
                      .arg(succeeded)
                      .arg(total));
    }
    else
    {
        emit sendStr2Console(tr("后台点云加载完成 %1/%2")
                                 .arg(succeeded)
                                 .arg(total));
    }

    statusBar()->showMessage(
        cancelled
            ? tr("点云加载已取消：已成功加载 %1 个").arg(succeeded)
            : tr("点云加载完成：成功 %1，失败 %2")
                  .arg(succeeded)
                  .arg(failurePaths.size()),
        6000);

    if (!failurePaths.isEmpty())
    {
        QStringList displayedMessages = failureMessages.mid(0, 10);
        QString details = displayedMessages.join(QStringLiteral("\n\n"));
        if (failureMessages.size() > displayedMessages.size())
        {
            details += tr("\n\n…共 %1 个文件未能加载")
                           .arg(failureMessages.size());
        }
        QMessageBox::warning(
            this,
            projectBatch ? tr("项目未完全加载") : tr("点云未完全加载"),
            details);
    }
}

void MainWindow::clearEntitySelectionHighlight()
{
    if (ui.graphicsView)
        ui.graphicsView->clearWorkspaceItemSelection();
    if (myVTK)
    {
        myVTK->clearMeasurementSelection();
        myVTK->displayAABBOrignalPosAxis(false);
        for (const MyCloudList& cloud : pointCloudList)
        {
            if (cloud.AABBactor)
                myVTK->removeAABBDisplay(cloud.AABBactor);
        }
    }
    isAABB = false;
    ui.actAABB->setChecked(false);
    ui.actAABB->setEnabled(false);
    ui.actAABB->setText(tr("包围盒"));
    ui.actAABB->setToolTip(tr("选择可见点云后显示其包围盒"));
}

void MainWindow::highlightWorkspaceEntity(const QModelIndex& index)
{
    clearEntitySelectionHighlight();
    const famp::workspace::WorkspaceEntity* entity = model->entity(index);
    if (!entity)
        return;

    if (entity->visible && entityRenderers.hasRenderer(entity->kind))
        entityRenderers.select(*entity);

    if (entity->kind != famp::workspace::EntityKind::PointCloud)
        return;

    const std::shared_ptr<MyCloudList> cloud =
        entity->payloadAs<MyCloudList>();
    const bool canShow = cloud && cloud->AABBactor
        && cloud->layer.visible && entity->visible;
    ui.actAABB->setEnabled(canShow);
    ui.actAABB->setChecked(canShow);
    isAABB = canShow;
    ui.actAABB->setToolTip(
        canShow ? tr("关闭当前选中点云的包围盒")
                : tr("当前点云已隐藏，无法显示包围盒"));
}

//点击DB Tree项目

void MainWindow::slotOn_treeView_clicked(const QModelIndex& index)
{
    ui.treeView->setCurrentIndex(index);
    updateEntityProperties();
    updateCloudToolActions();
    synchronizeProjectionWorkflowFromSelection();
    highlightWorkspaceEntity(index);

    MyCloudList cloud;
    if (!selectedCloudData(cloud))
    {
        ui.actRandomPlane->setEnabled(false);
        ui.actVerticalPlane->setEnabled(false);
        ui.actHorizonalPlane->setEnabled(false);
        return;
    }

    ui.actDelete->setEnabled(!cloud.layer.locked);
}

//DB Tree 删除项

void MainWindow::slotOn_actDelete_triggered()
{
    QVector<famp::workspace::EntityId> ids = selectedEntityIds();
    ids.removeAll(workspaceStore->rootId());
    if (ids.isEmpty())
        return;

    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, tr("删除实体"),
        tr("确定删除所选的 %1 个实体及其全部子项吗？此操作不能撤销。")
            .arg(ids.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes)
        return;

    QVector<MyCloudList> cloudsToRemove;
    QVector<famp::workspace::WorkspaceEntity> renderedEntitiesToRemove;
    QVector<famp::workspace::EntityId> payloadIds = ids;
    for (const famp::workspace::EntityId& id : ids)
        payloadIds += workspaceStore->descendants(id);
    for (const famp::workspace::EntityId& id : payloadIds)
    {
        const famp::workspace::WorkspaceEntity* entity = workspaceStore->entity(id);
        if (!entity)
            continue;
        if (entity->kind == famp::workspace::EntityKind::PointCloud)
        {
            const std::shared_ptr<MyCloudList> cloud =
                entity->payloadAs<MyCloudList>();
            if (cloud
                && std::none_of(
                    cloudsToRemove.cbegin(), cloudsToRemove.cend(),
                    [&cloud](const MyCloudList& candidate) {
                        return candidate.id == cloud->id;
                    }))
            {
                cloudsToRemove.append(*cloud);
            }
        }
        else if (entityRenderers.hasRenderer(entity->kind))
        {
            renderedEntitiesToRemove.append(*entity);
        }
    }

    QString error;
    if (!workspaceStore->removeEntities(ids, &error))
    {
        QMessageBox::warning(this, tr("无法删除实体"), error);
        return;
    }

    {
        // Only mutate the rendering backends after the store accepted the
        // entire deletion. This keeps a locked subtree failure atomic.
        const QScopedValueRollback<bool> graphicsGuard(
            syncingGraphicsEntities, true);
        const QScopedValueRollback<bool> measurementGuard(
            syncingMeasurementEntities, true);
        const QScopedValueRollback<bool> entityGuard(
            syncingWorkspaceEntity, true);
        for (const famp::workspace::WorkspaceEntity& entity
             : renderedEntitiesToRemove)
        {
            entityRenderers.remove(entity);
        }
    }
    for (const MyCloudList& cloud : cloudsToRemove)
        removeCloudFromWorkspace(cloud);

    if (pointCloudList.empty())
    {
        ui.actAABB->setEnabled(false);
        ui.actAABB->setChecked(false);
        ui.actRandomPlane->setEnabled(false);
        ui.actVerticalPlane->setEnabled(false);
        ui.actHorizonalPlane->setEnabled(false);
        isAABB = false;
    }
    markProjectDirty();
    synchronizeMeasurementEntities();
    synchronizeGraphicsEntities();
    updateCloudToolActions();
}

//开启AABB按钮

void MainWindow::slotOn_actAABB_triggered(bool checked)
{
    MyCloudList cloud;
    if (!selectedCloudData(cloud) || !cloud.layer.visible)
    {
        isAABB = false;
        ui.actAABB->setChecked(false);
        ui.actAABB->setToolTip(tr("选择可见点云后显示其包围盒"));
        return;
    }
    isAABB = checked;
    ui.actAABB->setText(tr("包围盒"));
    ui.actAABB->setToolTip(
        checked ? tr("关闭当前选中点云的包围盒")
                : tr("显示当前选中点云的包围盒"));
    if (checked)
        myVTK->display(cloud.AABBactor);
    else
        myVTK->removeAABBDisplay(cloud.AABBactor);
    myVTK->update();
}

//接受消息到Console
void MainWindow::slotGetStr2Console(QString text)
{
    //获取系统时间
    QTime current_time = QTime::currentTime();
    QString time = QString::asprintf("[%d:%d:%d]", current_time.hour(), current_time.minute(), current_time.second());

    ui.listWidget->addItem(time + "\t" + text);
    ui.listWidget->setCurrentRow(ui.listWidget->count() - 1);
}

//随机平面
//统一的平面部件显示方法
void MainWindow::showPlaneWidget(vtkPlaneWidget* (MyVTK::*displayFunc)(), const char* consoleMsg)
{
    MyCloudList source;
    if (!selectedCloudData(source) || !source.layer.points
        || source.layer.points->empty() || !source.layer.visible)
    {
        QMessageBox::information(
            this, tr("点云切割"),
            tr("请先在内容列表中选择一个可见的点云。"));
        return;
    }
    activeVtkSourceId = entityIdForCloud(source);
    myVTK->getDBItemCloud(
        source.layer.points, source.layer.display.pointSize);

    //将切割按钮设置为禁止开启
    ui.actHorizonalPlane->setEnabled(false);
    ui.actRandomPlane->setEnabled(false);
    ui.actVerticalPlane->setEnabled(false);
    ui.actDelete->setEnabled(false);

    //获得平面
    vtkPlaneWidget * plane;
    plane = (myVTK->*displayFunc)();
    plane->On();
    emit sendClipPlane(plane);

    //弹出平面裁剪对话框
    myVTK->setDlgClip();
    emit sendStr2Console(consoleMsg);
    myVTK->displayAABBOrignalPosAxis(true);     //显示AABB最小的坐标创建坐标轴
    myVTK->update();
}

//任意平面
void MainWindow::slotOn_actRandomPlane_triggered(bool checked)
{
    showPlaneWidget(&MyVTK::DisplayRandomPlane, "已加载任意切割面");
}

//垂直平面
void MainWindow::slotOn_actVerticalPlane_triggered()
{
    showPlaneWidget(&MyVTK::DisplayVerticalPlane, "已加载竖直切割面");
}

//水平平面
void MainWindow::slotOn_actHorizonalPlane_triggered(bool checked)
{
    showPlaneWidget(&MyVTK::DisplayHorizonalPlane, "已加载水平切割面");
}

//将DBTree下的Item点云送到VTK

void MainWindow::DBTreeSendVTKItemCloud()
{
    synchronizeProjectionWorkflowFromSelection();
}

void MainWindow::DBTreeSendGraphicViewItemCloud()
{
    MyCloudList cloud;
    if (selectedCloudData(cloud))
        ui.graphicsView->getDBItemCloud(cloud.layer.points);
}

void MainWindow::setClipButtonEnable(const QModelIndex&)
{
    MyCloudList cloud;
    const bool enabled = selectedCloudData(cloud)
        && cloud.layer.visible && !myVTK->isActiveDlgClip;
    ui.actHorizonalPlane->setEnabled(enabled);
    ui.actRandomPlane->setEnabled(enabled);
    ui.actVerticalPlane->setEnabled(enabled);
}

void MainWindow::setDlgClipPbnEnable(const QModelIndex&)
{
    MyCloudList cloud;
    const bool enabled = selectedCloudData(cloud) && cloud.layer.visible;
    if (myVTK->isActiveDlgClip)
        emit sendDlgClipPbnEnable(enabled);
    if (myVTK->isActiveDlgClip)
        ui.actDelete->setEnabled(false);
}

//鼠标追踪获得GraphicScene坐标
void MainWindow::slotOn_mouseMove_SceneCoordinate(QPoint point)
{
    QPointF pointScene = ui.graphicsView->mapToScene(point);
    ui.graphicsView->labelScene->setText(QString::asprintf("Scene坐标:%.1f,%.1f", pointScene.x(), pointScene.y()));
}

//接受是否关闭XOY图标
void MainWindow::getClosedXOYLabel(bool enable)
{
    setXOYLabelVisible(enable);
}

//接受是否关闭比例尺
void MainWindow::getClosedScale(bool enable)
{
    setScaleVisible(enable);
}

//发送当前比例尺到GraphicView
void MainWindow::sendCurrentScaleToGraphicView()
{
    ui.graphicsView->currentScaleIndex = this->scaleCombox->currentIndex();
}
