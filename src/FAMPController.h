/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: V1.0
 * Description: 中介者控制器 — 统一管理信号/槽连接
 *****************************************************************/

#pragma once

#include <QObject>

class MainWindow;
class MyVTK;
class MyGraphicsView;
class QWidget;
class QStandardItemModel;
class QDockWidget;
class QComboBox;

namespace Ui {
    class MainWindowClass;
}

class FAMPController : public QObject
{
    Q_OBJECT

public:
    FAMPController(MainWindow* mainWindow, MyVTK* myVTK, MyGraphicsView* graphicsView, QObject* parent = nullptr);
    void initializeConnections(const Ui::MainWindowClass& ui, QStandardItemModel* model, QWidget* centerDock, QComboBox* scaleCombox);

private:
    MainWindow* m_mainWindow;
    MyVTK* m_myVTK;
    MyGraphicsView* m_graphicsView;
};
