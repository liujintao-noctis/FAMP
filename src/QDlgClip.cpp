#include "QDlgClip.h"
#include"MainWindow.h"
#include"MyVTK.h"
QDlgClip::QDlgClip(QWidget *parent)
	: QDialog(parent)
{
	ui.setupUi(this);
	this->setWindowTitle("切割平面");
	ui.doubleSpinBox->setValue(0.01);
	ui.doubleSpinBox->setRange(0.00000, 1000000.0);
	ui.doubleSpinBox->setDecimals(3);

	QString pBn_assiantInf = "距离阈值(m)，点云集的点到该平面的距离，若小于设定的阈值，则认定该点在平面上";
	ui.labDisThreshold->setToolTip(pBn_assiantInf);

	

	connect(ui.pBnClip, SIGNAL(clicked()), this, SLOT(slotOn_pBnClipClicked()));
	connect(ui.pBnSave, SIGNAL(clicked()), this, SLOT(slotOn_actSave_triggered()));
}

QDlgClip::~QDlgClip()
{
}

void QDlgClip::closeEvent(QCloseEvent * event)
{
	MyVTK * parWin = (MyVTK*)parentWidget();
	parWin->endCutRemoveActors();
	parWin->cloud_cut_plane = NULL;
	parWin->isActiveDlgClip = false;
	parWin->isProjectionSuccess = false;
	parWin->isClipSucessed = false;
	parWin->triggeredSignalProj();
	parWin->triggeredSignalXOYProj();
	parWin->triggeredSignalXOZProj();
	parWin->triggeredSignalYOZProj();
	parWin->triggeredSignalOverLookProj();
	parWin->displayAABBOrignalPosAxis(false);		//关闭AABB最小的坐标创建坐标轴
	parWin->setQDlgClipNULL();
	
}

// 设置开始切割按钮是否能获得
 void QDlgClip::setClipButtonEnable(bool enable)
{
	ui.pBnClip->setEnabled(enable);
}

 void QDlgClip::setSpinBoxRange(double min, double max)
 {
	 ui.doubleSpinBox->setRange(min, max);
 }

 void QDlgClip::setSpinBoxValue(double value)
 {
	 ui.doubleSpinBox->setValue(value);
 }

 void QDlgClip::getSpinBoxValue(double &value)
 {
	 value = ui.doubleSpinBox->value();
 }

 void QDlgClip::slotOn_actSave_triggered()
 {
	 MyVTK * parWin = (MyVTK*)parentWidget();
	 parWin->saveDlgCloudFile();
 }


void QDlgClip::slotOn_pBnClipClicked()
{
	MyVTK * parWin = (MyVTK*)parentWidget();
	parWin->beginClipPlane();
}