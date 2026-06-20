/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: V1.0
 * Description: 程序入口 — VTK/OpenGL 初始化
 *****************************************************************/

#include "MainWindow.h"
#include "MyVTK.h"

#include <QtWidgets/QApplication>

#include <vtkOutputWindow.h>
int main(int argc, char *argv[])
{
    vtkOutputWindow::SetGlobalWarningDisplay(0);
#if VTK_MAJOR_VERSION >= 9
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
#endif
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
