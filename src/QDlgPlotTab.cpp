/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: 出图模板对话框
 *****************************************************************/

#include "QDlgPlotTab.h"
#include "MainWindow.h"
#include "MyGraphicsView.h"

QDlgPlotTab::QDlgPlotTab(QWidget *parent)
    : QDialog(parent)
{
    ui.setupUi(this);
    this->setWindowTitle("制图信息");

    connect(ui.enterPBT, SIGNAL(clicked()), this, SLOT(slotOn_enterPBTClick()));

}

QDlgPlotTab::~QDlgPlotTab()
{
}

void QDlgPlotTab::closeEvent(QCloseEvent * event)
{
    MyGraphicsView * parWin = qobject_cast<MyGraphicsView*>(parentWidget());
    if (!parWin) return;
    //parWin->sendDlgClipVisible(true); //打开裁剪对话框
    //qDebug() << "关闭";
    parWin->setDlgPlotTabNull();
}

//确定按钮是否获得
void QDlgPlotTab::setEnterPBTEnable(bool enable)
{
    ui.enterPBT->setEnabled(enable);
}

//获得制图人文字
QString QDlgPlotTab::getDesignerPTE()
{
    QString text = ui.designerLE->text();
    return text;
}

//获得日期文字
QString QDlgPlotTab::getDataPTE()
{
    QString text = ui.dateLE->text();
    return text;

}

//获得比例尺文字
QString QDlgPlotTab::getScalePTE()
{
    QString text = ui.scaleLE->text();
    return text;
}

//设置比例尺默认文字
void QDlgPlotTab::setScalePTEText(const QString & str)
{
    ui.scaleLE->setText(str);
}

//获得当前比例尺索引,并设置默认比例尺文字
void QDlgPlotTab::getCurrentScaleIndex(const int & scale)
{
    currentScaleIndex = scale;
    //qDebug() << "scale" << currentScaleIndex;
    switch (scale)
    {
    case(0):
    {
        setScalePTEText("1:10");
    }
    break;

    case(1):
    {
        setScalePTEText("1:20");
    }
    break;

    case(2):
    {
        setScalePTEText("1:50");
    }
    break;

    case(3):
    {
        setScalePTEText("1:100");
    }
    break;

    default:
        break;
    }
}

//将制图人，比例尺，日期发送给Graphics
//void QDlgPlotTab::sendTextToGraphics()
//{
//  MyGraphicsView * parWin = (MyGraphicsView*)parentWidget();
//  parWin->dataText = getDataPTE();
//  parWin->scaleText = getScalePTE();
//  parWin->designerText = getDesignerPTE();
//}

//点击确认按钮
void QDlgPlotTab::slotOn_enterPBTClick()
{
    MyGraphicsView * parWin = qobject_cast<MyGraphicsView*>(parentWidget());
    if (!parWin) return;
    //parWin->sendGetText();
    parWin->getText();
    parWin->drawFormTable();        //绘制出图表格
    this->close();

}