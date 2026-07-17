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
#include "FileIO.h"
#include "GraphicsUndoCommands.h"
#include "HelpContent.h"
#include "LasLoader.h"
#include "PcdLoader.h"
#include "ProcessingRecipe.h"
#include "RecentFiles.h"
#include "TerrainAnalysis.h"
#include "TerrainDialog.h"
#include "TerrainIO.h"

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

#include <pcl/pcl_config.h>
#include <vtkVersion.h>
#include <vtkMapper.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkProperty.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

Q_DECLARE_METATYPE(MyCloudList)

static int iCount = 0;      //记录点云的ID号

namespace
{
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

struct TerrainTaskOutput
{
    famp::terrain::Result analysis;
    QStringList savedPaths;
    QStringList warnings;
    QString error;
    bool sidecarSaved = false;
    bool cancelled = false;

    bool succeeded() const
    {
        return analysis.succeeded() && sidecarSaved
            && error.isEmpty() && !cancelled;
    }
};
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
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
    , measurementActionGroup(nullptr)
    , distanceMeasureAction(nullptr)
    , areaMeasureAction(nullptr)
    , angleMeasureAction(nullptr)
    , clearMeasurementsAction(nullptr)
    , cloudDisplaySettingsAction(nullptr)
    , preprocessCloudAction(nullptr)
    , cropCloudAction(nullptr)
    , registerCloudAction(nullptr)
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
{
    ui.setupUi(this);

    this->resize(1920, 1080);
    setAcceptDrops(true);

    myCloud = NULL;
    inCloud = NULL;
    itemCloud = NULL;
    itemProject = NULL;
    dlgClip = new QDlgClip;
    myItem = NULL;

    isAABB = false; //是否显示AABB

    model = new QStandardItemModel(ui.treeView);    //将tree放入标准模式中
    model->setHorizontalHeaderLabels(QStringList() << "");  //设置水平表头
    icon_1 = QIcon(":/images/images/dbHObjectSymbol.png");
    icon_2 = QIcon(":/images/images/dbCloudSymbol.png");
    ui.treeView->setModel(this->model);
    ui.treeView->setItemsExpandable(true);      //默认全部展开
    ui.treeView->expandAll();

    //允许嵌套dock
    setDockNestingEnabled(true);

    //DBTree
    ui.dockWidget1->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, ui.dockWidget1);

    //GraphicsView
    ui.dockWidget2->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    this->resizeDocks({ ui.dockWidget2 }, {(this->width()-ui.dockWidget2->width())/2}, Qt::Horizontal);
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
    initializeProjectActions();
    initializeRecentFilesMenu();
    initializeCrsActions();
    initializeUndoActions();

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
    QTimer::singleShot(0, this, [this]() { checkForRecoveryProject(); });

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
            this, &MainWindow::markProjectDirty);

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
    for (int row = 0; row < model->rowCount(); ++row)
    {
        const QStandardItem* projectItem = model->item(row);
        if (!projectItem || projectItem->rowCount() == 0)
            continue;
        const QStandardItem* cloudItem = projectItem->child(0);
        const MyCloudList cloud = cloudItem->data(Qt::UserRole + 2)
                                      .value<MyCloudList>();
        famp::report::CloudEntry entry;
        entry.name = cloud.layer.name;
        entry.path = cloudItem->data(Qt::UserRole).toString();
        entry.crs = cloud.layer.crs;
        entry.pointCount = cloud.layer.points ? cloud.layer.points->size() : 0;
        entry.visible = cloudItem->checkState() == Qt::Checked;
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
    QString* errorMessage) const
{
    document.mapScale = scaleCombox->currentText();
    document.projectCrs = projectCrs;
    document.graphicsState = ui.graphicsView->saveProjectState(errorMessage);
    if (document.graphicsState.isEmpty())
        return false;
    document.measurements3d = myVTK->measurements();
    document.windowGeometry = saveGeometry();
    document.windowState = saveState(famp::project::SchemaVersion);
    document.xoyLabelVisible = xoy_label && !xoy_label->isHidden();
    document.scaleVisible = scaleCombox && !scaleCombox->isHidden();
    for (int row = 0; row < model->rowCount(); ++row)
    {
        const QStandardItem* projectItem = model->item(row);
        if (!projectItem || projectItem->rowCount() == 0)
            continue;

        const QString path = projectItem->child(0)->data(Qt::UserRole).toString();
        if (!path.isEmpty())
        {
            document.cloudFiles.append(path);
            const MyCloudList cloud = projectItem->child(0)
                                          ->data(Qt::UserRole + 2)
                                          .value<MyCloudList>();
            famp::project::CloudReference reference;
            reference.path = path;
            reference.layerId = cloud.layer.id;
            reference.name = projectItem->child(0)->text().trimmed();
            reference.crs = cloud.layer.crs;
            reference.visible = projectItem->child(0)->checkState()
                == Qt::Checked;
            reference.locked = cloud.layer.locked;
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
    famp::project::Document document;
    if (!currentProjectDocument(document, &error))
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

bool MainWindow::maybeSaveCurrentProject()
{
    if (!projectDirty)
        return true;

    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        tr("项目尚未保存"),
        tr("当前点云项目有未保存的更改。"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel)
        return false;
    if (choice == QMessageBox::Save)
        return saveProject(false);

    return true;
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
    while (!pointCloudList.empty())
        removeCloudFromWorkspace(pointCloudList.back());
    model->clear();
    model->setHorizontalHeaderLabels(QStringList() << QString());
    ui.graphicsView->clearSceneAndHistory();
    pendingProjectMeasurements3d.clear();

    inCloud.reset();
    delete myCloud;
    myCloud = nullptr;
    itemCloud = nullptr;
    itemProject = nullptr;
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
    QFile::remove(recoveryProjectPath());
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

    QString error;
    famp::project::Document document;
    if (!currentProjectDocument(document, &error))
    {
        statusBar()->showMessage(tr("自动保存失败：%1").arg(error), 8000);
        return;
    }
    if (!famp::project::save(
            recoveryProjectPath(),
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
    QString sourcePath;
    if (!selectedCloudData(cloud, &sourcePath))
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
           "结果会原子保存为新的 PCD，当前图层切换到新文件。"
           "原有逐点属性会按原类型和顺序无损保留。"
           "该图层原有三维测量和控制点将随重投影移除，撤销时一并恢复。"),
        &dialog);
    explanation.setWordWrap(true);
    layout.addRow(tr("源 CRS"), &sourceCrsEdit);
    layout.addRow(tr("目标 CRS"), &targetCrsEdit);
    layout.addRow(&explanation);
    QDialogButtonBox buttons(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons.button(QDialogButtonBox::Ok)->setText(tr("选择输出文件…"));
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

    const QFileInfo sourceFile(sourcePath);
    QString targetSuffix = targetInfo.identifier;
    targetSuffix.replace(QLatin1Char(':'), QLatin1Char('_'));
    const QString initialPath = sourceFile.absoluteDir().filePath(
        sourceFile.completeBaseName() + QStringLiteral("_reprojected_")
        + targetSuffix + QStringLiteral(".pcd"));
    QString outputPath = QFileDialog::getSaveFileName(
        this, tr("保存重投影点云"), initialPath, tr("PCD 点云 (*.pcd)"));
    if (outputPath.isEmpty())
        return;
    outputPath = famp::recent::normalizedPath(
        famp::io::pathWithRequiredSuffix(outputPath, QStringLiteral("pcd")));

    const Qt::CaseSensitivity pathCaseSensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (outputPath.compare(
            famp::recent::normalizedPath(sourcePath), pathCaseSensitivity) == 0)
    {
        QMessageBox::warning(
            this, tr("点云重投影"),
            tr("输出文件不能覆盖当前源点云，请选择新的文件名。"));
        return;
    }
    for (const MyCloudList& candidate : pointCloudList)
    {
        if (candidate.layer.id == cloud.layer.id)
            continue;
        if (outputPath.compare(
                famp::recent::normalizedPath(candidate.layer.sourcePath),
                pathCaseSensitivity) == 0)
        {
            QMessageBox::warning(
                this, tr("点云重投影"),
                tr("输出文件正被另一个已加载图层使用，请选择新的文件名。"));
            return;
        }
    }

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
    const famp::cloud::CloudAttributes inputAttributes = cloud.layer.attributes;
    const QString sourceCrs = sourceInfo.identifier;
    const QString targetCrs = targetInfo.identifier;
    watcher.setFuture(QtConcurrent::run(
        [this, task, input, inputSpatial, inputAttributes,
         sourceCrs, targetCrs, outputPath]() {
            auto result = famp::cloud::reproject(
                input, inputSpatial, sourceCrs, targetCrs,
                task.cancellationCheck(),
                [this, task](double value) {
                    taskManager->setProgress(
                        task.id, value * 0.9,
                        tr("正在转换点云坐标：%1%")
                            .arg(static_cast<int>(value * 100.0)));
                });
            if (!result.succeeded())
                return result;
            if (famp::tasks::isCancellationRequested(task.cancellationCheck()))
            {
                result.points.reset();
                result.cancelled = true;
                result.error = tr("点云重投影已取消。");
                return result;
            }

            taskManager->setProgress(task.id, 0.92, tr("正在原子保存重投影点云…"));
            QString saveError;
            if (!famp::io::savePcdAsciiAtomically(
                    outputPath, *result.points, &saveError, &result.spatial,
                    &inputAttributes))
            {
                result.error = saveError;
                return result;
            }
            if (famp::tasks::isCancellationRequested(task.cancellationCheck()))
            {
                result.points.reset();
                result.cancelled = true;
                result.error = tr("重投影在保存完成后取消；输出文件已保留，图层未切换。");
                return result;
            }
            taskManager->setProgress(task.id, 1.0, tr("重投影点云已保存"));
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
    taskManager->succeed(task.id, tr("点云重投影完成"));

    const famp::cloud::CloudLayer before = cloud.layer;
    const QVector<famp::measurement::Record3D> beforeMeasurements =
        myVTK->measurements();
    QVector<famp::measurement::Record3D> afterMeasurements;
    afterMeasurements.reserve(beforeMeasurements.size());
    for (const auto& measurement : beforeMeasurements)
    {
        if (measurement.layerId.compare(
                before.id, Qt::CaseInsensitive) != 0)
        {
            afterMeasurements.append(measurement);
        }
    }
    famp::cloud::CloudLayer after = before;
    after.points = result.points;
    after.sourcePoints.reset();
    after.sourcePath = outputPath;
    after.spatial = result.spatial;
    after.crs = result.targetCrs;
    after.controlPoints.clear();
    if (before.name == sourceFile.fileName())
        after.name = QFileInfo(outputPath).fileName();
    ++after.revision;
    const QString layerId = before.id;
    ui.graphicsView->commandStack()->push(
        famp::graphics::makeCallbackCommand(
            [this, layerId, before, beforeMeasurements]() {
                if (!applyCloudLayerState(layerId, before))
                    return;
                QString error;
                if (!myVTK->setMeasurements(beforeMeasurements, &error))
                    emit sendStr2Console(
                        tr("恢复重投影前测量失败：%1").arg(error));
            },
            [this, layerId, after, afterMeasurements]() {
                if (!applyCloudLayerState(layerId, after))
                    return;
                QString error;
                if (!myVTK->setMeasurements(afterMeasurements, &error))
                    emit sendStr2Console(
                        tr("更新重投影后测量失败：%1").arg(error));
            },
            tr("重投影点云 %1 → %2")
                .arg(result.sourceCrs, result.targetCrs)));
    addRecentFile(outputPath);
    statusBar()->showMessage(
        tr("点云已重投影为 %1，可使用撤销恢复。")
            .arg(result.targetCrs),
        8000);
    emit sendStr2Console(
        tr("点云重投影完成：%1 → %2，%3 个点，已保存到 %4")
            .arg(result.sourceCrs, result.targetCrs)
            .arg(result.points->size())
            .arg(outputPath));
}

bool MainWindow::selectedCloudData(MyCloudList& cloud, QString* path) const
{
    const QModelIndex index = ui.treeView->currentIndex();
    if (!index.isValid())
        return false;

    QStandardItem* item = model->itemFromIndex(index);
    if (!item)
        return false;
    if (item->hasChildren())
        item = item->child(0);
    if (!item)
        return false;

    const QVariant data = item->data(Qt::UserRole + 2);
    if (!data.isValid())
        return false;
    cloud = data.value<MyCloudList>();
    if (!cloud.layer.points || cloud.layer.points->empty() || !cloud.cloudactor)
        return false;

    if (path)
        *path = item->data(Qt::UserRole).toString();
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

    const QSignalBlocker blocker(model);
    for (int row = 0; row < model->rowCount(); ++row)
    {
        QStandardItem* projectItem = model->item(row);
        if (!projectItem || projectItem->rowCount() == 0)
            continue;
        QStandardItem* cloudItem = projectItem->child(0);
        const QVariant data = cloudItem->data(Qt::UserRole + 2);
        if (!data.isValid() || data.value<MyCloudList>().id != cloud.id)
            continue;
        projectItem->setData(QVariant::fromValue(cloud), Qt::UserRole + 2);
        cloudItem->setData(QVariant::fromValue(cloud), Qt::UserRole + 2);
        const QString path = cloud.layer.sourcePath;
        projectItem->setData(path, Qt::UserRole);
        cloudItem->setData(path, Qt::UserRole);
        cloudItem->setData(QFileInfo(path).fileName(), Qt::UserRole + 1);
        projectItem->setText(cloud.layer.name + QStringLiteral("(")
                             + path + QStringLiteral(")"));
        cloudItem->setText(cloud.layer.name);
        return;
    }
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
                    tr(" DEM CSV"), exportPaths.gridCsv,
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
                exportPaths.sidecar,
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
        .arg(result.savedPaths.join(QStringLiteral("；")))
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
        tr("处理结果会原子保存为新的 PCD 文件并作为新点云加入项目，原始点云和原始文件不会被修改。"),
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
    buttons.button(QDialogButtonBox::Ok)->setText(tr("选择输出文件…"));
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

    const QFileInfo sourceInfo(sourcePath);
    const QString suffix = options.method
            == famp::processing::Method::VoxelDownsample
        ? QStringLiteral("_voxel.pcd")
        : QStringLiteral("_denoised.pcd");
    const QString initialPath = sourceInfo.absoluteDir().filePath(
        sourceInfo.completeBaseName() + suffix);
    QString outputPath = QFileDialog::getSaveFileName(
        this, tr("保存预处理点云"), initialPath, tr("PCD 点云 (*.pcd)"));
    if (outputPath.isEmpty())
        return;
    outputPath = famp::io::pathWithRequiredSuffix(outputPath, QStringLiteral("pcd"));

    const Qt::CaseSensitivity pathCaseSensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (QFileInfo(outputPath).absoluteFilePath().compare(
            QFileInfo(sourcePath).absoluteFilePath(), pathCaseSensitivity) == 0)
    {
        QMessageBox::warning(
            this, tr("点云预处理"),
            tr("输出文件不能覆盖当前源点云，请选择新的文件名。"));
        return;
    }

    QProgressDialog progress(
        tr("正在后台预处理并保存点云…"), tr("取消"), 0, 0, this);
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
    const famp::cloud::SpatialReference spatial = cloud.layer.spatial;
    watcher.setFuture(QtConcurrent::run(
        [input, options, outputPath, cancellation, spatial]() {
            return famp::processing::processAndSave(
                input, options, outputPath, [cancellation]() {
                    return cancellation->load(std::memory_order_relaxed);
                }, &spatial);
        }));
    if (!watcher.isFinished())
        progress.exec();

    const famp::processing::Result result = watcher.result();
    if (result.cancelled)
    {
        statusBar()->showMessage(tr("点云预处理已取消，未写入输出文件。"), 5000);
        emit sendStr2Console(tr("点云预处理已取消  %1").arg(sourcePath));
        return;
    }
    if (!result.succeeded())
    {
        QMessageBox::warning(this, tr("点云预处理失败"), result.error);
        return;
    }

    QString recipePath;
    QString recipeError;
    const QString automaticRecipePath =
        famp::recipe::automaticSidecarPath(result.outputPath);
    if (!famp::recipe::save(
            automaticRecipePath,
            famp::recipe::forProcessing(options, sourcePath),
            &recipePath,
            &recipeError))
    {
        emit sendStr2Console(
            tr("点云处理完成，但自动方案保存失败：%1").arg(recipeError));
    }
    else
    {
        emit sendStr2Console(tr("已保存可复现处理方案  %1").arg(recipePath));
    }

    famp::cloud::LoadResult loaded;
    loaded.path = result.outputPath;
    loaded.displayCloud = result.cloud;
    loaded.spatial = cloud.layer.spatial;
    integrateLoadedCloud(loaded);
    emit sendStr2Console(
        tr("点云预处理完成：%1 → %2 个点，已保存到 %3")
            .arg(result.inputPointCount)
            .arg(result.outputPointCount)
            .arg(result.outputPath));
    updateCloudToolActions();
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
        spinBox->setDecimals(6);
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
    QLabel note(tr("范围使用当前点云的局部坐标；结果另存为新 PCD，原始文件不变。"),
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
    buttons.button(QDialogButtonBox::Ok)->setText(tr("选择输出文件…"));
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

    const QFileInfo sourceInfo(sourcePath);
    const QString initialPath = sourceInfo.absoluteDir().filePath(
        sourceInfo.completeBaseName() + QStringLiteral("_cropped.pcd"));
    QString outputPath = QFileDialog::getSaveFileName(
        this, tr("保存裁剪点云"), initialPath, tr("PCD 点云 (*.pcd)"));
    if (outputPath.isEmpty())
        return;
    outputPath = famp::io::pathWithRequiredSuffix(outputPath, QStringLiteral("pcd"));

    const Qt::CaseSensitivity pathCaseSensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (QFileInfo(outputPath).absoluteFilePath().compare(
            QFileInfo(sourcePath).absoluteFilePath(), pathCaseSensitivity) == 0)
    {
        QMessageBox::warning(this, tr("点云范围裁剪"),
                             tr("输出文件不能覆盖当前源点云。"));
        return;
    }

    QProgressDialog progress(
        tr("正在后台裁剪并保存点云…"), tr("取消"), 0, 0, this);
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
    const famp::cloud::SpatialReference spatial = cloud.layer.spatial;
    watcher.setFuture(QtConcurrent::run(
        [input, options, outputPath, cancellation, spatial]() {
            return famp::crop::processAndSave(
                input, options, outputPath, [cancellation]() {
                    return cancellation->load(std::memory_order_relaxed);
                }, &spatial);
        }));
    if (!watcher.isFinished())
        progress.exec();
    const famp::crop::Result result = watcher.result();
    if (result.cancelled)
    {
        statusBar()->showMessage(tr("点云范围裁剪已取消，未写入输出文件。"), 5000);
        return;
    }
    if (!result.succeeded())
    {
        QMessageBox::warning(this, tr("点云范围裁剪失败"), result.error);
        return;
    }

    QString recipePath;
    QString recipeError;
    if (!famp::recipe::save(
            famp::recipe::automaticSidecarPath(result.outputPath),
            famp::recipe::forCrop(options, sourcePath),
            &recipePath,
            &recipeError))
    {
        emit sendStr2Console(
            tr("范围裁剪完成，但自动方案保存失败：%1").arg(recipeError));
    }
    else
    {
        emit sendStr2Console(tr("已保存可复现处理方案  %1").arg(recipePath));
    }

    famp::cloud::LoadResult loaded;
    loaded.path = result.outputPath;
    loaded.displayCloud = result.cloud;
    loaded.spatial = cloud.layer.spatial;
    integrateLoadedCloud(loaded);
    emit sendStr2Console(
        tr("点云范围裁剪完成：%1 → %2 个点，已保存到 %3")
            .arg(result.inputPointCount)
            .arg(result.outputPointCount)
            .arg(result.outputPath));
    updateCloudToolActions();
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
    for (int row = 0; row < model->rowCount(); ++row)
    {
        QStandardItem* projectItem = model->item(row);
        if (!projectItem || projectItem->rowCount() == 0)
            continue;
        QStandardItem* cloudItem = projectItem->child(0);
        const QVariant data = cloudItem->data(Qt::UserRole + 2);
        if (!data.isValid())
            continue;
        const MyCloudList candidate = data.value<MyCloudList>();
        if (!candidate.layer.points || candidate.layer.points->empty())
            continue;
        Entry entry;
        entry.cloud = candidate;
        entry.path = cloudItem->data(Qt::UserRole).toString();
        entry.label = QFileInfo(entry.path).fileName();
        if (entry.label.isEmpty())
            entry.label = tr("点云 %1").arg(candidate.id);
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
    QLabel note(tr("将源点云刚性配准到目标点云，结果保存为新的 PCD 并沿用目标点云坐标参考；原始点云不会被修改。ICP 需要两者初始位置足够接近。"), &dialog);
    note.setWordWrap(true);
    layout.addRow(tr("源点云（移动）"), &sourceCombo);
    layout.addRow(tr("目标点云（固定）"), &targetCombo);
    layout.addRow(tr("最大迭代次数"), &iterations);
    layout.addRow(tr("最大对应距离"), &correspondenceDistance);
    layout.addRow(tr("配准体素边长"), &samplingVoxelSize);
    layout.addRow(tr("变换收敛阈值"), &transformationEpsilon);
    layout.addRow(tr("适应度收敛阈值"), &fitnessEpsilon);
    layout.addRow(&note);
    QDialogButtonBox buttons(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons.button(QDialogButtonBox::Ok)->setText(tr("选择输出文件…"));
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
    famp::registration::Options options;
    options.maximumIterations = iterations.value();
    options.maximumCorrespondenceDistance = correspondenceDistance.value();
    options.samplingVoxelSizeMeters = samplingVoxelSize.value();
    options.transformationEpsilon = transformationEpsilon.value();
    options.fitnessEpsilon = fitnessEpsilon.value();
    QString validationError;
    if (!famp::registration::validateOptions(options, &validationError))
    {
        QMessageBox::warning(this, tr("点云 ICP 配准"), validationError);
        return;
    }
    const QFileInfo sourceInfo(source.path);
    QString outputPath = QFileDialog::getSaveFileName(
        this, tr("保存配准点云"),
        sourceInfo.absoluteDir().filePath(
            sourceInfo.completeBaseName() + QStringLiteral("_registered.pcd")),
        tr("PCD 点云 (*.pcd)"));
    if (outputPath.isEmpty())
        return;
    outputPath = famp::io::pathWithRequiredSuffix(outputPath, QStringLiteral("pcd"));
    const Qt::CaseSensitivity pathCaseSensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (QFileInfo(outputPath).absoluteFilePath().compare(
            QFileInfo(source.path).absoluteFilePath(), pathCaseSensitivity) == 0
        || QFileInfo(outputPath).absoluteFilePath().compare(
            QFileInfo(target.path).absoluteFilePath(), pathCaseSensitivity) == 0)
    {
        QMessageBox::warning(this, tr("点云 ICP 配准"),
                             tr("输出文件不能覆盖源点云或目标点云。"));
        return;
    }

    QProgressDialog progress(tr("正在后台配准并保存点云…"), tr("取消"), 0, 0, this);
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
    const famp::cloud::SpatialReference targetSpatial = target.cloud.layer.spatial;
    watcher.setFuture(QtConcurrent::run(
        [sourceCloud, targetCloud, options, outputPath, cancellation, targetSpatial]() {
            return famp::registration::alignAndSave(
                sourceCloud, targetCloud, options, outputPath, [cancellation]() {
                    return cancellation->load(std::memory_order_relaxed);
                }, &targetSpatial);
        }));
    if (!watcher.isFinished())
        progress.exec();
    const famp::registration::Result result = watcher.result();
    if (result.cancelled)
    {
        statusBar()->showMessage(tr("点云配准已取消，未写入输出文件。"), 5000);
        return;
    }
    if (!result.succeeded())
    {
        QMessageBox::warning(this, tr("点云配准失败"), result.error);
        return;
    }

    famp::cloud::LoadResult loaded;
    loaded.path = result.outputPath;
    loaded.displayCloud = result.cloud;
    loaded.spatial = target.cloud.layer.spatial;
    integrateLoadedCloud(loaded);
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
        tr("ICP 配准完成：适应度 %1，配准采样 %2/%3 点，输出 %4\n变换矩阵：\n%5")
            .arg(result.fitnessScore, 0, 'g', 10)
            .arg(result.registrationSourcePointCount)
            .arg(result.registrationTargetPointCount)
            .arg(result.outputPath, matrixRows.join(QLatin1Char('\n'))));
    updateCloudToolActions();
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
    const QFileInfo fileInfo(result.path);
    const QString dir = fileInfo.fileName();

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

    /////-------------------DB tree---------------------
    QString str = pointCloud.layer.name + "(" + result.path + ")";

    //点云文件夹
    itemProject = new QStandardItem(icon_1, str);
    itemProject->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsEditable );
    itemProject->setCheckState(cloudVisible ? Qt::Checked : Qt::Unchecked);
    itemProject->setData(QVariant(result.path), Qt::UserRole);
    itemProject->setData(QVariant::fromValue(pointCloud), Qt::UserRole + 2);
    model->appendRow(itemProject);
    //else(currentItem->parent()->appendRow(itemProject));

    //点云文件
    QStandardItem * itemChild = new QStandardItem(icon_2, pointCloud.layer.name);
    ui.treeView->expand(itemProject->index());      //默认展开该项
    Qt::ItemFlags cloudFlags = Qt::ItemIsSelectable | Qt::ItemIsEnabled
        | Qt::ItemIsUserCheckable;
    if (!pointCloud.layer.locked)
    {
        cloudFlags |= Qt::ItemIsEditable | Qt::ItemIsDragEnabled
            | Qt::ItemIsDropEnabled;
    }
    itemChild->setFlags(cloudFlags);
    itemChild->setCheckState(cloudVisible ? Qt::Checked : Qt::Unchecked);
    itemChild->setData(QVariant(result.path), Qt::UserRole);
    itemChild->setData(QVariant(dir), Qt::UserRole+1);
    itemChild->setData(QVariant::fromValue(pointCloud), Qt::UserRole + 2);
    itemProject->appendRow(itemChild);

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
    ui.actAABB->setText("关闭包围盒");
    addRecentFile(result.path);
    markProjectDirty();
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
    else if (!busy)
        updateCloudToolActions();
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

    if (projectBatch)
    {
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
            || measurementRestoreIncomplete;
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

//点击DB Tree项目
void MainWindow::slotOn_treeView_clicked(const QModelIndex & index)
{
    ui.treeView->setCurrentIndex(index);
    updateCloudToolActions();
    QStandardItem * currentItem = model->itemFromIndex(index);
    //qDebug() << currentItem->data(Qt::UserRole).toString();
    MyCloudList cloudData = currentItem->data(Qt::UserRole + 2).value<MyCloudList>();
    vtkActor * cloudDataActor = cloudData.cloudactor;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr  itemCloud = cloudData.layer.points;
    //qDebug() <<"cloudID"<< cloudData.id;

    //qDebug() << currentItem->text();

    //AABB按钮变化情况
    ui.actDelete->setEnabled(true);         //设置删除按钮是否启用
    if (currentItem->isCheckable())
    {
        if (currentItem->checkState() == Qt::Checked)
        {
            if (currentItem->hasChildren())
            {
                ui.actAABB->setEnabled(false);
                ui.actAABB->setText("关闭包围盒");
                ui.actAABB->setChecked(false);
            }
            else if (!currentItem->hasChildren())
            {
                if (isAABB)
                {
                    ui.actAABB->setEnabled(true);
                    ui.actAABB->setChecked(true);
                    ui.actAABB->setText("显示包围盒");
                }
                else if (!isAABB)
                {
                    ui.actAABB->setEnabled(true);
                    ui.actAABB->setChecked(false);
                    ui.actAABB->setText("关闭包围盒");
                }
            }
        }
        else if (currentItem->checkState() == Qt::Unchecked)
        {
            ui.actAABB->setEnabled(false);
            ui.actAABB->setText("关闭包围盒");
            ui.actAABB->setChecked(false);
        }

    }

    //DBTree check框变化
    if (currentItem->checkState() == Qt::Checked)
    {

        if (!currentItem->hasChildren())    //没有子成员
        {
            if (currentItem->parent() != 0)     //有父亲
            {
                myVTK->display(cloudDataActor); //显示点云演员;

                if (isAABB)
                {
                    ui.actAABB->setEnabled(true);
                    ui.actAABB->setChecked(true);
                    ui.actAABB->setText("显示包围盒");
                    myVTK->display(cloudData.AABBactor);
                }
                else if(!isAABB)
                {
                    ui.actAABB->setEnabled(true);
                    ui.actAABB->setChecked(false);
                    ui.actAABB->setText("关闭包围盒");
                    myVTK->removeAABBDisplay(cloudData.AABBactor);
                }
            }

        }
        else                                 //有子成员
        {
            MyCloudList childData = currentItem->child(0)->data(Qt::UserRole + 2).value<MyCloudList>();
            myVTK->display(childData.cloudactor);       //显示子成员点云演员

            /*if (isAABB)
            {
                ui.actAABB->setEnabled(true);
                ui.actAABB->setChecked(true);
                ui.actAABB->setText("显示AABB");
                myVTK->display(childData.AABBactor);
            }
            else if (!isAABB)
            {
                ui.actAABB->setEnabled(true);
                ui.actAABB->setChecked(false);
                ui.actAABB->setText("关闭AABB");
                myVTK->removeAABBDisplay(childData.AABBactor);
            }*/
        }
    }
    else if (currentItem->checkState() == Qt::Unchecked)
    {

        if (!currentItem->hasChildren())    //没有子成员
        {
            if (currentItem->parent() != 0)     //有父亲
            {
                myVTK->removeCloudDisplay(cloudDataActor);  //移除点云演员

                ui.actAABB->setEnabled(false);
                ui.actAABB->setChecked(false);
                ui.actAABB->setText("关闭包围盒");
                isAABB = false;
                myVTK->removeAABBDisplay(cloudData.AABBactor);

            }

        }
        else                                //有子成员
        {
            MyCloudList childData = currentItem->child(0)->data(Qt::UserRole + 2).value<MyCloudList>();
            myVTK->removeCloudDisplay(childData.cloudactor);

            ui.actAABB->setEnabled(false);
            ui.actAABB->setChecked(false);
            ui.actAABB->setText("关闭包围盒");
            isAABB = false;
            myVTK->removeAABBDisplay(childData.AABBactor);

        }
    }

    //以AABB最小的坐标创建坐标轴
        if (currentItem->isCheckable())
        {
            if (!currentItem->hasChildren())
            {
                if (currentItem->checkState() == Qt::Checked)
                {
                    myVTK->AABBOrignalPosAxis(itemCloud);   //将DBTree有效的点云发送给VTK
                    if(myVTK->isActiveDlgClip)      myVTK->displayAABBOrignalPosAxis(true);     //是否显示AABB最小的坐标创建坐标轴

                }
                else if (currentItem->checkState() == Qt::Unchecked)
                {
                    myVTK->displayAABBOrignalPosAxis(false);        //是否显示AABB最小的坐标创建坐标轴
                }
            }

            else if (currentItem->hasChildren())
            {
                if (currentItem->checkState() == Qt::Checked)
                {
                    QStandardItem * childItem = currentItem->child(0);
                    MyCloudList cloudData = childItem->data(Qt::UserRole + 2).value<MyCloudList>();
                    myVTK->AABBOrignalPosAxis(cloudData.layer.points);   //将DBTree有效的点云发送给VTK
                    //myVTK->displayAABBOrignalPosAxis(true);       //是否显示AABB最小的坐标创建坐标轴
                }
                else if (currentItem->checkState() == Qt::Unchecked)
                {
                    myVTK->displayAABBOrignalPosAxis(false);        //是否显示AABB最小的坐标创建坐标轴
                }
            }
        }

    else if(!myVTK->isActiveDlgClip)
    {
        myVTK->displayAABBOrignalPosAxis(false);        //是否显示AABB最小的坐标创建坐标轴
    }
}

//DB Tree  发生变化
void MainWindow::slotOn_treeItemChanged(QStandardItem * item)
{

    if (item == nullptr)        return;

    const QVariant layerData = item->data(Qt::UserRole + 2);
    if (!item->hasChildren() && layerData.isValid())
    {
        MyCloudList cloud = layerData.value<MyCloudList>();
        const QString normalizedName = item->text().trimmed();
        if (normalizedName.isEmpty())
        {
            const QSignalBlocker blocker(model);
            item->setText(cloud.layer.name);
        }
        else
        {
            const bool visible = item->checkState() == Qt::Checked;
            if (cloud.layer.name != normalizedName
                || cloud.layer.visible != visible)
            {
                cloud.layer.name = normalizedName;
                cloud.layer.visible = visible;
                ++cloud.layer.revision;
                updateCloudData(cloud);
                markProjectDirty();
            }
        }
    }

    QModelIndex index = ui.treeView->currentIndex();
    QStandardItem * currentItem = model->itemFromIndex(index);
    //qDebug() <<" currentItem->type"<< currentItem->type();

    if (item->isCheckable())
    {
        if (item->hasChildren())
        {
            if (item->checkState() == Qt::Checked)
            {

                for (int i = 0; i < item->rowCount(); i++)
                {
                    item->child(i)->setCheckState(Qt::Checked);
                }
            }
            else if (item->checkState() == Qt::Unchecked)
            {
                for (int i = 0; i < item->rowCount(); i++)
                {
                    item->child(i)->setCheckState(Qt::Unchecked);
                }
            }

        }
        else if (!item->hasChildren())
        {
            QStandardItem * parentItem = item->parent();
                if (parentItem == nullptr)  return;

            if (parentItem->isCheckable())
            {
                //if (parentItem->checkState() == Qt::Unchecked)        parentItem->setCheckState(Qt::PartiallyChecked);
                int isAllTure = 0;
                int isAllFalse = 0;

                for (size_t i = 0; i < parentItem->rowCount(); i++)
                {
                    if (parentItem->child(i)->checkState() == Qt::Checked)  isAllTure++;
                    if (parentItem->child(i)->checkState() == Qt::Unchecked)    isAllFalse++;
                }

                if (isAllTure != 0)
                {
                    if (isAllTure == parentItem->rowCount())    parentItem->setCheckState(Qt::Checked);
                    else(parentItem->setCheckState(Qt::PartiallyChecked));
                }
                if (isAllFalse == parentItem->rowCount())
                {
                    parentItem->setCheckState(Qt::Unchecked);
                }
            }
        }
    }

}

//DB Tree 删除项
void MainWindow::slotOn_actDelete_triggered()
{
    QModelIndex index = ui.treeView->currentIndex();
    QStandardItem * currentItem = model->itemFromIndex(index);
    bool removedItem = false;

    //if (parentItem == nullptr)    return;
    if (!model->hasChildren())
    {
        return;
    }
    else if (!currentItem->hasChildren() && currentItem->parent() == 0)
    {
        model->removeRow(currentItem->index().row());
        removedItem = true;

    }
    else if (currentItem->hasChildren())        //删除点云文件夹
    {

        MyCloudList childData = currentItem->child(0)->data(Qt::UserRole + 2).value<MyCloudList>();
        removeCloudFromWorkspace(childData);

        while (currentItem->rowCount() != 0)
        {
            currentItem->removeRow(0);
        }
        model->removeRow(currentItem->index().row());
        removedItem = true;

    }
    else if (currentItem->parent() != nullptr)      //删除点云
    {

        MyCloudList myCloudData = currentItem->data(Qt::UserRole + 2).value<MyCloudList>();
        removeCloudFromWorkspace(myCloudData);

        QStandardItem * parentItem = currentItem->parent();
        parentItem->removeRow(currentItem->row());
        removedItem = true;

    }

    if (!model->hasChildren())      //判断删除后DB Tree是否有其他项目
    {
        ui.actDelete->setEnabled(false);
        ui.actAABB->setEnabled(false);
        ui.actAABB->setText("关闭包围盒");

        ui.actRandomPlane->setEnabled(false);
        ui.actVerticalPlane->setEnabled(false);
        ui.actHorizonalPlane->setEnabled(false);

        isAABB = false;
    }
    if (removedItem)
        markProjectDirty();
    updateCloudToolActions();
}

//开启AABB按钮
void MainWindow::slotOn_actAABB_triggered(bool checked)
{
    QModelIndex index = ui.treeView->currentIndex();
    QStandardItem * currentItem = model->itemFromIndex(index);
    MyCloudList cloudData = currentItem->data(Qt::UserRole + 2).value<MyCloudList>();

    if (checked)
    {
        ui.actAABB->setText("显示包围盒");
        isAABB = true;
        qDebug() << "显示包围盒";

        if (currentItem->isCheckable())
        {
            if (currentItem->checkState() == Qt::Checked)
            {
                if (!currentItem->hasChildren())        myVTK->display(cloudData.AABBactor);
                else
                {
                    //MyCloudList childData = currentItem->child(0)->data(Qt::UserRole + 2).value<MyCloudList>();
                    //myVTK->display(childData.AABBactor);
                    //ui.actAABB->setEnabled(false);
                }
            }
            else if (currentItem->checkState() == Qt::Unchecked)
            {
                if (!currentItem->hasChildren())        myVTK->removeAABBDisplay(cloudData.AABBactor);
            }

        }
    }
    else if(!checked)
    {
        ui.actAABB->setText("关闭包围盒");
        isAABB = false;
        qDebug() << "关闭包围盒";

        if (currentItem->isCheckable())
        {
            if (!currentItem->hasChildren())
            {
                myVTK->removeAABBDisplay(cloudData.AABBactor);
            }
            else
            {
                //MyCloudList childData = currentItem->child(0)->data(Qt::UserRole + 2).value<MyCloudList>();
                //myVTK->display(childData.AABBactor);
                //ui.actAABB->setEnabled(false);
            }
        }
    }

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
    QModelIndex index = ui.treeView->currentIndex();
    QStandardItem * currentItem = model->itemFromIndex(index);
    if (currentItem == nullptr  || currentItem->hasChildren())      return;
    MyCloudList cloudData = currentItem->data(Qt::UserRole + 2).value<MyCloudList>();
    myVTK->getDBItemCloud(cloudData.layer.points);

}

//将DBTree下的Item点云送到GraphicView
void MainWindow::DBTreeSendGraphicViewItemCloud()
{
    QModelIndex index = ui.treeView->currentIndex();
    QStandardItem * currentItem = model->itemFromIndex(index);
    if (currentItem == nullptr || currentItem->hasChildren())       return;
    MyCloudList cloudData = currentItem->data(Qt::UserRole + 2).value<MyCloudList>();
    ui.graphicsView->getDBItemCloud(cloudData.layer.points);
}

//设置DBTree item点云与切割按钮的禁用与否
void MainWindow::setClipButtonEnable(const QModelIndex & index)
{
    QStandardItem * currentItem = model->itemFromIndex(index);

    //判断当前Item是否能进行切割
    if (currentItem == nullptr)     return;

    if (currentItem->checkState() == Qt::Checked)
    {
        if (myVTK->isActiveDlgClip)
        {
            ui.actHorizonalPlane->setEnabled(false);
            ui.actRandomPlane->setEnabled(false);
            ui.actVerticalPlane->setEnabled(false);
        }
        else if (!myVTK->isActiveDlgClip)
        {
            if (!currentItem->hasChildren())
            {
                //dlgClip->setClipButtonEnable(true);
                ui.actHorizonalPlane->setEnabled(true);
                ui.actRandomPlane->setEnabled(true);
                ui.actVerticalPlane->setEnabled(true);
            }
            else if (currentItem->hasChildren())
            {
                //dlgClip->setClipButtonEnable(false);
                ui.actHorizonalPlane->setEnabled(false);
                ui.actRandomPlane->setEnabled(false);
                ui.actVerticalPlane->setEnabled(false);
            }
        }
    }
    else if (currentItem->checkState() == Qt::Unchecked)
    {
        ui.actHorizonalPlane->setEnabled(false);
        ui.actRandomPlane->setEnabled(false);
        ui.actVerticalPlane->setEnabled(false);
    }
}

//设置切割对话框中的切割按键禁用与否
void MainWindow::setDlgClipPbnEnable(const QModelIndex & index)
{
    //qDebug() << "myVTK->isActiveDlgClip" << myVTK->isActiveDlgClip;
    QStandardItem * currentItem = model->itemFromIndex(index);

    if (currentItem == nullptr)     return;
    if (currentItem->checkState() == Qt::Checked)
    {
        if (myVTK->isActiveDlgClip)
        {
            if (!currentItem->hasChildren())
            {

                emit sendDlgClipPbnEnable(true);
            }
            else
            {
                emit sendDlgClipPbnEnable(false);
            }
        }

    }
    else if (currentItem->checkState() == Qt::Unchecked)
    {
        if (myVTK->isActiveDlgClip)     emit sendDlgClipPbnEnable(false);

    }

    if (myVTK->isActiveDlgClip)
    {
        ui.actDelete->setEnabled(false);
    }
}

//设置投影连线按钮能否获得
void MainWindow::setActProjLineEnable(bool enable)
{
    ui.actProjLine->setEnabled(enable);
}

//设置投影到XOY按钮能否获得
void MainWindow::setActProjXOYEnable(bool enable)
{
    ui.actProjXOY->setEnabled(enable);
}

//设置投影到XOZ按钮能否获得
void MainWindow::setActProjXOZEnable(bool enable)
{
    ui.actProjXOZ->setEnabled(enable);
}

//设置投影到YOZ按钮能否获得
void MainWindow::setActProjYOZEnable(bool enable)
{
    ui.actProjYOZ->setEnabled(enable);
}

// 设置俯视投影按钮能否获得
void MainWindow::setActOverLookProjEnable(bool enable)
{
    ui.actOverLookProj->setEnabled(enable);
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
