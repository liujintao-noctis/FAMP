#include "MyVTK.h"
#include "QDlgClip.h"

#include <gtest/gtest.h>

#include <QPushButton>

#include <array>

TEST(PlaneClipCommitTest, ReusesDialogAndIgnoresVisibilityWithoutOne)
{
    MyVTK viewport(nullptr);
    EXPECT_NO_FATAL_FAILURE(viewport.setDlgClipVisible(false));

    viewport.setDlgClip();
    QDlgClip* firstDialog = viewport.findChild<QDlgClip*>();
    ASSERT_NE(firstDialog, nullptr);
    viewport.setDlgClipVisible(false);
    EXPECT_TRUE(firstDialog->isHidden());

    viewport.setDlgClip();
    const QList<QDlgClip*> dialogs = viewport.findChildren<QDlgClip*>();
    ASSERT_EQ(dialogs.size(), 1);
    EXPECT_EQ(dialogs.front(), firstDialog);
    EXPECT_FALSE(firstDialog->isHidden());
}

TEST(PlaneClipCommitTest, EmitsResultOnlyAfterExplicitConfirmation)
{
    MyVTK viewport(nullptr);
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    cloud->push_back(pcl::PointXYZRGB(-1.0f, 0.0f, -1.0f));
    cloud->push_back(pcl::PointXYZRGB(1.0f, 0.0f, -1.0f));
    cloud->push_back(pcl::PointXYZRGB(-1.0f, 0.0f, 1.0f));
    cloud->push_back(pcl::PointXYZRGB(1.0f, 0.0f, 1.0f));
    viewport.getDBItemCloud(cloud);
    vtkPlaneWidget* plane = viewport.DisplayRandomPlane();
    ASSERT_NE(plane, nullptr);
    EXPECT_NEAR(plane->GetHandleSize(), 0.006, 1e-12);
    EXPECT_LE(plane->GetHandleProperty()->GetLineWidth(), 1.25);
    viewport.getClipPlane(plane);
    viewport.setDlgClip();
    QPushButton* confirmButton = viewport.findChild<QPushButton*>(
        QStringLiteral("pBnConfirm"));
    ASSERT_NE(confirmButton, nullptr);
    EXPECT_FALSE(confirmButton->isEnabled());

    int committedResults = 0;
    QObject::connect(
        &viewport, &MyVTK::sendClipCloudResult,
        &viewport,
        [&committedResults](
            pcl::PointCloud<pcl::PointXYZRGB>::Ptr,
            QVector<qint64>, QVector3D, QVector3D, double) {
            ++committedResults;
        });

    viewport.beginClipPlane();
    EXPECT_TRUE(viewport.hasPendingClipResult());
    EXPECT_TRUE(viewport.hasVisibleClipPreview());
    EXPECT_GT(viewport.clipPreviewPointSize(), 2.0);
    const std::array<double, 3> clipColor = viewport.clipPreviewColor();
    EXPECT_NEAR(clipColor[0], 1.0, 1e-12);
    EXPECT_NEAR(clipColor[1], 0.58, 1e-12);
    EXPECT_NEAR(clipColor[2], 0.0, 1e-12);
    EXPECT_FALSE(viewport.isClipSucessed);
    EXPECT_TRUE(confirmButton->isEnabled());
    EXPECT_EQ(committedResults, 0);

    viewport.confirmClipPlane();
    EXPECT_FALSE(viewport.hasPendingClipResult());
    EXPECT_TRUE(viewport.isClipSucessed);
    EXPECT_FALSE(confirmButton->isEnabled());
    EXPECT_EQ(committedResults, 1);
}

TEST(ProjectionPreviewTest, ProjectsTheCurrentlySelectedCloudWithoutCommitting)
{
    MyVTK viewport(nullptr);
    auto cloud = pcl::PointCloud<pcl::PointXYZRGB>::Ptr(
        new pcl::PointCloud<pcl::PointXYZRGB>);
    for (int index = 0; index < 12; ++index)
    {
        pcl::PointXYZRGB point;
        point.x = -2.0F + static_cast<float>(index);
        point.y = static_cast<float>(index % 4);
        point.z = static_cast<float>(index % 3);
        cloud->push_back(point);
    }
    viewport.getDBItemCloud(cloud, 3.5);

    int previews = 0;
    bool previewVisibleWhenSignalled = false;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr projected;
    famp::projection::Plane projectedPlane = famp::projection::Plane::XOY;
    QObject::connect(
        &viewport, &MyVTK::sendProjectedCloudPreview,
        &viewport,
        [&](pcl::PointCloud<pcl::PointXYZRGB>::Ptr points,
            famp::projection::Plane plane) {
            ++previews;
            previewVisibleWhenSignalled =
                viewport.hasVisibleProjectionPreview();
            projected = points;
            projectedPlane = plane;
        });

    viewport.projectToPlane(famp::projection::Plane::YOZ);
    ASSERT_EQ(previews, 1);
    EXPECT_TRUE(previewVisibleWhenSignalled);
    EXPECT_TRUE(viewport.hasVisibleProjectionPreview());
    EXPECT_GT(viewport.projectionPreviewPointSize(), 3.5);
    const std::array<double, 3> previewColor =
        viewport.projectionPreviewColor();
    EXPECT_NEAR(previewColor[0], 0.0, 1e-12);
    EXPECT_NEAR(previewColor[1], 0.9, 1e-12);
    EXPECT_NEAR(previewColor[2], 1.0, 1e-12);
    ASSERT_TRUE(projected);
    ASSERT_EQ(projected->size(), cloud->size());
    EXPECT_EQ(projectedPlane, famp::projection::Plane::YOZ);
    for (const auto& point : projected->points)
        EXPECT_FLOAT_EQ(point.x, -2.0F);
}
