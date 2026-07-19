#include "MainWindow.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QAbstractItemModel>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <QToolButton>
#include <QUrl>

#include <algorithm>
#include <array>
#include <functional>

#ifndef FAMP_SAMPLE_DIR
#error "FAMP_SAMPLE_DIR must identify the sample cloud directory"
#endif

namespace
{
class DroppableMainWindow : public MainWindow
{
public:
    using MainWindow::MainWindow;
    using MainWindow::dropEvent;
};

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    QApplication::processEvents();
    return predicate();
}

QMessageBox* visibleProjectionPrompt(DroppableMainWindow& window)
{
    const auto prompts = window.findChildren<QMessageBox*>(
        QStringLiteral("projectionCommitPrompt"));
    for (QMessageBox* prompt : prompts)
    {
        if (prompt && prompt->isVisible())
            return prompt;
    }
    return nullptr;
}

QMessageBox* openProjectionDecision(DroppableMainWindow& window,
                                    QAction* projectionAction)
{
    projectionAction->trigger();
    QMessageBox* prompt = nullptr;
    waitUntil([&]() {
        prompt = visibleProjectionPrompt(window);
        return prompt != nullptr;
    }, 1000);
    return prompt;
}

QModelIndex childByName(QAbstractItemModel* model,
                        const QModelIndex& parent,
                        const QString& name)
{
    if (!model)
        return {};
    for (int row = 0; row < model->rowCount(parent); ++row)
    {
        const QModelIndex candidate = model->index(row, 0, parent);
        if (candidate.data(Qt::DisplayRole).toString() == name)
            return candidate;
    }
    return {};
}

QPoint expectedInitialPromptPosition(const DroppableMainWindow& window,
                                     const QMessageBox& prompt)
{
    QPoint expected = window.mapToGlobal(window.rect().center())
        - QPoint(prompt.width() / 2, prompt.height() / 2);
    QScreen* screen = QGuiApplication::screenAt(
        expected + QPoint(prompt.width() / 2, prompt.height() / 2));
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return expected;
    const QRect available = screen->availableGeometry();
    expected.setX(qBound(
        available.left(), expected.x(),
        std::max(available.left(),
                 available.right() - prompt.width() + 1)));
    expected.setY(qBound(
        available.top(), expected.y(),
        std::max(available.top(),
                 available.bottom() - prompt.height() + 1)));
    return expected;
}

void scheduleInitialRotationConfirmation(
    DroppableMainWindow& window,
    int degrees,
    bool& handled)
{
    auto* timer = new QTimer(&window);
    timer->setInterval(1);
    QObject::connect(timer, &QTimer::timeout,
                     &window, [&window, degrees, &handled, timer]() {
        QDialog* dialog = window.findChild<QDialog*>(
            QStringLiteral("initialDrawingRotationDialog"));
        if (!dialog)
            return;
        timer->stop();
        timer->deleteLater();
        QComboBox* combo = dialog->findChild<QComboBox*>(
            QStringLiteral("initialDrawingRotationCombo"));
        QPushButton* confirm = dialog->findChild<QPushButton*>(
            QStringLiteral("initialDrawingRotationConfirmButton"));
        if (!combo || !confirm)
        {
            dialog->reject();
            return;
        }
        const int index = combo->findData(degrees);
        if (index < 0)
        {
            dialog->reject();
            return;
        }
        combo->setCurrentIndex(index);
        handled = true;
        confirm->click();
    });
    timer->start();
}
}

TEST(ProjectionWorkflowUiTest,
     PreviewExistsOnlyWhileDecisionIsOpenAndDoesNotInsertImplicitly)
{
    DroppableMainWindow window;
    window.show();
    QApplication::processEvents();
    QTreeView* tree = window.findChild<QTreeView*>(
        QStringLiteral("treeView"));
    QAction* projectXoy = window.findChild<QAction*>(
        QStringLiteral("actProjXOY"));
    QAction* autoDraw = window.findChild<QAction*>(
        QStringLiteral("actProjLine"));
    QToolButton* editAndExport = window.findChild<QToolButton*>(
        QStringLiteral("archaeologyWorkflowStep6"));
    QAction* a4Export = window.findChild<QAction*>(
        QStringLiteral("actSave"));
    QAction* aabb = window.findChild<QAction*>(
        QStringLiteral("actAABB"));
    MyVTK* viewport = window.findChild<MyVTK*>();
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(projectXoy, nullptr);
    ASSERT_NE(autoDraw, nullptr);
    ASSERT_NE(editAndExport, nullptr);
    ASSERT_NE(a4Export, nullptr);
    ASSERT_NE(aabb, nullptr);
    ASSERT_NE(viewport, nullptr);

    const QString path = QFileInfo(
        QString::fromUtf8(FAMP_SAMPLE_DIR)
        + QStringLiteral("/projectPointCloud.pcd")).absoluteFilePath();
    ASSERT_TRUE(QFileInfo::exists(path));
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    QDropEvent drop(
        QPointF(10.0, 10.0), Qt::CopyAction, &mime,
        Qt::LeftButton, Qt::NoModifier);
    window.dropEvent(&drop);
    EXPECT_TRUE(drop.isAccepted());

    QAbstractItemModel* treeModel = tree->model();
    ASSERT_NE(treeModel, nullptr);
    ASSERT_TRUE(waitUntil([&]() {
        const QModelIndex project = treeModel->index(0, 0);
        return project.isValid() && treeModel->rowCount(project) == 1;
    }, 5000));

    const QModelIndex project = treeModel->index(0, 0);
    const QModelIndex source = treeModel->index(0, 0, project);
    ASSERT_TRUE(source.isValid());
    tree->setCurrentIndex(source);
    EXPECT_FALSE(aabb->isChecked());
    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window, "slotOn_treeView_clicked", Qt::DirectConnection,
        Q_ARG(QModelIndex, source)));
    EXPECT_TRUE(aabb->isEnabled());
    EXPECT_TRUE(aabb->isChecked());
    ASSERT_TRUE(projectXoy->isEnabled());
    EXPECT_FALSE(autoDraw->isEnabled());
    const int originalRows = treeModel->rowCount(project);

    QMessageBox* prompt = openProjectionDecision(window, projectXoy);
    ASSERT_NE(prompt, nullptr);
    EXPECT_LE((prompt->pos()
               - expectedInitialPromptPosition(window, *prompt))
                  .manhattanLength(),
              4);
    EXPECT_FALSE(prompt->isModal());
    EXPECT_EQ(prompt->windowModality(), Qt::NonModal);
    EXPECT_TRUE(window.isEnabled());
    EXPECT_TRUE(viewport->isEnabled());
    EXPECT_TRUE(viewport->hasVisibleProjectionPreview());
    EXPECT_GT(viewport->projectionPreviewPointSize(), 2.0);
    QPushButton* drawPreview = prompt->findChild<QPushButton*>(
        QStringLiteral("projectionAutoDrawButton"));
    QPushButton* addToTreePreview = prompt->findChild<QPushButton*>(
        QStringLiteral("projectionAddToTreeButton"));
    QPushButton* closePreview = prompt->findChild<QPushButton*>(
        QStringLiteral("projectionClosePreviewButton"));
    ASSERT_NE(drawPreview, nullptr);
    ASSERT_NE(addToTreePreview, nullptr);
    ASSERT_NE(closePreview, nullptr);
    EXPECT_EQ(closePreview->text(), QStringLiteral("关闭预览"));
    EXPECT_TRUE(drawPreview->toolTip().contains(QStringLiteral("右侧考古制图画布")));
    EXPECT_TRUE(addToTreePreview->toolTip().contains(QStringLiteral("内存点云")));
    EXPECT_TRUE(closePreview->toolTip().contains(QStringLiteral("临时投影")));
    EXPECT_FALSE(prompt->informativeText().contains(
        QStringLiteral("此窗口不会阻止三维视图操作")));
    EXPECT_FALSE(prompt->informativeText().contains(
        QStringLiteral("“加入内容列表”仅保留")));
    QScreen* screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    const QRect available = screen->availableGeometry();
    const QPoint rememberedPosition(
        available.left() + 24, available.top() + 36);
    prompt->move(rememberedPosition);
    QApplication::processEvents();
    prompt->close();
    ASSERT_TRUE(waitUntil([&]() {
        return !viewport->hasVisibleProjectionPreview()
            && !autoDraw->isEnabled();
    }, 1000));
    EXPECT_EQ(treeModel->rowCount(project), originalRows);
    EXPECT_FALSE(editAndExport->isEnabled());
    EXPECT_FALSE(a4Export->isEnabled());

    // The source remains selected, but closing the window invalidates the
    // transient preview. A new explicit projection can still be committed.
    tree->setCurrentIndex(source);
    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window, "slotOn_treeView_clicked", Qt::DirectConnection,
        Q_ARG(QModelIndex, source)));
    QMessageBox* repeatedPrompt = openProjectionDecision(window, projectXoy);
    ASSERT_NE(repeatedPrompt, nullptr);
    EXPECT_LE((repeatedPrompt->pos() - rememberedPosition).manhattanLength(),
              4);
    QPushButton* addToTree = repeatedPrompt->findChild<QPushButton*>(
        QStringLiteral("projectionAddToTreeButton"));
    ASSERT_NE(addToTree, nullptr);
    addToTree->click();
    ASSERT_TRUE(waitUntil([&]() {
        return treeModel->rowCount(project) == originalRows + 1
            && !viewport->hasVisibleProjectionPreview()
            && !autoDraw->isEnabled();
    }, 1000));
    const QModelIndex derived = treeModel->index(1, 0, project);
    EXPECT_TRUE(derived.data(Qt::DisplayRole).toString().contains(
        QStringLiteral("projected_XOY")));
    EXPECT_EQ(source.data(Qt::CheckStateRole).toInt(),
              static_cast<int>(Qt::Checked));
    EXPECT_FALSE(editAndExport->isEnabled());
    EXPECT_FALSE(a4Export->isEnabled());
}

TEST(ProjectionWorkflowUiTest,
     UnlocksEditingOnlyWhileThreeRequiredDrawingsExistInCanvasAndTree)
{
    DroppableMainWindow window;
    window.show();
    QApplication::processEvents();
    QTreeView* tree = window.findChild<QTreeView*>(
        QStringLiteral("treeView"));
    MyGraphicsView* drafting = window.findChild<MyGraphicsView*>(
        QStringLiteral("graphicsView"));
    QLabel* context = window.findChild<QLabel*>(
        QStringLiteral("archaeologyWorkflowContext"));
    QToolButton* editAndExport = window.findChild<QToolButton*>(
        QStringLiteral("archaeologyWorkflowStep6"));
    QAction* a4Export = window.findChild<QAction*>(
        QStringLiteral("actSave"));
    QAction* aabb = window.findChild<QAction*>(
        QStringLiteral("actAABB"));
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(drafting, nullptr);
    ASSERT_NE(context, nullptr);
    ASSERT_NE(editAndExport, nullptr);
    ASSERT_NE(a4Export, nullptr);
    ASSERT_NE(aabb, nullptr);

    const QString path = QFileInfo(
        QString::fromUtf8(FAMP_SAMPLE_DIR)
        + QStringLiteral("/ordercloud.pcd")).absoluteFilePath();
    ASSERT_TRUE(QFileInfo::exists(path));
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    QDropEvent drop(
        QPointF(10.0, 10.0), Qt::CopyAction, &mime,
        Qt::LeftButton, Qt::NoModifier);
    window.dropEvent(&drop);
    EXPECT_TRUE(drop.isAccepted());

    QAbstractItemModel* treeModel = tree->model();
    ASSERT_NE(treeModel, nullptr);
    ASSERT_TRUE(waitUntil([&]() {
        const QModelIndex project = treeModel->index(0, 0);
        return project.isValid() && treeModel->rowCount(project) == 1;
    }, 5000));
    const QModelIndex project = treeModel->index(0, 0);
    const QModelIndex source = treeModel->index(0, 0, project);
    ASSERT_TRUE(source.isValid());
    tree->setCurrentIndex(source);
    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window, "slotOn_treeView_clicked", Qt::DirectConnection,
        Q_ARG(QModelIndex, source)));

    const QList<QAction*> requiredActions{
        window.findChild<QAction*>(QStringLiteral("actOverLookProj")),
        window.findChild<QAction*>(QStringLiteral("actProjXOZ")),
        window.findChild<QAction*>(QStringLiteral("actProjYOZ"))};
    for (QAction* action : requiredActions)
    {
        ASSERT_NE(action, nullptr);
        ASSERT_TRUE(action->isEnabled());
    }
    EXPECT_FALSE(editAndExport->isEnabled());
    EXPECT_FALSE(a4Export->isEnabled());
    EXPECT_TRUE(context->text().contains(QStringLiteral("俯视")));
    EXPECT_TRUE(context->text().contains(QStringLiteral("XOZ")));
    EXPECT_TRUE(context->text().contains(QStringLiteral("YOZ")));
    EXPECT_TRUE(context->text().contains(QStringLiteral("首要步骤")));
    EXPECT_TRUE(context->text().contains(
        QStringLiteral("【俯视投影二维制图】")));
    EXPECT_TRUE(context->styleSheet().contains(QStringLiteral("#f97316")));

    // Profiles may be previewed first, but automatic drawing is blocked until
    // the plan exists so that every profile has a deterministic section line
    // and alignment anchor.
    QMessageBox* earlyProfilePrompt = openProjectionDecision(
        window, requiredActions.at(1));
    ASSERT_NE(earlyProfilePrompt, nullptr);
    QPushButton* earlyDraw = earlyProfilePrompt->findChild<QPushButton*>(
        QStringLiteral("projectionAutoDrawButton"));
    ASSERT_NE(earlyDraw, nullptr);
    EXPECT_FALSE(earlyDraw->isEnabled());
    EXPECT_TRUE(earlyDraw->toolTip().contains(QStringLiteral("俯视")));
    EXPECT_TRUE(earlyProfilePrompt->informativeText().contains(
        QStringLiteral("请先完成【俯视投影二维制图】")));
    QLabel* planFirstNotice = earlyProfilePrompt->findChild<QLabel*>(
        QStringLiteral("projectionPlanFirstNotice"));
    ASSERT_NE(planFirstNotice, nullptr);
    EXPECT_TRUE(planFirstNotice->styleSheet().contains(
        QStringLiteral("font-weight:800")));
    EXPECT_TRUE(planFirstNotice->styleSheet().contains(
        QStringLiteral("#f97316")));
    QPushButton* closeEarlyPreview =
        earlyProfilePrompt->findChild<QPushButton*>(
            QStringLiteral("projectionClosePreviewButton"));
    ASSERT_NE(closeEarlyPreview, nullptr);
    closeEarlyPreview->click();
    ASSERT_TRUE(waitUntil([&]() {
        return visibleProjectionPrompt(window) == nullptr;
    }, 1000));

    // Step ④ only represents the current preview. Step ⑤ and Step ⑥ depend
    // on the three actual drawings, not on accumulated preview history.
    const std::array<QStringList, 3> expectedTreeItems{
        QStringList{QStringLiteral("俯视投影")},
        QStringList{QStringLiteral("俯视投影"),
                    QStringLiteral("XOZ面投影"),
                    QStringLiteral("XOZ 剖面切割线")},
        QStringList{QStringLiteral("俯视投影"),
                    QStringLiteral("XOZ面投影"),
                    QStringLiteral("XOZ 剖面切割线"),
                    QStringLiteral("YOZ面投影"),
                    QStringLiteral("YOZ 剖面切割线")}};
    const std::array<int, 3> confirmedRotations{90, 180, 270};
    const std::array<famp::projection::Plane, 3> requiredPlanes{
        famp::projection::Plane::Overlook,
        famp::projection::Plane::XOZ,
        famp::projection::Plane::YOZ};
    for (int index = 0; index < requiredActions.size(); ++index)
    {
        const int before = drafting->projectionDrawingCount();
        QMessageBox* prompt = openProjectionDecision(
            window, requiredActions.at(index));
        ASSERT_NE(prompt, nullptr);
        QPushButton* draw = prompt->findChild<QPushButton*>(
            QStringLiteral("projectionAutoDrawButton"));
        ASSERT_NE(draw, nullptr);
        ASSERT_TRUE(draw->isEnabled());
        bool rotationHandled = false;
        scheduleInitialRotationConfirmation(
            window, confirmedRotations.at(index), rotationHandled);
        draw->click();
        EXPECT_TRUE(rotationHandled);
        EXPECT_EQ(drafting->projectionRotationDegrees(
                      requiredPlanes.at(index)),
                  confirmedRotations.at(index));
        ASSERT_TRUE(waitUntil(
            [&]() { return drafting->projectionDrawingCount() > before; },
            3000));
        ASSERT_TRUE(waitUntil([&]() {
            const QModelIndex draftingGroup = childByName(
                treeModel, project, QStringLiteral("二维制图"));
            if (!draftingGroup.isValid())
                return false;
            return std::all_of(
                expectedTreeItems.at(index).cbegin(),
                expectedTreeItems.at(index).cend(),
                [&](const QString& name) {
                    return childByName(treeModel, draftingGroup, name)
                        .isValid();
                });
        }, 3000));
        EXPECT_EQ(editAndExport->isEnabled(),
                  index + 1 == requiredActions.size());
        EXPECT_EQ(a4Export->isEnabled(),
                  index + 1 == requiredActions.size());
    }

    const QModelIndex draftingGroup = childByName(
        treeModel, project, QStringLiteral("二维制图"));
    ASSERT_TRUE(draftingGroup.isValid());
    QStringList drawingNames;
    for (int row = 0; row < treeModel->rowCount(draftingGroup); ++row)
    {
        drawingNames.append(treeModel->index(row, 0, draftingGroup)
                                .data(Qt::DisplayRole).toString());
    }
    EXPECT_TRUE(drawingNames.contains(QStringLiteral("俯视投影")));
    EXPECT_TRUE(drawingNames.contains(QStringLiteral("XOZ面投影")));
    EXPECT_TRUE(drawingNames.contains(QStringLiteral("YOZ面投影")));
    EXPECT_TRUE(drawingNames.contains(QStringLiteral("XOZ 剖面切割线")));
    EXPECT_TRUE(drawingNames.contains(QStringLiteral("YOZ 剖面切割线")));
    EXPECT_TRUE(drafting->hasSectionCutLine(
        famp::projection::Plane::XOZ));
    EXPECT_TRUE(drafting->hasSectionCutLine(
        famp::projection::Plane::YOZ));
    EXPECT_EQ(drafting->projectionRotationDegrees(
                  famp::projection::Plane::Overlook),
              90);
    EXPECT_EQ(drafting->projectionRotationDegrees(
                  famp::projection::Plane::XOZ),
              180);
    EXPECT_EQ(drafting->projectionRotationDegrees(
                  famp::projection::Plane::YOZ),
              270);
    EXPECT_TRUE(context->text().contains(QStringLiteral("第⑥步已解锁")));
    EXPECT_TRUE(editAndExport->toolTip().contains(QStringLiteral("A4")));

    const QModelIndex firstDrawing = treeModel->index(0, 0, draftingGroup);
    ASSERT_TRUE(firstDrawing.isValid());
    tree->setCurrentIndex(firstDrawing);
    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window, "slotOn_treeView_clicked", Qt::DirectConnection,
        Q_ARG(QModelIndex, firstDrawing)));
    EXPECT_FALSE(aabb->isEnabled());
    EXPECT_FALSE(aabb->isChecked());
    EXPECT_TRUE(drafting->hasSelectedItems());

    drafting->slotOn_actClearScene_triggered();
    ASSERT_TRUE(waitUntil([&]() {
        return drafting->projectionDrawingCount() == 0
            && treeModel->rowCount(draftingGroup) == 0
            && !editAndExport->isEnabled() && !a4Export->isEnabled();
    }, 3000));
    EXPECT_TRUE(
        context->text().contains(QStringLiteral("绘图画布缺"))
        || context->text().contains(QStringLiteral("尚未选择点云")));
    EXPECT_TRUE(context->text().contains(QStringLiteral("二维制图"))
                || context->text().contains(QStringLiteral("三项自动绘图")));
}
