#include "VTKRenderManager.h"

#include <gtest/gtest.h>

#include <vtkAxesActor.h>
#include <vtkOrientationMarkerWidget.h>

TEST(VTKRenderManagerTest, RetainsLabeledOrientationAxesInLowerLeft)
{
    FAMP_QVTK_WIDGET widget;
    VTKRenderManager manager(&widget);
    manager.Init();
    manager.setWidgetAxes();

    vtkAxesActor* axes = manager.orientationAxes();
    vtkOrientationMarkerWidget* marker = manager.orientationMarkerWidget();
    ASSERT_NE(axes, nullptr);
    ASSERT_NE(marker, nullptr);
    EXPECT_STREQ(axes->GetXAxisLabelText(), "X");
    EXPECT_STREQ(axes->GetYAxisLabelText(), "Y");
    EXPECT_STREQ(axes->GetZAxisLabelText(), "Z");
    EXPECT_EQ(marker->GetOrientationMarker(), axes);
    EXPECT_EQ(marker->GetEnabled(), 1);
    EXPECT_EQ(marker->GetInteractive(), 0);

    const double* viewport = marker->GetViewport();
    ASSERT_NE(viewport, nullptr);
    EXPECT_DOUBLE_EQ(viewport[0], 0.0);
    EXPECT_DOUBLE_EQ(viewport[1], 0.0);
    EXPECT_DOUBLE_EQ(viewport[2], 0.18);
    EXPECT_DOUBLE_EQ(viewport[3], 0.18);
}
