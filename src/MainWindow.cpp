/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 主窗口 — UI 编排、点云加载、DB Tree 管理
 *****************************************************************/

#include "MainWindow.h"
#include "FAMPController.h"
#include "CrsService.h"
#include "HelpContent.h"
#include "LasLoader.h"
#include "PcdLoader.h"
#include "RecentFiles.h"

#include <QAction>
#include <QCloseEvent>
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
#include <QPixmap>
#include <QPushButton>
#include <QFileInfo>
#include <QCoreApplication>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QTime>
#include <QTextBrowser>
#include <QTimer>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

#include <pcl/pcl_config.h>
#include <vtkVersion.h>

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
    , autosaveTimer(nullptr)
    , toolsMenu(nullptr)
    , editMenu(nullptr)
    , setProjectCrsAction(nullptr)
    , coordinateConverterAction(nullptr)
    , undoGraphicsAction(nullptr)
    , redoGraphicsAction(nullptr)
    , crsStatusLabel(nullptr)
    , projectDirty(false)
    , loadingProject(false)
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
    connect(scaleCombox, &QComboBox::currentTextChanged,
            this, [this]() { markProjectDirty(); });

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
    connect(setProjectCrsAction, &QAction::triggered,
            this, &MainWindow::slotSetProjectCrs);
    connect(coordinateConverterAction, &QAction::triggered,
            this, &MainWindow::slotOpenCoordinateConverter);

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

    ui.menu_4->insertAction(ui.actOpenCloud, newProjectAction);
    ui.menu_4->insertAction(ui.actOpenCloud, openProjectAction);
    ui.menu_4->insertAction(ui.actOpenCloud, saveProjectAction);
    ui.menu_4->insertAction(ui.actOpenCloud, saveProjectAsAction);
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
}

famp::project::Document MainWindow::currentProjectDocument() const
{
    famp::project::Document document;
    document.mapScale = scaleCombox->currentText();
    document.projectCrs = projectCrs;
    for (int row = 0; row < model->rowCount(); ++row)
    {
        const QStandardItem* projectItem = model->item(row);
        if (!projectItem || projectItem->rowCount() == 0)
            continue;

        const QString path = projectItem->child(0)->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            document.cloudFiles.append(path);
    }
    return document;
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
    if (!famp::project::save(
            path,
            currentProjectDocument(),
            QCoreApplication::applicationVersion(),
            &error))
    {
        QMessageBox::warning(this, tr("保存项目失败"), error);
        return false;
    }

    currentProjectPath = famp::project::pathWithProjectSuffix(path);
    projectDirty = false;
    removeRecoveryProject();
    updateWindowTitle();
    emit sendStr2Console(tr("已保存项目  %1").arg(currentProjectPath));
    return true;
}

bool MainWindow::loadProjectFromPath(const QString& path, bool isRecovery)
{
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

    loadingProject = true;
    clearWorkspace();
    const int scaleIndex = scaleCombox->findText(document.mapScale);
    if (scaleIndex >= 0)
        scaleCombox->setCurrentIndex(scaleIndex);
    projectCrs = document.projectCrs;
    updateCrsStatus();

    QStringList failedFiles;
    for (const QString& cloudPath : document.cloudFiles)
    {
        const QFileInfo cloudInfo(cloudPath);
        if (!cloudInfo.exists() || !cloudInfo.isFile() || !cloudInfo.isReadable())
        {
            failedFiles.append(cloudPath);
            continue;
        }
        if (!openCloudFile(cloudPath))
            failedFiles.append(cloudPath);
    }
    loadingProject = false;

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
    projectDirty = isRecovery || !failedFiles.isEmpty();
    updateWindowTitle();
    emit sendStr2Console(tr("已打开项目  %1").arg(path));

    if (!failedFiles.isEmpty())
    {
        QStringList displayedFiles = failedFiles.mid(0, 10);
        QString details = displayedFiles.join(QLatin1Char('\n'));
        if (failedFiles.size() > displayedFiles.size())
        {
            details += tr("\n…共 %1 个文件未能加载")
                           .arg(failedFiles.size());
        }
        QMessageBox::warning(
            this,
            tr("项目未完全加载"),
            tr("以下点云文件不存在、不可读或无效：\n%1")
                .arg(details));
    }
    return true;
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
    if (!projectDirty)
        return;

    QString error;
    if (!famp::project::save(
            recoveryProjectPath(),
            currentProjectDocument(),
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
            projectCrs.clear();
            updateCrsStatus();
            markProjectDirty();
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
    projectCrs = info.identifier;
    updateCrsStatus();
    markProjectDirty();
    statusBar()->showMessage(
        tr("项目 CRS 已设置为 %1 — %2；已加载点云坐标未被修改。")
            .arg(info.identifier, info.name),
        8000);
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
    int openedCount = 0;
    for (const QString& path : paths)
    {
        if (openCloudFile(path))
            ++openedCount;
    }
    emit sendStr2Console(tr("拖放打开 %1/%2 个点云文件")
                             .arg(openedCount)
                             .arg(paths.size()));
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
        tr("点云文件 (*.pcd *.las);;PCD (*.pcd);;LAS (*.las)"));
    if (!path.isEmpty())
        openCloudFile(path);
}

bool MainWindow::openCloudFile(const QString& requestedPath)
{
    const QString path = famp::recent::normalizedPath(requestedPath);
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile())
    {
        QMessageBox::warning(
            this, tr("无法打开点云"), tr("文件不存在：\n%1").arg(path));
        return false;
    }
    if (!fileInfo.isReadable())
    {
        QMessageBox::warning(
            this, tr("无法打开点云"), tr("文件不可读：\n%1").arg(path));
        return false;
    }
    if (!famp::recent::isSupportedCloudFile(path))
    {
        QMessageBox::warning(
            this,
            tr("不支持的文件"),
            tr("仅支持 PCD 和 LAS 点云文件：\n%1").arg(path));
        return false;
    }

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr loadedPoints(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    const QString fileSuffix = fileInfo.suffix().toLower();

    //读取点云
    if (fileSuffix == "pcd")
    {
        QString loadError;
        if (!loadPcdAsRgb(path, loadedPoints, &loadError))
        {
            QMessageBox::warning(this, tr("无法打开点云"), loadError);
            return false;
        }
        if (loadedPoints->empty())
        {
            QMessageBox::warning(
                this, tr("无法打开点云"), tr("点云不包含可用点：\n%1").arg(path));
            return false;
        }

        std::unique_ptr<Cloud> loadedCloud(new Cloud(loadedPoints));
        loadedPoints = loadedCloud->computeDecentrationCloud();
        if (!loadedPoints || loadedPoints->empty())
        {
            QMessageBox::warning(
                this, tr("无法打开点云"), tr("点云不包含可用点：\n%1").arg(path));
            return false;
        }

        delete myCloud;
        myCloud = loadedCloud.release();
    }
    else if (fileSuffix == "las")
    {
        QString loadError;
        if (!loadLasAsRgb(path, loadedPoints, &loadError))
        {
            QMessageBox::warning(this, tr("无法打开点云"), loadError);
            return false;
        }
    }

    inCloud = loadedPoints;
    const QString dir = fileInfo.fileName();

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
    pointCloudList.push_back(pointCloud);

    //发送给Console消息
    emit sendStr2Console(tr("打开点云  %1").arg(path));

    /////-------------------DB tree---------------------
    QString str = dir + "(" + path + ")";

    //点云文件夹
    itemProject = new QStandardItem(icon_1, str);
    itemProject->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsEditable );
    itemProject->setCheckState(Qt::Checked);
    itemProject->setData(QVariant(path), Qt::UserRole);
    itemProject->setData(QVariant::fromValue(pointCloud), Qt::UserRole + 2);
    model->appendRow(itemProject);
    //else(currentItem->parent()->appendRow(itemProject));

    //点云文件
    QStandardItem * itemChild = new QStandardItem(icon_2, dir);
    ui.treeView->expand(itemProject->index());      //默认展开该项
    itemChild->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    itemChild->setCheckState(Qt::Checked);
    itemChild->setData(QVariant(path), Qt::UserRole);
    itemChild->setData(QVariant(dir), Qt::UserRole+1);
    itemChild->setData(QVariant::fromValue(pointCloud), Qt::UserRole + 2);
    itemProject->appendRow(itemChild);

    myVTK->display(pointCloudList.back().cloudactor);       //显示点云
    //myVTK->display(pointCloudList.back().AABBactor);     //显示AABB
    myVTK->initCamera();
    myVTK->update();
    ++iCount;

    //设置AABB是否启用
    ui.actAABB->setEnabled(true);
    ui.actAABB->setChecked(false);
    ui.actAABB->setText("关闭包围盒");
    addRecentFile(path);
    markProjectDirty();
    return true;
}

//点击DB Tree项目
void MainWindow::slotOn_treeView_clicked(const QModelIndex & index)
{
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
