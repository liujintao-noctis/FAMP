#include "CloudDisplaySettings.h"

#include <vtkActor.h>
#include <vtkColorTransferFunction.h>
#include <vtkDataSet.h>
#include <vtkFloatArray.h>
#include <vtkMapper.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkProperty.h>

#include <algorithm>
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

vtkDataSet* actorData(vtkActor* actor)
{
    return actor && actor->GetMapper()
        ? vtkDataSet::SafeDownCast(
              actor->GetMapper()->GetInputDataObject(0, 0))
        : nullptr;
}
}

bool elevationRange(vtkActor* actor,
                    double& minimum,
                    double& maximum,
                    QString* errorMessage)
{
    vtkDataSet* data = actorData(actor);
    if (!data || data->GetNumberOfPoints() == 0)
    {
        setError(errorMessage, QStringLiteral("点云没有可用于高程着色的点。"));
        return false;
    }

    double point[3]{};
    data->GetPoint(0, point);
    if (!std::isfinite(point[2]))
    {
        setError(errorMessage, QStringLiteral("点云高程包含无效数值。"));
        return false;
    }
    double candidateMinimum = point[2];
    double candidateMaximum = point[2];
    for (vtkIdType index = 1; index < data->GetNumberOfPoints(); ++index)
    {
        data->GetPoint(index, point);
        if (!std::isfinite(point[2]))
        {
            setError(errorMessage, QStringLiteral("点云高程包含无效数值。"));
            return false;
        }
        candidateMinimum = std::min(candidateMinimum, point[2]);
        candidateMaximum = std::max(candidateMaximum, point[2]);
    }

    minimum = candidateMinimum;
    maximum = candidateMaximum;
    if (errorMessage)
        errorMessage->clear();
    return true;
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

    double scalarMinimum = settings.scalarMinimum;
    double scalarMaximum = settings.scalarMaximum;
    vtkDataSet* data = actorData(actor);
    if (settings.colorMode == ColorMode::Elevation)
    {
        if (settings.automaticScalarRange
            && !elevationRange(actor, scalarMinimum, scalarMaximum, errorMessage))
        {
            return false;
        }
        if (!std::isfinite(scalarMinimum)
            || !std::isfinite(scalarMaximum)
            || scalarMinimum >= scalarMaximum)
        {
            if (settings.automaticScalarRange
                && qFuzzyCompare(scalarMinimum + 1.0, scalarMaximum + 1.0))
            {
                scalarMinimum -= 0.5;
                scalarMaximum += 0.5;
            }
            else
            {
                setError(errorMessage,
                         QStringLiteral("高程色带最小值必须小于最大值。"));
                return false;
            }
        }
    }

    actor->GetProperty()->SetPointSize(settings.pointSize);
    actor->GetProperty()->SetOpacity(settings.opacity);
    vtkMapper* mapper = actor->GetMapper();
    if (settings.colorMode == ColorMode::PointRgb)
    {
        if (data && data->GetPointData()->GetArray("RGB"))
            data->GetPointData()->SetActiveScalars("RGB");
        mapper->SetScalarModeToUsePointData();
        mapper->SetColorModeToDirectScalars();
        mapper->ScalarVisibilityOn();
    }
    else if (settings.colorMode == ColorMode::Uniform)
    {
        mapper->ScalarVisibilityOff();
        actor->GetProperty()->SetColor(
            settings.red, settings.green, settings.blue);
    }
    else
    {
        vtkNew<vtkFloatArray> elevations;
        elevations->SetName("FAMP_Elevation");
        elevations->SetNumberOfComponents(1);
        elevations->SetNumberOfTuples(data->GetNumberOfPoints());
        double point[3]{};
        for (vtkIdType index = 0; index < data->GetNumberOfPoints(); ++index)
        {
            data->GetPoint(index, point);
            elevations->SetValue(index, static_cast<float>(point[2]));
        }
        data->GetPointData()->RemoveArray("FAMP_Elevation");
        data->GetPointData()->AddArray(elevations);
        data->GetPointData()->SetActiveScalars("FAMP_Elevation");

        vtkNew<vtkColorTransferFunction> colors;
        colors->SetColorSpaceToRGB();
        colors->AddRGBPoint(scalarMinimum, 0.10, 0.20, 0.80);
        colors->AddRGBPoint(
            scalarMinimum + (scalarMaximum - scalarMinimum) * 0.5,
            0.10, 0.80, 0.55);
        colors->AddRGBPoint(scalarMaximum, 0.90, 0.20, 0.10);
        mapper->SetScalarModeToUsePointFieldData();
        mapper->SelectColorArray("FAMP_Elevation");
        mapper->SetColorModeToMapScalars();
        mapper->SetLookupTable(colors);
        mapper->SetScalarRange(scalarMinimum, scalarMaximum);
        mapper->ScalarVisibilityOn();
    }
    mapper->Modified();
    actor->Modified();
    if (errorMessage)
        errorMessage->clear();
    return true;
}
}
