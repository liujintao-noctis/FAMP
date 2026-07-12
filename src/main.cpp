/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 程序入口 — VTK/OpenGL 初始化
 *****************************************************************/

#include "MainWindow.h"
#include "MyVTK.h"
#include "Version.h"

#include <QIcon>
#include <QString>
#include <QtWidgets/QApplication>

#include <vtkOutputWindow.h>
int main(int argc, char *argv[])
{
    vtkOutputWindow::SetGlobalWarningDisplay(0);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#ifdef _WIN32
    // The packaged Windows build includes Mesa3D as an OpenGL fallback for
    // non-accelerated VMs. Prefer llvmpipe unless the user explicitly chooses
    // another Gallium driver in their environment.
    if (qgetenv("GALLIUM_DRIVER").isEmpty())
    {
        qputenv("GALLIUM_DRIVER", "llvmpipe");
    }
#endif
#if VTK_MAJOR_VERSION >= 9
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
#endif
    QApplication a(argc, argv);
    a.setOrganizationName(QStringLiteral("FAMP Project"));
    a.setApplicationName(QStringLiteral("FAMP"));
    a.setApplicationDisplayName(QStringLiteral("FAMP"));
    a.setApplicationVersion(QString::fromLatin1(famp::Version));
    a.setWindowIcon(QIcon(":/images/images/icon/famp_icon_256.png"));
#ifdef Q_OS_LINUX
    QApplication::setDesktopFileName("famp");
#endif
    MainWindow w;
    w.show();
    return a.exec();
}
