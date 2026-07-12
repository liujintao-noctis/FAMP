#pragma once

#include <QString>

class vtkActor;

namespace famp::display
{
struct Settings
{
    double pointSize = 2.0;
    double opacity = 1.0;
    bool usePointColors = true;
    double red = 1.0;
    double green = 1.0;
    double blue = 1.0;
};

bool apply(vtkActor* actor,
           const Settings& settings,
           QString* errorMessage = nullptr);
}
