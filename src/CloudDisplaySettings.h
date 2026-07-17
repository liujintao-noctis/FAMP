#pragma once

#include "CloudAttributes.h"

#include <QString>

class vtkActor;

namespace famp::display
{
enum class ColorMode
{
    PointRgb,
    Uniform,
    Elevation,
    Attribute
};

struct Settings
{
    double pointSize = 2.0;
    double opacity = 1.0;
    ColorMode colorMode = ColorMode::PointRgb;
    double red = 1.0;
    double green = 1.0;
    double blue = 1.0;
    bool automaticScalarRange = true;
    double scalarMinimum = 0.0;
    double scalarMaximum = 1.0;
    QString attributeName;
};

bool validateSettings(const Settings& settings,
                      QString* errorMessage = nullptr);

bool elevationRange(vtkActor* actor,
                    double& minimum,
                    double& maximum,
                    QString* errorMessage = nullptr);

bool attachAttribute(vtkActor* actor,
                     const famp::cloud::CloudAttributes& attributes,
                     const QString& attributeName,
                     QString* errorMessage = nullptr);

bool attributeRange(vtkActor* actor,
                    const QString& attributeName,
                    double& minimum,
                    double& maximum,
                    QString* errorMessage = nullptr);

bool apply(vtkActor* actor,
           const Settings& settings,
           QString* errorMessage = nullptr);
}
