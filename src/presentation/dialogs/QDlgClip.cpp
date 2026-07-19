/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 切割平面对话框
 *****************************************************************/

#include "QDlgClip.h"
#include "MyVTK.h"

QDlgClip::QDlgClip(QWidget* parent)
    : QDialog(parent)
{
    ui.setupUi(this);
    setWindowTitle(tr("切割平面"));
    ui.doubleSpinBox->setValue(0.01);
    ui.doubleSpinBox->setRange(0.0, 1000000.0);
    ui.doubleSpinBox->setDecimals(3);

    ui.labDisThreshold->setToolTip(
        tr("距离阈值(m)：点到平面的距离不大于该值时，计入切割预览"));
    ui.pBnClip->setToolTip(
        tr("计算并预览切割结果；确认前不会加入内容列表"));

    connect(ui.pBnClip, &QPushButton::clicked,
            this, &QDlgClip::slotOn_pBnClipClicked);
    connect(ui.pBnConfirm, &QPushButton::clicked,
            this, &QDlgClip::slotOn_pBnConfirmClicked);
}

QDlgClip::~QDlgClip()
{
}

void QDlgClip::closeEvent(QCloseEvent* event)
{
    MyVTK* viewport = qobject_cast<MyVTK*>(parentWidget());
    if (viewport)
    {
        viewport->endCutRemoveActors();
        viewport->cloud_cut_plane.reset();
        viewport->isActiveDlgClip = false;
        viewport->isClipSucessed = false;
        viewport->displayAABBOrignalPosAxis(false);
        viewport->setQDlgClipNULL();
    }
    QDialog::closeEvent(event);
}

// 设置开始切割按钮是否能获得
void QDlgClip::setClipButtonEnable(bool enable)
{
    ui.pBnClip->setEnabled(enable);
}

void QDlgClip::setConfirmButtonEnabled(bool enabled)
{
    ui.pBnConfirm->setEnabled(enabled);
}

void QDlgClip::setSpinBoxRange(double min, double max)
{
    ui.doubleSpinBox->setRange(min, max);
}

void QDlgClip::setSpinBoxValue(double value)
{
    ui.doubleSpinBox->setValue(value);
}

void QDlgClip::getSpinBoxValue(double& value)
{
    value = ui.doubleSpinBox->value();
}

void QDlgClip::slotOn_pBnClipClicked()
{
    MyVTK* viewport = qobject_cast<MyVTK*>(parentWidget());
    if (viewport)
        viewport->beginClipPlane();
}

void QDlgClip::slotOn_pBnConfirmClicked()
{
    MyVTK* viewport = qobject_cast<MyVTK*>(parentWidget());
    if (viewport)
        viewport->confirmClipPlane();
}
