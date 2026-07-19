#include "MyGraphicsView.h"
#include "CompassItem.h"
#include "FormTabulationItem.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QPainter>
#include <QTimer>

#include <array>
#include <cmath>

namespace
{
pcl::PointCloud<pcl::PointXYZRGB>::Ptr planarCloud()
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (int index = 0; index < 20; ++index)
    {
        pcl::PointXYZRGB point;
        point.x = static_cast<float>(index % 5);
        point.y = static_cast<float>(index / 5);
        point.z = 0.0F;
        cloud->push_back(point);
    }
    return cloud;
}

pcl::PointCloud<pcl::PointXYZRGB>::Ptr archaeologySurface(
    float width = 4.0F,
    float depth = 3.0F,
    float height = 1.2F)
{
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    constexpr int Columns = 16;
    constexpr int Rows = 12;
    for (int row = 0; row < Rows; ++row)
    {
        for (int column = 0; column < Columns; ++column)
        {
            const float x = width * column / static_cast<float>(Columns - 1);
            const float y = depth * row / static_cast<float>(Rows - 1);
            pcl::PointXYZRGB point;
            point.x = x;
            point.y = y;
            point.z = height * (0.15F
                + 0.45F * x / width
                + 0.30F * y / depth
                + 0.10F * std::sin(x));
            cloud->push_back(point);
        }
    }
    return cloud;
}

pcl::PointCloud<pcl::PointXYZRGB>::Ptr projectedCloud(
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& source,
    famp::projection::Plane plane)
{
    auto projected = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>(*source));
    for (auto& point : projected->points)
    {
        if (plane == famp::projection::Plane::Overlook
            || plane == famp::projection::Plane::XOY)
        {
            point.z = 0.0F;
        }
        else if (plane == famp::projection::Plane::XOZ)
        {
            point.y = 0.0F;
        }
        else if (plane == famp::projection::Plane::YOZ)
        {
            point.x = 0.0F;
        }
    }
    return projected;
}

void createProjectionDrawing(
    MyGraphicsView& view,
    const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& source,
    famp::projection::Plane plane)
{
    QString error;
    // The projected preview is flattened. Keep the selected source cloud as
    // the archaeological reference used to construct the plan section line.
    view.getDBItemCloud(source);
    ASSERT_TRUE(view.setProjectionInput(
        projectedCloud(source, plane), plane, &error))
        << error.toStdString();
    view.slotOn_actProjLine_triggered();
    ASSERT_TRUE(view.hasProjectionDrawing(plane));
}

void expectOrthographicLayout(const MyGraphicsView& view)
{
    const QRectF overlook = view.projectionDrawingSceneBounds(
        famp::projection::Plane::Overlook);
    const QRectF xoz = view.projectionDrawingSceneBounds(
        famp::projection::Plane::XOZ);
    const QRectF yoz = view.projectionDrawingSceneBounds(
        famp::projection::Plane::YOZ);
    ASSERT_TRUE(overlook.isValid());
    ASSERT_TRUE(xoz.isValid());
    ASSERT_TRUE(yoz.isValid());

    EXPECT_LT(yoz.bottom(), overlook.top());
    EXPECT_GT(xoz.left(), overlook.right());
    EXPECT_FALSE(overlook.intersects(xoz));
    EXPECT_FALSE(overlook.intersects(yoz));
    EXPECT_FALSE(xoz.intersects(yoz));

    // The plan, rather than the combined three-view block, is the stable
    // initial anchor. Adding profiles and changing scale must never drag it
    // away from the centre of the drafting canvas.
    EXPECT_NEAR(overlook.center().x(), 0.0, 0.01);
    EXPECT_NEAR(overlook.center().y(), 0.0, 0.01);

    // Each profile is aligned to the corresponding section line drawn over
    // the plan. YOZ shares the cut-line X position above the plan; XOZ shares
    // the cut-line Y position to the right.
    ASSERT_TRUE(view.hasSectionCutLine(famp::projection::Plane::XOZ));
    ASSERT_TRUE(view.hasSectionCutLine(famp::projection::Plane::YOZ));
    const QRectF xozSection = view.sectionCutLineSceneBounds(
        famp::projection::Plane::XOZ);
    const QRectF yozSection = view.sectionCutLineSceneBounds(
        famp::projection::Plane::YOZ);
    ASSERT_TRUE(xozSection.isValid());
    ASSERT_TRUE(yozSection.isValid());
    EXPECT_NEAR(xoz.center().y(), xozSection.center().y(), 0.01);
    EXPECT_NEAR(yoz.center().x(), yozSection.center().x(), 0.01);
    EXPECT_TRUE(overlook.adjusted(-25.0, -25.0, 25.0, 25.0)
                    .contains(xozSection.center()));
    EXPECT_TRUE(overlook.adjusted(-25.0, -25.0, 25.0, 25.0)
                    .contains(yozSection.center()));

    const QRectF sceneRect = view.drawingSceneRect().adjusted(
        -0.5, -0.5, 0.5, 0.5);
    EXPECT_TRUE(sceneRect.contains(overlook));
    EXPECT_TRUE(sceneRect.contains(xoz));
    EXPECT_TRUE(sceneRect.contains(yoz));
    EXPECT_TRUE(sceneRect.contains(view.projectionLayoutSceneBounds()));
}
}

TEST(ProjectionDraftingInputTest, UsesExplicitPlaneInsteadOfGeometryGuessing)
{
    MyGraphicsView view(nullptr);
    const auto cloud = planarCloud();
    QString error;

    ASSERT_TRUE(view.setProjectionInput(
        cloud, famp::projection::Plane::XOY, &error))
        << error.toStdString();
    EXPECT_TRUE(view.hasProjectionInput());
    EXPECT_EQ(view.projectionPlane(), famp::projection::Plane::XOY);

    // XOY and overlook are geometrically identical. The explicit command
    // must retain the intended archaeology workflow branch.
    ASSERT_TRUE(view.setProjectionInput(
        cloud, famp::projection::Plane::Overlook, &error))
        << error.toStdString();
    EXPECT_EQ(view.projectionPlane(), famp::projection::Plane::Overlook);
    EXPECT_EQ(view.projectionDrawingCount(), 0);

    view.clearProjectionInput();
    EXPECT_FALSE(view.hasProjectionInput());
}

TEST(ProjectionDraftingInputTest, BuildsAnOverlookDrawingFromThePreview)
{
    MyGraphicsView view(nullptr);
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (int y = 0; y < 10; ++y)
    {
        for (int x = 0; x < 10; ++x)
        {
            pcl::PointXYZRGB point;
            point.x = static_cast<float>(x) * 0.1F;
            point.y = static_cast<float>(y) * 0.1F;
            point.z = 0.0F;
            cloud->push_back(point);
        }
    }
    QString error;
    ASSERT_TRUE(view.setProjectionInput(
        cloud, famp::projection::Plane::Overlook, &error))
        << error.toStdString();
    int drawingCreatedCount = 0;
    famp::projection::Plane createdPlane = famp::projection::Plane::XOY;
    QObject::connect(
        &view, &MyGraphicsView::projectionDrawingCreated,
        [&drawingCreatedCount, &createdPlane](famp::projection::Plane plane) {
            ++drawingCreatedCount;
            createdPlane = plane;
        });
    view.slotOn_actProjLine_triggered();
    EXPECT_EQ(view.projectionDrawingCount(), 1);
    EXPECT_EQ(drawingCreatedCount, 1);
    EXPECT_EQ(createdPlane, famp::projection::Plane::Overlook);
}

TEST(ProjectionDraftingInputTest,
     KeepsEveryInitialRotationPreviewInsideTheViewport)
{
    MyGraphicsView view(nullptr);
    view.resize(900, 600);
    view.show();
    QApplication::processEvents();

    const std::array<famp::projection::Plane, 3> planes{
        famp::projection::Plane::Overlook,
        famp::projection::Plane::XOZ,
        famp::projection::Plane::YOZ};
    for (const famp::projection::Plane plane : planes)
    {
        const auto source = plane == famp::projection::Plane::YOZ
            ? archaeologySurface(1.0F, 80.0F, 0.08F)
            : archaeologySurface(80.0F, 1.0F, 0.08F);
        QString error;
        ASSERT_TRUE(view.setProjectionInput(
            projectedCloud(source, plane), plane, &error))
            << error.toStdString();

        bool inspected = false;
        bool allContentInside = true;
        QStringList overflowDetails;
        std::array<qreal, 4> previewScales{};
        QTimer inspector;
        inspector.setInterval(1);
        QObject::connect(&inspector, &QTimer::timeout, &view, [&]() {
            QDialog* dialog = view.findChild<QDialog*>(
                QStringLiteral("initialDrawingRotationDialog"));
            if (!dialog || !dialog->isVisible())
                return;

            QComboBox* combo = dialog->findChild<QComboBox*>(
                QStringLiteral("initialDrawingRotationCombo"));
            QGraphicsView* preview = dialog->findChild<QGraphicsView*>(
                QStringLiteral("initialDrawingRotationPreview"));
            if (!combo || !preview || !preview->scene())
            {
                allContentInside = false;
                inspected = true;
                dialog->reject();
                return;
            }

            inspector.stop();
            int rotationIndex = 0;
            for (const int degrees : {0, 90, 180, 270})
            {
                const int index = combo->findData(degrees);
                if (index < 0)
                {
                    allContentInside = false;
                    continue;
                }
                combo->setCurrentIndex(index);
                QApplication::processEvents();
                previewScales.at(rotationIndex++) = std::hypot(
                    preview->transform().m11(),
                    preview->transform().m12());
                const QRectF contentBounds =
                    preview->scene()->itemsBoundingRect();
                const QRectF contentInViewport =
                    preview->mapFromScene(contentBounds).boundingRect();
                const QRectF viewportBounds(preview->viewport()->rect());
                if (!viewportBounds.adjusted(-1.0, -1.0, 1.0, 1.0)
                         .contains(contentInViewport))
                {
                    allContentInside = false;
                    overflowDetails.append(
                        QStringLiteral("%1 deg: content=%2,%3 %4x%5 viewport=%6x%7 scene=%8x%9")
                            .arg(degrees)
                            .arg(contentInViewport.left())
                            .arg(contentInViewport.top())
                            .arg(contentInViewport.width())
                            .arg(contentInViewport.height())
                            .arg(viewportBounds.width())
                            .arg(viewportBounds.height())
                            .arg(preview->sceneRect().width())
                            .arg(preview->sceneRect().height()));
                }
            }
            inspected = true;
            dialog->reject();
        });
        inspector.start();

        EXPECT_FALSE(view.confirmProjectionRotation(&view));
        EXPECT_TRUE(inspected);
        EXPECT_TRUE(allContentInside)
            << famp::projection::axisName(plane).toStdString() << ": "
            << overflowDetails.join(QStringLiteral("; ")).toStdString();
        for (std::size_t index = 1; index < previewScales.size(); ++index)
        {
            EXPECT_NEAR(
                previewScales.at(index), previewScales.front(), 1.0e-9)
                << famp::projection::axisName(plane).toStdString()
                << " rotation index " << index;
        }
    }
}

TEST(ProjectionDraftingInputTest,
     ArrangesThreeViewsAndPreservesTheirLayoutAtEveryScale)
{
    MyGraphicsView view(nullptr);
    const auto cloud = archaeologySurface();
    createProjectionDrawing(view, cloud, famp::projection::Plane::Overlook);
    createProjectionDrawing(view, cloud, famp::projection::Plane::XOZ);
    createProjectionDrawing(view, cloud, famp::projection::Plane::YOZ);
    ASSERT_EQ(view.projectionDrawingCount(), 3);

    std::array<double, 4> normalizedPlanWidths{};
    double referenceVerticalGap = -1.0;
    double referenceHorizontalGap = -1.0;
    const std::array<int, 4> denominators{10, 20, 50, 100};
    for (int scaleIndex = 0; scaleIndex < 4; ++scaleIndex)
    {
        view.getScaleComBoxCurrentIndexChanged(scaleIndex);
        ASSERT_EQ(view.drawingScaleDenominator(), denominators[scaleIndex]);
        expectOrthographicLayout(view);

        const QRectF overlook = view.projectionDrawingSceneBounds(
            famp::projection::Plane::Overlook);
        const QRectF xoz = view.projectionDrawingSceneBounds(
            famp::projection::Plane::XOZ);
        const QRectF yoz = view.projectionDrawingSceneBounds(
            famp::projection::Plane::YOZ);
        // MyItem contributes 15 px interaction padding plus 3 px repaint
        // safety padding on both sides. Removing it leaves the map geometry,
        // which must vary exactly as 1 / denominator.
        normalizedPlanWidths[scaleIndex] =
            (overlook.width() - 36.0) * denominators[scaleIndex];
        const double verticalGap = overlook.top() - yoz.bottom();
        const double horizontalGap = xoz.left() - overlook.right();
        if (scaleIndex == 0)
        {
            referenceVerticalGap = verticalGap;
            referenceHorizontalGap = horizontalGap;
        }
        else
        {
            EXPECT_NEAR(verticalGap, referenceVerticalGap, 0.01);
            EXPECT_NEAR(horizontalGap, referenceHorizontalGap, 0.01);
        }
    }

    for (int index = 1; index < 4; ++index)
    {
        EXPECT_NEAR(normalizedPlanWidths[index], normalizedPlanWidths[0],
                    normalizedPlanWidths[0] * 0.01);
    }
}

TEST(ProjectionDraftingInputTest,
     RestoresCanonicalLayoutWhenOverlookIsGeneratedLast)
{
    MyGraphicsView view(nullptr);
    const auto cloud = archaeologySurface();
    createProjectionDrawing(view, cloud, famp::projection::Plane::XOZ);
    createProjectionDrawing(view, cloud, famp::projection::Plane::YOZ);
    EXPECT_FALSE(view.projectionDrawingSceneBounds(
        famp::projection::Plane::XOZ).intersects(
            view.projectionDrawingSceneBounds(
                famp::projection::Plane::YOZ)));

    createProjectionDrawing(view, cloud, famp::projection::Plane::Overlook);
    expectOrthographicLayout(view);
}

TEST(ProjectionDraftingInputTest,
     KeepsOverlookAtItsInitialAnchorWhileProfilesAreAdded)
{
    MyGraphicsView view(nullptr);
    const auto cloud = archaeologySurface();
    createProjectionDrawing(view, cloud, famp::projection::Plane::Overlook);
    const QRectF initialOverlook = view.projectionDrawingSceneBounds(
        famp::projection::Plane::Overlook);
    ASSERT_TRUE(initialOverlook.isValid());
    EXPECT_NEAR(initialOverlook.center().x(), 0.0, 0.01);
    EXPECT_NEAR(initialOverlook.center().y(), 0.0, 0.01);

    createProjectionDrawing(view, cloud, famp::projection::Plane::XOZ);
    const QRectF afterXoz = view.projectionDrawingSceneBounds(
        famp::projection::Plane::Overlook);
    EXPECT_NEAR(afterXoz.center().x(), initialOverlook.center().x(), 0.01);
    EXPECT_NEAR(afterXoz.center().y(), initialOverlook.center().y(), 0.01);

    createProjectionDrawing(view, cloud, famp::projection::Plane::YOZ);
    const QRectF afterYoz = view.projectionDrawingSceneBounds(
        famp::projection::Plane::Overlook);
    EXPECT_NEAR(afterYoz.center().x(), initialOverlook.center().x(), 0.01);
    EXPECT_NEAR(afterYoz.center().y(), initialOverlook.center().y(), 0.01);
    expectOrthographicLayout(view);
}

TEST(ProjectionDraftingInputTest,
     UsesExactAxisAlignedClipPlaneForThePlanSectionLine)
{
    MyGraphicsView view(nullptr);
    const auto cloud = archaeologySurface();
    createProjectionDrawing(view, cloud, famp::projection::Plane::Overlook);

    view.getDBItemCloud(cloud);
    constexpr float ExactSectionY = 0.4F;
    view.setSectionPlaneReference(
        QVector3D(0.0F, ExactSectionY, 0.0F),
        QVector3D(0.0F, 1.0F, 0.0F));
    QString error;
    ASSERT_TRUE(view.setProjectionInput(
        projectedCloud(cloud, famp::projection::Plane::XOZ),
        famp::projection::Plane::XOZ, &error)) << error.toStdString();
    view.slotOn_actProjLine_triggered();

    const QRectF plan = view.projectionDrawingSceneBounds(
        famp::projection::Plane::Overlook);
    const QPointF planOrigin = view.projectionDrawingSceneOrigin(
        famp::projection::Plane::Overlook);
    const QRectF section = view.sectionCutLineSceneBounds(
        famp::projection::Plane::XOZ);
    ASSERT_TRUE(plan.isValid());
    ASSERT_TRUE(section.isValid());
    const double sceneUnitsPerSourceY =
        (plan.height() - 36.0) / 3.0;
    EXPECT_NEAR(section.center().y() - planOrigin.y(),
                -ExactSectionY * sceneUnitsPerSourceY, 0.01);
}

TEST(ProjectionDraftingInputTest,
     KeepsProfileSidesAndSectionAlignmentAtEveryConfirmedRotation)
{
    MyGraphicsView view(nullptr);
    const auto cloud = archaeologySurface();
    createProjectionDrawing(view, cloud, famp::projection::Plane::Overlook);
    createProjectionDrawing(view, cloud, famp::projection::Plane::XOZ);
    createProjectionDrawing(view, cloud, famp::projection::Plane::YOZ);

    for (const int degrees : {0, 90, 180, 270})
    {
        ASSERT_TRUE(view.setOrthographicRotationDegrees(degrees));
        EXPECT_EQ(view.orthographicRotationDegrees(), degrees);
        const QRectF overlook = view.projectionDrawingSceneBounds(
            famp::projection::Plane::Overlook);
        const QRectF xoz = view.projectionDrawingSceneBounds(
            famp::projection::Plane::XOZ);
        const QRectF yoz = view.projectionDrawingSceneBounds(
            famp::projection::Plane::YOZ);
        ASSERT_TRUE(overlook.isValid());
        ASSERT_TRUE(xoz.isValid());
        ASSERT_TRUE(yoz.isValid());
        EXPECT_NEAR(overlook.center().x(), 0.0, 0.01);
        EXPECT_NEAR(overlook.center().y(), 0.0, 0.01);
        EXPECT_FALSE(overlook.intersects(xoz));
        EXPECT_FALSE(overlook.intersects(yoz));
        EXPECT_FALSE(xoz.intersects(yoz));

        EXPECT_LT(yoz.bottom(), overlook.top());
        EXPECT_GT(xoz.left(), overlook.right());
        const QRectF xozSection = view.sectionCutLineSceneBounds(
            famp::projection::Plane::XOZ);
        const QRectF yozSection = view.sectionCutLineSceneBounds(
            famp::projection::Plane::YOZ);
        ASSERT_TRUE(xozSection.isValid());
        ASSERT_TRUE(yozSection.isValid());
        EXPECT_NEAR(xoz.center().y(), xozSection.center().y(), 0.01);
        EXPECT_NEAR(yoz.center().x(), yozSection.center().x(), 0.01);
    }

    EXPECT_FALSE(view.setOrthographicRotationDegrees(45));
    EXPECT_EQ(view.orthographicRotationDegrees(), 270);

    QString error;
    const QJsonObject saved = view.saveProjectState(&error);
    ASSERT_FALSE(saved.isEmpty()) << error.toStdString();
    MyGraphicsView restored(nullptr);
    ASSERT_TRUE(restored.restoreProjectState(saved, &error))
        << error.toStdString();
    EXPECT_EQ(restored.orthographicRotationDegrees(), 270);
    EXPECT_TRUE(restored.hasSectionCutLine(famp::projection::Plane::XOZ));
    EXPECT_TRUE(restored.hasSectionCutLine(famp::projection::Plane::YOZ));
    restored.getScaleComBoxCurrentIndexChanged(1);
    EXPECT_LT(restored.projectionDrawingSceneBounds(
                  famp::projection::Plane::YOZ).bottom(),
              restored.projectionDrawingSceneBounds(
                  famp::projection::Plane::Overlook).top());
    EXPECT_GT(restored.projectionDrawingSceneBounds(
                  famp::projection::Plane::XOZ).left(),
              restored.projectionDrawingSceneBounds(
                  famp::projection::Plane::Overlook).right());
}

TEST(ProjectionDraftingInputTest,
     KeepsEachConfirmedProfileRotationIndependentAndPersistent)
{
    MyGraphicsView view(nullptr);
    const auto cloud = archaeologySurface();
    createProjectionDrawing(view, cloud, famp::projection::Plane::Overlook);
    createProjectionDrawing(view, cloud, famp::projection::Plane::XOZ);
    createProjectionDrawing(view, cloud, famp::projection::Plane::YOZ);

    ASSERT_TRUE(view.setProjectionRotationDegrees(
        famp::projection::Plane::Overlook, 90));
    const QRectF planBeforeProfileConfirmation =
        view.projectionDrawingSceneBounds(
            famp::projection::Plane::Overlook);
    ASSERT_TRUE(view.setProjectionRotationDegrees(
        famp::projection::Plane::XOZ, 180));
    ASSERT_TRUE(view.setProjectionRotationDegrees(
        famp::projection::Plane::YOZ, 270));

    EXPECT_EQ(view.projectionRotationDegrees(
                  famp::projection::Plane::Overlook),
              90);
    EXPECT_EQ(view.projectionRotationDegrees(
                  famp::projection::Plane::XOZ),
              180);
    EXPECT_EQ(view.projectionRotationDegrees(
                  famp::projection::Plane::YOZ),
              270);
    EXPECT_EQ(view.projectionDrawingSceneBounds(
                  famp::projection::Plane::Overlook),
              planBeforeProfileConfirmation);
    expectOrthographicLayout(view);

    EXPECT_FALSE(view.setProjectionRotationDegrees(
        famp::projection::Plane::XOZ, 45));
    EXPECT_EQ(view.projectionRotationDegrees(
                  famp::projection::Plane::XOZ),
              180);

    QString error;
    const QJsonObject saved = view.saveProjectState(&error);
    ASSERT_FALSE(saved.isEmpty()) << error.toStdString();
    const QJsonObject rotations = saved.value(
        QStringLiteral("projectionRotationDegrees")).toObject();
    EXPECT_EQ(rotations.value(QStringLiteral("overlook")).toInt(), 90);
    EXPECT_EQ(rotations.value(QStringLiteral("xoz")).toInt(), 180);
    EXPECT_EQ(rotations.value(QStringLiteral("yoz")).toInt(), 270);

    MyGraphicsView restored(nullptr);
    ASSERT_TRUE(restored.restoreProjectState(saved, &error))
        << error.toStdString();
    EXPECT_EQ(restored.projectionRotationDegrees(
                  famp::projection::Plane::Overlook),
              90);
    EXPECT_EQ(restored.projectionRotationDegrees(
                  famp::projection::Plane::XOZ),
              180);
    EXPECT_EQ(restored.projectionRotationDegrees(
                  famp::projection::Plane::YOZ),
              270);
    restored.getScaleComBoxCurrentIndexChanged(2);
    expectOrthographicLayout(restored);

    // Projects saved before independent profile directions only contain the
    // legacy shared angle. Restore it for all three drawings so their next
    // scale redraw cannot silently change orientation.
    QJsonObject legacySaved = saved;
    legacySaved.remove(QStringLiteral("projectionRotationDegrees"));
    legacySaved.insert(QStringLiteral("orthographicRotationDegrees"), 180);
    MyGraphicsView legacyRestored(nullptr);
    ASSERT_TRUE(legacyRestored.restoreProjectState(legacySaved, &error))
        << error.toStdString();
    for (const famp::projection::Plane plane : {
             famp::projection::Plane::Overlook,
             famp::projection::Plane::XOZ,
             famp::projection::Plane::YOZ})
    {
        EXPECT_EQ(legacyRestored.projectionRotationDegrees(plane), 180);
    }
    legacyRestored.getScaleComBoxCurrentIndexChanged(3);
    expectOrthographicLayout(legacyRestored);
}

TEST(ProjectionDraftingInputTest,
     SelectedCurvePaintDoesNotScheduleRecursiveSceneInvalidation)
{
    QGraphicsScene scene;
    QVector<QPointF> points{
        QPointF(0.0, 0.0), QPointF(50.0, 20.0), QPointF(100.0, 10.0)};
    auto* curve = new MyItem(points, XOZ);
    scene.addItem(curve);
    curve->setSelected(true);
    auto* compass = new CompassItem();
    scene.addItem(compass);
    compass->setSelected(true);
    auto* form = new FormTabulationItem(
        QStringLiteral("测试员"), QStringLiteral("2026-07-19"),
        QStringLiteral("1:20"), nullptr);
    scene.addItem(form);
    form->setSelected(true);
    QApplication::processEvents();

    int invalidationCount = 0;
    QObject::connect(
        &scene, &QGraphicsScene::changed,
        [&invalidationCount](const QList<QRectF>&) {
            ++invalidationCount;
        });

    QImage frame(320, 200, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::white);
    QPainter painter(&frame);
    scene.render(&painter);
    painter.end();
    QApplication::processEvents();

    // No canvas item may call update() from inside paint(). The old compass
    // and title-block implementations kept the whole scene perpetually dirty,
    // which left visible trails around a transformed selected projection.
    EXPECT_EQ(invalidationCount, 0);
    EXPECT_LE(curve->boundingRect().left(), -18.0);
    EXPECT_GE(curve->boundingRect().right(), 118.0);

    MyGraphicsView view(nullptr);
    EXPECT_EQ(view.cacheMode(), QGraphicsView::CacheNone);
    EXPECT_EQ(view.viewportUpdateMode(),
              QGraphicsView::FullViewportUpdate);
}

TEST(ProjectionDraftingInputTest,
     ExpandsCanvasForLargeScaleDrawingsWithoutOverlapOrImplicitScaling)
{
    MyGraphicsView view(nullptr);
    view.getScaleComBoxCurrentIndexChanged(0);
    const auto cloud = archaeologySurface(25.0F, 18.0F, 5.0F);
    createProjectionDrawing(view, cloud, famp::projection::Plane::Overlook);
    createProjectionDrawing(view, cloud, famp::projection::Plane::XOZ);
    createProjectionDrawing(view, cloud, famp::projection::Plane::YOZ);

    expectOrthographicLayout(view);
    EXPECT_GT(view.drawingSceneRect().width(), 3000.0);
    EXPECT_GT(view.drawingSceneRect().height(), 3000.0);
    EXPECT_GT(view.projectionDrawingSceneBounds(
                  famp::projection::Plane::Overlook).width(),
              9000.0);
}
