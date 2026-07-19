#include "MainWindow.h"
#include "MyGraphicsView.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QTreeView>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>

#include <cmath>

TEST(MainWindowLayoutTest, StartsWithContentLeftAndEqualWorkAreas)
{
    MainWindow window;
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    QDockWidget* content = window.findChild<QDockWidget*>(
        QStringLiteral("dockWidget1"));
    QDockWidget* drafting = window.findChild<QDockWidget*>(
        QStringLiteral("dockWidget2"));
    ASSERT_NE(content, nullptr);
    ASSERT_NE(drafting, nullptr);
    ASSERT_NE(window.centralWidget(), nullptr);
    QAction* calibration = window.findChild<QAction*>(
        QStringLiteral("actCalibrateMetricGrid"));
    QToolBar* draftingTools = window.findChild<QToolBar*>(
        QStringLiteral("GraViewToolBar"));
    ASSERT_NE(calibration, nullptr);
    ASSERT_NE(draftingTools, nullptr);

    EXPECT_EQ(window.dockWidgetArea(content), Qt::LeftDockWidgetArea);
    EXPECT_EQ(window.dockWidgetArea(drafting), Qt::RightDockWidgetArea);
    EXPECT_FALSE(content->isFloating());
    EXPECT_FALSE(drafting->isFloating());
    EXPECT_GE(content->width(), 250);
    EXPECT_LE(content->width(), 350);
    EXPECT_LE(std::abs(window.centralWidget()->width() - drafting->width()),
              20);
    EXPECT_TRUE(draftingTools->actions().contains(calibration));
    EXPECT_TRUE(calibration->isEnabled());
}

TEST(MainWindowLayoutTest, PlacesGeneratedDraftingItemsInsideDraftingGroup)
{
    MainWindow window;
    MyGraphicsView* drafting = window.findChild<MyGraphicsView*>(
        QStringLiteral("graphicsView"));
    QTreeView* content = window.findChild<QTreeView*>(
        QStringLiteral("treeView"));
    ASSERT_NE(drafting, nullptr);
    ASSERT_NE(content, nullptr);

    famp::terrain::ContourLine contour;
    contour.elevation = 10.0;
    contour.points = {{0.0, 0.0}, {1.0, 1.0}};
    QString error;
    ASSERT_TRUE(drafting->addTerrainContours(
        {contour}, 1.0, QStringLiteral("EPSG:3857"),
        QUuid::createUuid().toString(QUuid::WithoutBraces),
        QStringLiteral("大墓坑"), QString(), 1.0, 0.0, &error))
        << error.toStdString();
    ASSERT_TRUE(QMetaObject::invokeMethod(
        drafting, "workspaceItemsChanged", Qt::DirectConnection));

    QAbstractItemModel* treeModel = content->model();
    ASSERT_NE(treeModel, nullptr);
    const QModelIndex project = treeModel->index(0, 0);
    ASSERT_TRUE(project.isValid());

    QModelIndex draftingGroup;
    for (int row = 0; row < treeModel->rowCount(project); ++row)
    {
        const QModelIndex candidate = treeModel->index(row, 0, project);
        if (candidate.data(Qt::DisplayRole).toString()
            == QStringLiteral("二维制图"))
        {
            draftingGroup = candidate;
            break;
        }
    }
    ASSERT_TRUE(draftingGroup.isValid());
    ASSERT_EQ(treeModel->rowCount(draftingGroup), 1);
    EXPECT_EQ(treeModel->index(0, 0, draftingGroup)
                  .data(Qt::DisplayRole).toString(),
              QStringLiteral("二维等高线"));
}

TEST(MainWindowLayoutTest, ShowsPersistentArchaeologyWorkflowNavigator)
{
    MainWindow window;
    QToolBar* workflow = window.findChild<QToolBar*>(
        QStringLiteral("archaeologyWorkflowBar"));
    QLabel* context = window.findChild<QLabel*>(
        QStringLiteral("archaeologyWorkflowContext"));
    ASSERT_NE(workflow, nullptr);
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(window.toolBarArea(workflow), Qt::TopToolBarArea);
    EXPECT_TRUE(context->text().contains(QStringLiteral("尚未选择点云")));

    for (int step = 1; step <= 6; ++step)
    {
        QToolButton* button = window.findChild<QToolButton*>(
            QStringLiteral("archaeologyWorkflowStep%1").arg(step));
        ASSERT_NE(button, nullptr);
        EXPECT_EQ(button->minimumSize(), button->maximumSize());
        EXPECT_EQ(button->font().weight(), QFont::DemiBold);
        if (step > 1)
        {
            QToolButton* first = window.findChild<QToolButton*>(
                QStringLiteral("archaeologyWorkflowStep1"));
            ASSERT_NE(first, nullptr);
            EXPECT_EQ(button->size(), first->size());
        }
    }
    EXPECT_FALSE(context->wordWrap());
    EXPECT_EQ(context->minimumHeight(), context->maximumHeight());
    EXPECT_EQ(context->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
    EXPECT_FALSE(window.findChild<QAction*>(
        QStringLiteral("actProjXOY"))->isEnabled());
    EXPECT_FALSE(window.findChild<QAction*>(
        QStringLiteral("actProjLine"))->isEnabled());
    QToolButton* editAndExport = window.findChild<QToolButton*>(
        QStringLiteral("archaeologyWorkflowStep6"));
    ASSERT_NE(editAndExport, nullptr);
    EXPECT_FALSE(editAndExport->isEnabled());
    EXPECT_FALSE(window.findChild<QAction*>(
        QStringLiteral("actSave"))->isEnabled());
    const QString previewStepText = window.findChild<QToolButton*>(
        QStringLiteral("archaeologyWorkflowStep4"))->text();
    EXPECT_TRUE(previewStepText.contains(QStringLiteral("投影预览")));
    EXPECT_FALSE(previewStepText.contains(QStringLiteral("三向")));
    EXPECT_FALSE(previewStepText.contains(QStringLiteral("三项")));
    EXPECT_TRUE(window.findChild<QToolButton*>(
        QStringLiteral("archaeologyWorkflowStep5"))->text().contains(
            QStringLiteral("三项")));
}
