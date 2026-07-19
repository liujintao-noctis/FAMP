#include "CloudDisplaySettings.h"

#include <QByteArray>

#include <vtkActor.h>
#include <vtkColorTransferFunction.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkFloatArray.h>
#include <vtkMapper.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkProperty.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <cmath>
#include <limits>

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

QString attributeArrayName(const QString& attributeName)
{
    return QStringLiteral("FAMP_Attribute::")
        + attributeName.trimmed().toCaseFolded();
}

vtkDataArray* actorAttributeArray(vtkActor* actor,
                                  const QString& attributeName,
                                  QString* errorMessage)
{
    vtkDataSet* data = actorData(actor);
    if (!data || data->GetNumberOfPoints() == 0)
    {
        setError(errorMessage, QStringLiteral("点云没有可用于属性着色的点。"));
        return nullptr;
    }
    const QByteArray arrayName = attributeArrayName(attributeName).toUtf8();
    vtkDataArray* array = data->GetPointData()->GetArray(arrayName.constData());
    if (!array || array->GetNumberOfComponents() != 1
        || array->GetNumberOfTuples() != data->GetNumberOfPoints())
    {
        setError(errorMessage,
                 QStringLiteral("点云不包含可用的逐点属性：%1")
                     .arg(attributeName.trimmed()));
        return nullptr;
    }
    return array;
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

bool attachAttribute(vtkActor* actor,
                     const famp::cloud::CloudAttributes& attributes,
                     const QString& attributeName,
                     QString* errorMessage)
{
    vtkDataSet* data = actorData(actor);
    if (!data)
    {
        setError(errorMessage, QStringLiteral("点云渲染数据无效。"));
        return false;
    }
    const qint64 pointCount = static_cast<qint64>(data->GetNumberOfPoints());
    if (!attributes.validate(pointCount, errorMessage))
        return false;
    const famp::cloud::AttributeChannel* channel =
        attributes.channel(attributeName);
    if (!channel)
    {
        setError(errorMessage,
                 QStringLiteral("点云不包含逐点属性：%1")
                     .arg(attributeName.trimmed()));
        return false;
    }

    vtkSmartPointer<vtkDoubleArray> array =
        vtkSmartPointer<vtkDoubleArray>::New();
    const QByteArray arrayName = attributeArrayName(channel->name).toUtf8();
    array->SetName(arrayName.constData());
    array->SetNumberOfComponents(1);
    array->SetNumberOfTuples(data->GetNumberOfPoints());
    for (vtkIdType index = 0; index < data->GetNumberOfPoints(); ++index)
    {
        double value = 0.0;
        if (!channel->valueAsDouble(static_cast<qint64>(index), value))
        {
            setError(errorMessage,
                     QStringLiteral("无法读取逐点属性：%1").arg(channel->name));
            return false;
        }
        array->SetValue(index, value);
    }
    vtkPointData* pointData = data->GetPointData();
    const QByteArray prefix("FAMP_Attribute::");
    for (int index = pointData->GetNumberOfArrays() - 1; index >= 0; --index)
    {
        const char* name = pointData->GetArrayName(index);
        if (name && QByteArray(name).startsWith(prefix))
            pointData->RemoveArray(name);
    }
    pointData->AddArray(array);
    pointData->Modified();
    data->Modified();
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool attributeRange(vtkActor* actor,
                    const QString& attributeName,
                    double& minimum,
                    double& maximum,
                    QString* errorMessage)
{
    if (attributeName.trimmed().isEmpty())
    {
        setError(errorMessage, QStringLiteral("请选择用于着色的逐点属性。"));
        return false;
    }
    vtkDataArray* array = actorAttributeArray(actor, attributeName, errorMessage);
    if (!array)
        return false;

    double candidateMinimum = std::numeric_limits<double>::infinity();
    double candidateMaximum = -std::numeric_limits<double>::infinity();
    for (vtkIdType index = 0; index < array->GetNumberOfTuples(); ++index)
    {
        const double value = array->GetComponent(index, 0);
        if (!std::isfinite(value))
            continue;
        candidateMinimum = std::min(candidateMinimum, value);
        candidateMaximum = std::max(candidateMaximum, value);
    }
    if (!std::isfinite(candidateMinimum) || !std::isfinite(candidateMaximum))
    {
        setError(errorMessage,
                 QStringLiteral("逐点属性 %1 不包含有限数值。")
                     .arg(attributeName.trimmed()));
        return false;
    }
    minimum = candidateMinimum;
    maximum = candidateMaximum;
    if (errorMessage)
        errorMessage->clear();
    return true;
}

bool validateSettings(const Settings& settings, QString* errorMessage)
{
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
    if (settings.colorMode != ColorMode::PointRgb
        && settings.colorMode != ColorMode::Uniform
        && settings.colorMode != ColorMode::Elevation
        && settings.colorMode != ColorMode::Attribute)
    {
        setError(errorMessage, QStringLiteral("点云颜色模式无效。"));
        return false;
    }
    if (!std::isfinite(settings.scalarMinimum)
        || !std::isfinite(settings.scalarMaximum))
    {
        setError(errorMessage, QStringLiteral("色带范围必须是有限数值。"));
        return false;
    }
    const bool scalarMode = settings.colorMode == ColorMode::Elevation
        || settings.colorMode == ColorMode::Attribute;
    if (scalarMode
        && !settings.automaticScalarRange
        && settings.scalarMinimum >= settings.scalarMaximum)
    {
        setError(errorMessage, QStringLiteral("色带最小值必须小于最大值。"));
        return false;
    }
    if (settings.colorMode == ColorMode::Attribute
        && (settings.attributeName.trimmed().isEmpty()
            || settings.attributeName.trimmed().size() > 128))
    {
        setError(errorMessage, QStringLiteral("请选择有效的逐点属性。"));
        return false;
    }
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
    if (!validateSettings(settings, errorMessage))
        return false;

    double scalarMinimum = settings.scalarMinimum;
    double scalarMaximum = settings.scalarMaximum;
    vtkDataSet* data = actorData(actor);
    const bool scalarMode = settings.colorMode == ColorMode::Elevation
        || settings.colorMode == ColorMode::Attribute;
    if (scalarMode)
    {
        double automaticMinimum = 0.0;
        double automaticMaximum = 0.0;
        const bool hasRange = settings.colorMode == ColorMode::Elevation
            ? elevationRange(actor, automaticMinimum, automaticMaximum, errorMessage)
            : attributeRange(actor, settings.attributeName,
                             automaticMinimum, automaticMaximum, errorMessage);
        if (!hasRange)
        {
            return false;
        }
        if (settings.automaticScalarRange)
        {
            scalarMinimum = automaticMinimum;
            scalarMaximum = automaticMaximum;
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
                         QStringLiteral("色带最小值必须小于最大值。"));
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
    else if (settings.colorMode == ColorMode::Elevation)
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
    else
    {
        const QByteArray arrayName =
            attributeArrayName(settings.attributeName).toUtf8();
        data->GetPointData()->SetActiveScalars(arrayName.constData());

        vtkNew<vtkColorTransferFunction> colors;
        colors->SetColorSpaceToRGB();
        colors->AddRGBPoint(scalarMinimum, 0.10, 0.20, 0.80);
        colors->AddRGBPoint(
            scalarMinimum + (scalarMaximum - scalarMinimum) * 0.5,
            0.10, 0.80, 0.55);
        colors->AddRGBPoint(scalarMaximum, 0.90, 0.20, 0.10);
        mapper->SetScalarModeToUsePointFieldData();
        mapper->SelectColorArray(arrayName.constData());
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
