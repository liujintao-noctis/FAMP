/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 出图模板对话框
 *****************************************************************/

#pragma once

#include <QDialog>
#include "ui_QDlgPlotTab.h"

class QDlgPlotTab : public QDialog
{
    Q_OBJECT

public:
    QDlgPlotTab(QWidget *parent = Q_NULLPTR);
    ~QDlgPlotTab();

private:
    Ui::QDlgPlotTab ui;

    int currentScaleIndex;

public:
    void closeEvent(QCloseEvent * event);
    void setEnterPBTEnable(bool enable);    //确定按钮是否获得
    QString getDesignerPTE();               //获得制图人文字
    QString getDataPTE();               //获得日期文字
    QString getScalePTE();              //获得比例尺文字
    void setScalePTEText(const QString &str);       //设置比例尺默认文字
    void getCurrentScaleIndex(const int &scale);        //获得当前比例尺索引,并设置默认比例尺文字

public slots:
    void slotOn_enterPBTClick();        //点击确认按钮
    //void sendTextToGraphics();            //将制图人，比例尺，日期发送给Graphics
};
