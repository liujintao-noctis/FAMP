#include "CloudDisplaySettings.h"

#include <vtkActor.h>
#include <vtkMapper.h>
#include <vtkProperty.h>

#include <cmath>

namespace famp::display
{
namespace
{
void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool unitValue(double value)
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}
}

bool apply(vtkActor* actor,
           const Settings& settings,
           QString* errorMessage)
{
    if (!actor || !actor->GetMapper())
    {
        setError(errorMessage, QStringLiteral("点云渲染对象无效。"));
        return false;
    }
    if (!std::isfinite(settings.pointSize)
        || settings.pointSize < 1.0
        || settings.pointSize > 20.0)
    {
        setError(errorMessage, QStringLiteral("点大小必须在 1 到 20 之间。"));
        return false;
    }
    if (!std::isfinite(settings.opacity)
        || settings.opacity < 0.05
        || settings.opacity > 1.0)
    {
        setError(errorMessage, QStringLiteral("不透明度必须在 0.05 到 1 之间。"));
        return false;
    }
    if (!unitValue(settings.red)
        || !unitValue(settings.green)
        || !unitValue(settings.blue))
    {
        setError(errorMessage, QStringLiteral("点云颜色分量必须在 0 到 1 之间。"));
        return false;
    }

    actor->GetProperty()->SetPointSize(settings.pointSize);
    actor->GetProperty()->SetOpacity(settings.opacity);
    if (settings.usePointColors)
    {
        actor->GetMapper()->ScalarVisibilityOn();
    }
    else
    {
        actor->GetMapper()->ScalarVisibilityOff();
        actor->GetProperty()->SetColor(
            settings.red, settings.green, settings.blue);
    }
    actor->GetMapper()->Modified();
    actor->Modified();
    if (errorMessage)
        errorMessage->clear();
    return true;
}
}
