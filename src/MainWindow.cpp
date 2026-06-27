/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: V1.0
 * Description: 主窗口 — UI 编排、点云加载、DB Tree 管理
 *****************************************************************/

#include "MainWindow.h"
#include "FAMPController.h"

#include <QFileDialog>
#include <QPixmap>
#include <QFileInfo>
#include <QCoreApplication>
#include <QMessageBox>
#include <QTime>
#include <QVBoxLayout>

#include <cstdint>
#include <memory>

Q_DECLARE_METATYPE(MyCloudList)

static int iCount = 0;      //记录点云的ID号
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    this->resize(1920, 1080);

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

    // CenterView VTK. Keep the OpenGL widget in a normal central widget; using
    // a QDockWidget as the central widget can overpaint sibling UI on Windows.
    centerDock = new QWidget(this);
    QVBoxLayout* centerLayout = new QVBoxLayout(centerDock);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);
    myVTK = new MyVTK(centerDock);
    centerLayout->addWidget(myVTK);
    this->setCentralWidget(centerDock);

    //this->setCursor(Qt::WaitCursor);

    //ui.statusBar->addWidget(ui.graphicsView->labelScene); //显示坐标信息
    addXOYLabel();                  //在GraphicsView左上方添加XOY坐标

    //添加比例尺
    addScaleWidget();

    // FAMPController 中介者：统一管理所有信号/槽连接
    controller = new FAMPController(this, myVTK, ui.graphicsView, this);
    controller->initializeConnections(ui, model, centerDock, scaleCombox);

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

//将LAS格式转为PCD格式
void MainWindow::las2PCD(const std::string& path, pcl::PointCloud<pcl::PointXYZRGB>::Ptr &outCloud)
{
    //-------------------------- 加载las点云 --------------------------
    LASreadOpener lasLoad;
    std::vector<char> pathBuf(path.begin(), path.end());
    pathBuf.push_back('\0');
    lasLoad.set_file_name(pathBuf.data());
    auto closeLasReader = [](LASreader* reader) {
        if (reader)
        {
            reader->close();
            delete reader;
        }
    };
    std::unique_ptr<LASreader, decltype(closeLasReader)> lasReader(lasLoad.open(), closeLasReader);
    if (!lasReader)
    {
        QMessageBox::warning(this, tr("Error"), tr("Failed to open LAS file: %1").arg(QString::fromStdString(path)));
        return;
    }
    uint32_t ptCount = lasReader->header.number_of_point_records;
    outCloud->points.reserve(static_cast<std::size_t>(ptCount));

    //点云中心
    double center[3];
    center[0] = lasReader->get_min_x() + (lasReader->get_max_x() - lasReader->get_min_x()) / 2.0;
    center[1] = lasReader->get_min_y() + (lasReader->get_max_y() - lasReader->get_min_y()) / 2.0;
    center[2] = lasReader->get_min_z() + (lasReader->get_max_z() - lasReader->get_min_z()) / 2.0;

    //cout << setprecision(16) << "center" << center[0] << "    " << center[1] << " " << center[2] << " " << endl;
    //去中心化

    while (lasReader->read_point())
    {
        double x = lasReader->point.get_x() - center[0];
        double y = lasReader->point.get_y() - center[1];
        double z = lasReader->point.get_z() - center[2];

        pcl::PointXYZRGB point;
        point.x = x;
        point.y = y;
        point.z = z;
        point.r = static_cast<std::uint8_t>(lasReader->point.get_R() * 255 / 65535);
        point.g = static_cast<std::uint8_t>(lasReader->point.get_G() * 255 / 65535);
        point.b = static_cast<std::uint8_t>(lasReader->point.get_B() * 255 / 65535);

        //cout <<setprecision(16)<< x << "  " << y << " " << z << endl;
        //cout << point << endl;
        outCloud->points.push_back(point);
    }
    outCloud->width = static_cast<std::uint32_t>(outCloud->points.size());
    outCloud->height = 1;
    outCloud->is_dense = false;

    // 调试用：保存转换结果，生产环境需移除
    // pcl::io::savePCDFileBinary("PCD2Las.pcd", *outCloud);
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
    QString filter = "PCD(*.pcd);;LAS(*.las);;所有(*.*)";
    QString path = QFileDialog::getOpenFileName(this, "打开点云文件", QCoreApplication::applicationDirPath(), filter);    //文件路径
    QString dir = QFileInfo(path).fileName();    //文件名
    QFileInfo fileInfo = QFileInfo(path);
    QString fileSuffix = fileInfo.suffix().toLower(); //文件后缀
    //qDebug() << "fileSuffix:" << fileSuffix;

    if (path.isEmpty())     return;

    inCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
    std::string pathPCD = qstr2str(path);   //将Qstring转为String

    //读取点云
    if (fileSuffix == "pcd")
    {
        if (pcl::io::loadPCDFile(pathPCD, *inCloud) == -1)
        {
            QMessageBox::warning(this, tr("Error"), tr("Failed to load PCD file: %1").arg(QString::fromStdString(pathPCD)));
            return;
        }
        delete myCloud;
        myCloud = new Cloud(inCloud);
        inCloud = myCloud->computeDecentrationCloud();
    }
    else if (fileSuffix == "las")
    {
        las2PCD(pathPCD, inCloud);
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
    pointCloudList.push_back(pointCloud);

    //发送给Console消息
    emit sendStr2Console(QString::asprintf("打开点云  %s", path.toStdString().c_str()));

    /////-------------------DB tree---------------------
    QModelIndex index = ui.treeView->currentIndex();
    QStandardItem * currentItem = model->itemFromIndex(index);
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

    //if (parentItem == nullptr)    return;
    if (!model->hasChildren())
    {
        return;
    }
    else if (!currentItem->hasChildren() && currentItem->parent() == 0)
    {
        model->removeRow(currentItem->index().row());

    }
    else if (currentItem->hasChildren())        //删除点云文件夹
    {

        MyCloudList childData = currentItem->child(0)->data(Qt::UserRole + 2).value<MyCloudList>();
        myVTK->removeCloudDisplay(childData.cloudactor);        //移除点云演员
        myVTK->removeCloudDisplay(childData.AABBactor);

        while (currentItem->rowCount() != 0)
        {
            currentItem->removeRow(0);
        }
        model->removeRow(currentItem->index().row());

    }
    else if (currentItem->parent() != nullptr)      //删除点云
    {

        MyCloudList myCloudData = currentItem->data(Qt::UserRole + 2).value<MyCloudList>();
        myVTK->removeCloudDisplay(myCloudData.cloudactor);      //移除点云演员
        myVTK->removeCloudDisplay(myCloudData.AABBactor);

        QStandardItem * parentItem = currentItem->parent();
        parentItem->removeRow(currentItem->row());

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
