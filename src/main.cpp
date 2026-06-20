#include "MainWindow.h"
#include <QtWidgets/QApplication>
#include"MyVTK.h"
VTK_MODULE_INIT(vtkRenderingOpenGL2);
VTK_MODULE_INIT(vtkInteractionStyle);
VTK_MODULE_INIT(vtkRenderingFreeType);

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
