#include <gtest/gtest.h>

#include <vtkActor.h>
#include <vtkNew.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>

#include "CloudDisplaySettings.h"

TEST(CloudDisplaySettingsTest, AppliesUniformColorPointSizeAndOpacity)
{
    vtkNew<vtkActor> actor;
    vtkNew<vtkPolyDataMapper> mapper;
    actor->SetMapper(mapper);

    famp::display::Settings settings;
    settings.pointSize = 4.5;
    settings.opacity = 0.65;
    settings.usePointColors = false;
    settings.red = 0.2;
    settings.green = 0.4;
    settings.blue = 0.6;
    QString error;

    ASSERT_TRUE(famp::display::apply(actor, settings, &error))
        << error.toStdString();
    EXPECT_DOUBLE_EQ(actor->GetProperty()->GetPointSize(), 4.5);
    EXPECT_DOUBLE_EQ(actor->GetProperty()->GetOpacity(), 0.65);
    EXPECT_EQ(mapper->GetScalarVisibility(), 0);
    const double* color = actor->GetProperty()->GetColor();
    EXPECT_NEAR(color[0], 0.2, 1.0e-12);
    EXPECT_NEAR(color[1], 0.4, 1.0e-12);
    EXPECT_NEAR(color[2], 0.6, 1.0e-12);
}

TEST(CloudDisplaySettingsTest, RestoresPointRgbMode)
{
    vtkNew<vtkActor> actor;
    vtkNew<vtkPolyDataMapper> mapper;
    actor->SetMapper(mapper);
    mapper->ScalarVisibilityOff();

    famp::display::Settings settings;
    settings.usePointColors = true;
    ASSERT_TRUE(famp::display::apply(actor, settings));
    EXPECT_NE(mapper->GetScalarVisibility(), 0);
}

TEST(CloudDisplaySettingsTest, RejectsInvalidSettingsWithoutPartialMutation)
{
    vtkNew<vtkActor> actor;
    vtkNew<vtkPolyDataMapper> mapper;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetPointSize(2.0);

    famp::display::Settings settings;
    settings.pointSize = 100.0;
    settings.opacity = 0.5;
    QString error;
    EXPECT_FALSE(famp::display::apply(actor, settings, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_DOUBLE_EQ(actor->GetProperty()->GetPointSize(), 2.0);
    EXPECT_DOUBLE_EQ(actor->GetProperty()->GetOpacity(), 1.0);
}
