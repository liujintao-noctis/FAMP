#include <gtest/gtest.h>

#include <vtkActor.h>
#include <vtkFloatArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
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
    settings.colorMode = famp::display::ColorMode::Uniform;
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
    settings.colorMode = famp::display::ColorMode::PointRgb;
    ASSERT_TRUE(famp::display::apply(actor, settings));
    EXPECT_NE(mapper->GetScalarVisibility(), 0);
}

TEST(CloudDisplaySettingsTest, ColorsByElevationWithAutomaticRange)
{
    vtkNew<vtkPoints> points;
    points->InsertNextPoint(0.0, 0.0, -2.0);
    points->InsertNextPoint(1.0, 0.0, 5.0);
    vtkNew<vtkPolyData> data;
    data->SetPoints(points);
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(data);
    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);

    double minimum = 0.0;
    double maximum = 0.0;
    ASSERT_TRUE(famp::display::elevationRange(
        actor, minimum, maximum));
    EXPECT_DOUBLE_EQ(minimum, -2.0);
    EXPECT_DOUBLE_EQ(maximum, 5.0);

    famp::display::Settings settings;
    settings.colorMode = famp::display::ColorMode::Elevation;
    ASSERT_TRUE(famp::display::apply(actor, settings));
    EXPECT_NE(mapper->GetScalarVisibility(), 0);
    ASSERT_NE(data->GetPointData()->GetArray("FAMP_Elevation"), nullptr);
    EXPECT_DOUBLE_EQ(mapper->GetScalarRange()[0], -2.0);
    EXPECT_DOUBLE_EQ(mapper->GetScalarRange()[1], 5.0);
}

TEST(CloudDisplaySettingsTest, RejectsInvalidManualElevationRangeAtomically)
{
    vtkNew<vtkPoints> points;
    points->InsertNextPoint(0.0, 0.0, 1.0);
    vtkNew<vtkPolyData> data;
    data->SetPoints(points);
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(data);
    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetPointSize(2.0);

    famp::display::Settings settings;
    settings.colorMode = famp::display::ColorMode::Elevation;
    settings.automaticScalarRange = false;
    settings.scalarMinimum = 10.0;
    settings.scalarMaximum = 5.0;
    settings.pointSize = 7.0;
    QString error;
    EXPECT_FALSE(famp::display::apply(actor, settings, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_DOUBLE_EQ(actor->GetProperty()->GetPointSize(), 2.0);
    EXPECT_EQ(data->GetPointData()->GetArray("FAMP_Elevation"), nullptr);
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
