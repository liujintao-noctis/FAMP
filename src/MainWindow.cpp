/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 主窗口 — UI 编排、点云加载、DB Tree 管理
 *****************************************************************/

#include "MainWindow.h"
#include "ArchaeologyReport.h"
#include "FAMPController.h"
#include "CrsService.h"
#include "CloudDisplaySettings.h"
#include "CloudCrop.h"
#include "CloudProcessing.h"
#include "CloudRegistration.h"
#include "FileIO.h"
#include "HelpContent.h"
#include "LasLoader.h"
#include "PcdLoader.h"
#include "ProcessingRecipe.h"
#include "RecentFiles.h"

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
#include <memory>

Q_DECLARE_METATYPE(MyCloudList)

static int iCount = 0;      //记录点云的ID号
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
        if (!cloudLoadBusy || !cloudLoadCancellation)
            return;
        cloudLoadCancelled = true;
        cloudLoadCancellation->store(true, std::memory_order_relaxed);
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
    connect(setProjectCrsAction, &QAction::triggered,
            this, &MainWindow::slotSetProjectCrs);
    connect(coordinateConverterAction, &QAction::triggered,
            this, &MainWindow::slotOpenCoordinateConverter);
    connect(cloudCoordinateAction, &QAction::triggered,
            this, &MainWindow::slotOpenCloudCoordinateViewer);

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
            ui.graphicsView, [this](bool checked) {
                if (checked)
                    ui.graphicsView->startDistanceMeasurement();
            });
    connect(areaMeasureAction, &QAction::triggered,
            ui.graphicsView, [this](bool checked) {
                if (checked)
                    ui.graphicsView->startAreaMeasurement();
            });
    connect(angleMeasureAction, &QAction::triggered,
            ui.graphicsView, [this](bool checked) {
                if (checked)
                    ui.graphicsView->startAngleMeasurement();
            });
    connect(clearMeasurementsAction, &QAction::triggered,
            ui.graphicsView, &MyGraphicsView::clearMeasurements);
    connect(ui.graphicsView, &MyGraphicsView::measurementModeEnded,
            this, [this]() {
                if (QAction* checked = measurementActionGroup->checkedAction())
                    checked->setChecked(false);
            });
    connect(ui.graphicsView, &MyGraphicsView::measurementStatus,
            this, [this](const QString& message) {
                statusBar()->showMessage(message, 8000);
                emit sendStr2Console(message);
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
    for (int row = 0; row < model->rowCount(); ++row)
    {
        const QStandardItem* projectItem = model->item(row);
        if (!projectItem || projectItem->rowCount() == 0)
            continue;
        const QStandardItem* cloudItem = projectItem->child(0);
        const MyCloudList cloud = cloudItem->data(Qt::UserRole + 2)
                                      .value<MyCloudList>();
        famp::report::CloudEntry entry;
        entry.path = cloudItem->data(Qt::UserRole).toString();
        entry.pointCount = cloud.input_cloud ? cloud.input_cloud->size() : 0;
        entry.visible = cloudItem->checkState() == Qt::Checked;
        entry.spatial = cloud.spatial;
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
            reference.visible = projectItem->child(0)->checkState()
                == Qt::Checked;
            reference.spatial = cloud.spatial;
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
            tr("点云文件 (*.pcd *.las *.ply *.xyz);;PCD (*.pcd);;LAS (*.las);;PLY (*.ply);;XYZ (*.xyz)"));
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
        myVTK->removeCloudDisplay(cloud.cloudactor);
        cloud.cloudactor->Delete();
    }
    if (cloud.AABBactor)
    {
        myVTK->removeCloudDisplay(cloud.AABBactor);
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
    if (!cloud.input_cloud || cloud.input_cloud->empty() || !cloud.cloudactor)
        return false;

    if (path)
        *path = item->data(Qt::UserRole).toString();
    return true;
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
            .arg(cloud.spatial.origin[0], 0, 'g', 15)
            .arg(cloud.spatial.origin[1], 0, 'g', 15)
            .arg(cloud.spatial.origin[2], 0, 'g', 15),
        &dialog);
    QLabel crsLabel(projectCrs.isEmpty() ? tr("未声明") : projectCrs, &dialog);
    layout.addRow(tr("点云"), &sourceLabel);
    layout.addRow(tr("原始坐标原点"), &originLabel);
    layout.addRow(tr("项目 CRS"), &crsLabel);

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
                cloud.spatial,
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
                cloud.spatial,
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
    pointSize.setValue(actor->GetProperty()->GetPointSize());

    QDoubleSpinBox opacity(&dialog);
    opacity.setRange(0.05, 1.0);
    opacity.setDecimals(2);
    opacity.setSingleStep(0.05);
    opacity.setValue(actor->GetProperty()->GetOpacity());

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
    if (!mapper->GetScalarVisibility())
        colorMode.setCurrentIndex(1);
    else if (mapper->GetColorMode() == VTK_COLOR_MODE_MAP_SCALARS)
        colorMode.setCurrentIndex(2);
    else
        colorMode.setCurrentIndex(0);

    const double* vtkColor = actor->GetProperty()->GetColor();
    QColor selectedColor = QColor::fromRgbF(
        vtkColor[0], vtkColor[1], vtkColor[2]);
    QPushButton colorButton(tr("选择颜色…"), &dialog);
    QCheckBox automaticRange(tr("自动使用点云高程范围"), &dialog);
    automaticRange.setChecked(true);
    QDoubleSpinBox scalarMinimum(&dialog);
    QDoubleSpinBox scalarMaximum(&dialog);
    for (QDoubleSpinBox* spinBox : {&scalarMinimum, &scalarMaximum})
    {
        spinBox->setRange(-1.0e9, 1.0e9);
        spinBox->setDecimals(6);
        spinBox->setSuffix(tr(" m"));
    }
    double elevationMinimum = 0.0;
    double elevationMaximum = 1.0;
    if (famp::display::elevationRange(
            actor, elevationMinimum, elevationMaximum, nullptr))
    {
        scalarMinimum.setValue(elevationMinimum);
        scalarMaximum.setValue(elevationMaximum);
    }
    auto updateColorButton = [&]() {
        colorButton.setStyleSheet(
            QStringLiteral("background-color: %1;").arg(selectedColor.name()));
    };
    updateColorButton();
    auto updateColorControls = [&]() {
        const auto mode = static_cast<famp::display::ColorMode>(
            colorMode.currentData().toInt());
        const bool elevation = mode == famp::display::ColorMode::Elevation;
        colorButton.setEnabled(mode == famp::display::ColorMode::Uniform);
        automaticRange.setEnabled(elevation);
        scalarMinimum.setEnabled(elevation && !automaticRange.isChecked());
        scalarMaximum.setEnabled(elevation && !automaticRange.isChecked());
    };
    connect(&colorMode, qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog, [&](int) { updateColorControls(); });
    connect(&automaticRange, &QCheckBox::toggled,
            &dialog, [&](bool) { updateColorControls(); });
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
    layout.addRow(QString(), &automaticRange);
    layout.addRow(tr("色带最小高程"), &scalarMinimum);
    layout.addRow(tr("色带最大高程"), &scalarMaximum);
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
    QString error;
    if (!famp::display::apply(actor, settings, &error))
    {
        QMessageBox::warning(this, tr("点云显示设置"), error);
        return;
    }
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
    const std::size_t availableNeighbors = cloud.input_cloud->size() > 1
        ? cloud.input_cloud->size() - 1
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
                recipe.processing, cloud.input_cloud->size(), &recipeError))
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
                current, cloud.input_cloud->size(), &recipeError))
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
            options, cloud.input_cloud->size(), &validationError))
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
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr input = cloud.input_cloud;
    const famp::cloud::SpatialReference spatial = cloud.spatial;
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
    loaded.spatial = cloud.spatial;
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
    if (!famp::crop::dataBounds(cloud.input_cloud, options, &error))
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
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr input = cloud.input_cloud;
    const famp::cloud::SpatialReference spatial = cloud.spatial;
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
    loaded.spatial = cloud.spatial;
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
        if (!candidate.input_cloud || candidate.input_cloud->empty())
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
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr sourceCloud = source.cloud.input_cloud;
    const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr targetCloud = target.cloud.input_cloud;
    const famp::cloud::SpatialReference targetSpatial = target.cloud.spatial;
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
    loaded.spatial = target.cloud.spatial;
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
        tr("点云文件 (*.pcd *.las *.ply *.xyz);;PCD (*.pcd);;LAS (*.las);;PLY (*.ply);;XYZ (*.xyz)"));
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
    cloudLoadCancellation = std::make_shared<std::atomic_bool>(false);

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
    const auto cancellation = cloudLoadCancellation;
    cloudLoadWatcher->setFuture(QtConcurrent::run([path, cancellation]() {
        return famp::cloud::load(path, [cancellation]() {
            return cancellation->load(std::memory_order_relaxed);
        });
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
    pointCloud.input_cloud = inCloud;
    pointCloud.id = iCount;
    pointCloud.cloudactor = cloud_actor;
    pointCloud.AABBactor = AABB_actor;
    pointCloud.spatial = result.spatial;
    bool cloudVisible = true;
    const auto storedReference = projectCloudReferences.constFind(result.path);
    if (storedReference != projectCloudReferences.cend())
    {
        pointCloud.spatial = storedReference->spatial;
        cloudVisible = storedReference->visible;
    }
    vtkNew<vtkMatrix4x4> cloudTransform;
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            cloudTransform->SetElement(
                row, column,
                pointCloud.spatial.transform[static_cast<std::size_t>(
                    row * 4 + column)]);
        }
    }
    pointCloud.cloudactor->SetUserMatrix(cloudTransform);
    pointCloud.AABBactor->SetUserMatrix(cloudTransform);
    pointCloudList.push_back(pointCloud);

    //发送给Console消息
    emit sendStr2Console(tr("打开点云  %1").arg(result.path));

    /////-------------------DB tree---------------------
    QString str = dir + "(" + result.path + ")";

    //点云文件夹
    itemProject = new QStandardItem(icon_1, str);
    itemProject->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsEditable );
    itemProject->setCheckState(cloudVisible ? Qt::Checked : Qt::Unchecked);
    itemProject->setData(QVariant(result.path), Qt::UserRole);
    itemProject->setData(QVariant::fromValue(pointCloud), Qt::UserRole + 2);
    model->appendRow(itemProject);
    //else(currentItem->parent()->appendRow(itemProject));

    //点云文件
    QStandardItem * itemChild = new QStandardItem(icon_2, dir);
    ui.treeView->expand(itemProject->index());      //默认展开该项
    itemChild->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    itemChild->setCheckState(cloudVisible ? Qt::Checked : Qt::Unchecked);
    itemChild->setData(QVariant(result.path), Qt::UserRole);
    itemChild->setData(QVariant(dir), Qt::UserRole+1);
    itemChild->setData(QVariant::fromValue(pointCloud), Qt::UserRole + 2);
    itemProject->appendRow(itemChild);

    if (cloudVisible)
        myVTK->display(pointCloudList.back().cloudactor);       //显示点云
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

    cloudLoadBusy = false;
    cloudLoadProjectBatch = false;
    cloudLoadProjectRecovery = false;
    cloudLoadCancelled = false;
    cloudLoadCancellation.reset();
    projectCloudReferences.clear();
    pendingCloudFiles.clear();
    currentCloudLoadPath.clear();
    setCloudLoadUiBusy(false);

    if (projectBatch)
    {
        loadingProject = false;
        projectDirty = projectRecovery || cancelled || !failurePaths.isEmpty();
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
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr  itemCloud = cloudData.input_cloud;
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
                    myVTK->AABBOrignalPosAxis(cloudData.input_cloud);   //将DBTree有效的点云发送给VTK
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
    myVTK->getDBItemCloud(cloudData.input_cloud);

}

//将DBTree下的Item点云送到GraphicView
void MainWindow::DBTreeSendGraphicViewItemCloud()
{
    QModelIndex index = ui.treeView->currentIndex();
    QStandardItem * currentItem = model->itemFromIndex(index);
    if (currentItem == nullptr || currentItem->hasChildren())       return;
    MyCloudList cloudData = currentItem->data(Qt::UserRole + 2).value<MyCloudList>();
    ui.graphicsView->getDBItemCloud(cloudData.input_cloud);
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
