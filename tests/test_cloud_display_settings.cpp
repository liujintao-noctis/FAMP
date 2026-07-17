#include <gtest/gtest.h>

#include <vtkActor.h>
#include <vtkDataArray.h>
#include <vtkDoubleArray.h>
#include <vtkFloatArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSmartPointer.h>

#include "CloudDisplaySettings.h"

namespace
{
vtkSmartPointer<vtkActor> actorWithPoints(int count)
{
    vtkNew<vtkPoints> points;
    for (int index = 0; index < count; ++index)
        points->InsertNextPoint(index, 0.0, index * 0.5);
    vtkNew<vtkPolyData> data;
    data->SetPoints(points);
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(data);
    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    return actor;
}
}

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

TEST(CloudDisplaySettingsTest, AttachesAndColorsByPointAttribute)
{
    vtkSmartPointer<vtkActor> actor = actorWithPoints(3);
    famp::cloud::AttributeChannel intensity;
    intensity.name = QStringLiteral("Intensity");
    intensity.unit = QStringLiteral("raw");
    intensity.type = famp::cloud::AttributeValueType::UnsignedInteger;
    intensity.unsignedValues = {10, 25, 40};
    famp::cloud::CloudAttributes attributes;
    ASSERT_TRUE(attributes.insert(intensity, 3));

    QString error;
    ASSERT_TRUE(famp::display::attachAttribute(
        actor, attributes, QStringLiteral("intensity"), &error))
        << error.toStdString();
    double minimum = 0.0;
    double maximum = 0.0;
    ASSERT_TRUE(famp::display::attributeRange(
        actor, QStringLiteral("intensity"), minimum, maximum, &error));
    EXPECT_DOUBLE_EQ(minimum, 10.0);
    EXPECT_DOUBLE_EQ(maximum, 40.0);

    famp::display::Settings settings;
    settings.colorMode = famp::display::ColorMode::Attribute;
    settings.attributeName = QStringLiteral("INTENSITY");
    ASSERT_TRUE(famp::display::apply(actor, settings, &error))
        << error.toStdString();
    EXPECT_NE(actor->GetMapper()->GetScalarVisibility(), 0);
    EXPECT_DOUBLE_EQ(actor->GetMapper()->GetScalarRange()[0], 10.0);
    EXPECT_DOUBLE_EQ(actor->GetMapper()->GetScalarRange()[1], 40.0);
}

TEST(CloudDisplaySettingsTest, RejectsMismatchedAttributeWithoutMutation)
{
    vtkSmartPointer<vtkActor> actor = actorWithPoints(2);
    famp::cloud::AttributeChannel invalid;
    invalid.name = QStringLiteral("classification");
    invalid.type = famp::cloud::AttributeValueType::UnsignedInteger;
    invalid.unsignedValues = {2};
    famp::cloud::CloudAttributes attributes;
    ASSERT_TRUE(attributes.insert(invalid));

    QString error;
    EXPECT_FALSE(famp::display::attachAttribute(
        actor, attributes, QStringLiteral("classification"), &error));
    EXPECT_FALSE(error.isEmpty());
    auto* data = vtkPolyData::SafeDownCast(
        actor->GetMapper()->GetInputDataObject(0, 0));
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->GetPointData()->GetNumberOfArrays(), 0);
}

TEST(CloudDisplaySettingsTest, KeepsOnlyCurrentRenderAttribute)
{
    vtkSmartPointer<vtkActor> actor = actorWithPoints(2);
    famp::cloud::AttributeChannel intensity;
    intensity.name = QStringLiteral("intensity");
    intensity.type = famp::cloud::AttributeValueType::UnsignedInteger;
    intensity.unsignedValues = {10, 20};
    famp::cloud::AttributeChannel classification;
    classification.name = QStringLiteral("classification");
    classification.type = famp::cloud::AttributeValueType::UnsignedInteger;
    classification.unsignedValues = {2, 5};
    famp::cloud::CloudAttributes attributes;
    ASSERT_TRUE(attributes.insert(intensity, 2));
    ASSERT_TRUE(attributes.insert(classification, 2));

    ASSERT_TRUE(famp::display::attachAttribute(
        actor, attributes, QStringLiteral("intensity")));
    ASSERT_TRUE(famp::display::attachAttribute(
        actor, attributes, QStringLiteral("classification")));
    auto* data = vtkPolyData::SafeDownCast(
        actor->GetMapper()->GetInputDataObject(0, 0));
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->GetPointData()->GetNumberOfArrays(), 1);
    double minimum = 0.0;
    double maximum = 0.0;
    EXPECT_FALSE(famp::display::attributeRange(
        actor, QStringLiteral("intensity"), minimum, maximum));
    EXPECT_TRUE(famp::display::attributeRange(
        actor, QStringLiteral("classification"), minimum, maximum));
}

TEST(CloudDisplaySettingsTest, RejectsMissingColorAttributeAtomically)
{
    vtkSmartPointer<vtkActor> actor = actorWithPoints(1);
    actor->GetProperty()->SetPointSize(2.0);
    famp::display::Settings settings;
    settings.colorMode = famp::display::ColorMode::Attribute;
    settings.attributeName = QStringLiteral("missing");
    settings.pointSize = 8.0;

    QString error;
    EXPECT_FALSE(famp::display::apply(actor, settings, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_DOUBLE_EQ(actor->GetProperty()->GetPointSize(), 2.0);
}
