/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 切割平面对话框
 *****************************************************************/

#pragma once

#include <QDialog>
#include "ui_QDlgClip.h"

class QDlgClip : public QDialog
{
    Q_OBJECT

public:
    QDlgClip(QWidget *parent = Q_NULLPTR);
    ~QDlgClip();

private:
    Ui::QDlgClip ui;

public:
    void closeEvent(QCloseEvent * event);
    void setClipButtonEnable(bool enable);      // 设置开始切割按钮是否能获得
    void setSpinBoxRange(double min, double max);
    void setSpinBoxValue(double value);
    void getSpinBoxValue(double &value);

public slots:
    void slotOn_pBnClipClicked();
    void slotOn_actSave_triggered();

};
