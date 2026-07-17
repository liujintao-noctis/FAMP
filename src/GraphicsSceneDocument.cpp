#include "GraphicsSceneDocument.h"

#include "CompassItem.h"
#include "ContourItem.h"
#include "FormTabulationItem.h"
#include "MeasurementItem.h"
#include "MyItem.h"

#include <QColor>
#include <QGraphicsItemGroup>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QJsonArray>
#include <QJsonValue>
#include <QtAlgorithms>
#include <QTransform>

#include <cmath>
#include <limits>
#include <memory>

namespace
{
constexpr int MaxItems = 20'000;
constexpr int MaxPointsPerItem = 1'000'000;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool finite(qreal value)
{
    return std::isfinite(static_cast<double>(value));
}

QJsonArray point(const QPointF& value)
{
    return QJsonArray{value.x(), value.y()};
}

bool readPoint(const QJsonValue& value, QPointF& result)
{
    if (!value.isArray())
        return false;
    const QJsonArray values = value.toArray();
    if (values.size() != 2 || !values.at(0).isDouble()
        || !values.at(1).isDouble())
    {
        return false;
    }
    const QPointF candidate(values.at(0).toDouble(), values.at(1).toDouble());
    if (!finite(candidate.x()) || !finite(candidate.y()))
        return false;
    result = candidate;
    return true;
}

QJsonArray points(const QVector<QPointF>& values)
{
    QJsonArray result;
    for (const QPointF& value : values)
        result.append(point(value));
    return result;
}

bool readPoints(const QJsonValue& value,
                int minimumCount,
                QVector<QPointF>& result)
{
    if (!value.isArray())
        return false;
    const QJsonArray values = value.toArray();
    if (values.size() < minimumCount || values.size() > MaxPointsPerItem)
        return false;
    QVector<QPointF> candidate;
    candidate.reserve(values.size());
    for (const QJsonValue& entry : values)
    {
        QPointF parsed;
        if (!readPoint(entry, parsed))
            return false;
        candidate.append(parsed);
    }
    result = candidate;
    return true;
}

QJsonObject commonState(const QGraphicsItem* item)
{
    const QTransform transform = item->transform();
    QJsonObject result;
    result.insert(QStringLiteral("position"), point(item->pos()));
    result.insert(QStringLiteral("origin"), point(item->transformOriginPoint()));
    result.insert(QStringLiteral("rotation"), item->rotation());
    result.insert(QStringLiteral("scale"), item->scale());
    result.insert(QStringLiteral("z"), item->zValue());
    result.insert(QStringLiteral("opacity"), item->opacity());
    result.insert(QStringLiteral("visible"), item->isVisible());
    result.insert(QStringLiteral("transform"), QJsonArray{
        transform.m11(), transform.m12(), transform.m13(),
        transform.m21(), transform.m22(), transform.m23(),
        transform.m31(), transform.m32(), transform.m33()});
    return result;
}

bool applyCommonState(QGraphicsItem* item,
                      const QJsonValue& value,
                      QString* errorMessage)
{
    if (!item || !value.isObject())
    {
        setError(errorMessage, QStringLiteral("二维图元缺少公共状态。"));
        return false;
    }
    const QJsonObject state = value.toObject();
    QPointF position;
    QPointF origin;
    if (!readPoint(state.value(QStringLiteral("position")), position)
        || !readPoint(state.value(QStringLiteral("origin")), origin))
    {
        setError(errorMessage, QStringLiteral("二维图元坐标无效。"));
        return false;
    }
    const double rotation = state.value(QStringLiteral("rotation")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    const double scale = state.value(QStringLiteral("scale")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    const double z = state.value(QStringLiteral("z")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    const double opacity = state.value(QStringLiteral("opacity")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    const QJsonValue visibleValue = state.value(QStringLiteral("visible"));
    const QJsonArray matrix = state.value(QStringLiteral("transform")).toArray();
    if (!std::isfinite(rotation) || !std::isfinite(scale) || scale <= 0.0
        || !std::isfinite(z) || !std::isfinite(opacity)
        || opacity < 0.0 || opacity > 1.0 || !visibleValue.isBool()
        || matrix.size() != 9)
    {
        setError(errorMessage, QStringLiteral("二维图元变换状态无效。"));
        return false;
    }
    double components[9];
    for (int index = 0; index < 9; ++index)
    {
        if (!matrix.at(index).isDouble())
        {
            setError(errorMessage, QStringLiteral("二维图元变换矩阵无效。"));
            return false;
        }
        components[index] = matrix.at(index).toDouble();
        if (!std::isfinite(components[index]))
        {
            setError(errorMessage, QStringLiteral("二维图元变换矩阵无效。"));
            return false;
        }
    }

    item->setPos(position);
    item->setTransformOriginPoint(origin);
    item->setTransform(QTransform(
        components[0], components[1], components[2],
        components[3], components[4], components[5],
        components[6], components[7], components[8]));
    item->setRotation(rotation);
    item->setScale(scale);
    item->setZValue(z);
    item->setOpacity(opacity);
    item->setVisible(visibleValue.toBool());
    return true;
}

bool serializeItem(const QGraphicsItem* item,
                   QJsonObject& result,
                   int& itemCount,
                   QString* errorMessage)
{
    if (!item || ++itemCount > MaxItems)
    {
        setError(errorMessage, QStringLiteral("二维图元数量超过安全上限。"));
        return false;
    }

    result.insert(QStringLiteral("state"), commonState(item));
    if (const auto* group = dynamic_cast<const QGraphicsItemGroup*>(item))
    {
        result.insert(QStringLiteral("type"), QStringLiteral("group"));
        QJsonArray children;
        for (const QGraphicsItem* child : group->childItems())
        {
            QJsonObject serialized;
            if (!serializeItem(child, serialized, itemCount, errorMessage))
                return false;
            children.append(serialized);
        }
        result.insert(QStringLiteral("children"), children);
        return true;
    }
    if (const auto* measurement = dynamic_cast<const MeasurementItem*>(item))
    {
        result.insert(QStringLiteral("type"), QStringLiteral("measurement"));
        QString kind = QStringLiteral("distance");
        if (measurement->kind() == famp::measurement::Kind::Area)
            kind = QStringLiteral("area");
        else if (measurement->kind() == famp::measurement::Kind::Angle)
            kind = QStringLiteral("angle");
        result.insert(QStringLiteral("kind"), kind);
        result.insert(QStringLiteral("meterPoints"),
                      points(measurement->meterPoints()));
        result.insert(QStringLiteral("sceneUnitsPerMeter"),
                      point(measurement->sceneUnitsPerMeter()));
        return true;
    }
    if (const auto* contours = dynamic_cast<const ContourItem*>(item))
    {
        const ContourItemData& data = contours->contourData();
        if (!ContourItem::validateData(data, errorMessage))
            return false;
        result.insert(QStringLiteral("type"), QStringLiteral("terrainContours"));
        result.insert(QStringLiteral("origin"),
                      QJsonArray{data.originX, data.originY});
        result.insert(QStringLiteral("horizontalUnitToMetre"),
                      data.horizontalUnitToMetre);
        result.insert(QStringLiteral("sourceCrs"), data.sourceCrs);
        result.insert(QStringLiteral("sourceLayerId"), data.sourceLayerId);
        result.insert(QStringLiteral("sourceLayerName"), data.sourceLayerName);
        result.insert(QStringLiteral("demPath"), data.demPath);
        result.insert(QStringLiteral("interval"), data.interval);
        result.insert(QStringLiteral("baseElevation"), data.baseElevation);
        result.insert(QStringLiteral("sceneUnitsPerMeter"),
                      point(contours->sceneUnitsPerMeter()));
        QJsonArray lines;
        for (const auto& line : data.relativeLines)
        {
            QJsonArray linePoints;
            for (const auto& linePoint : line.points)
                linePoints.append(QJsonArray{linePoint[0], linePoint[1]});
            QJsonObject lineObject;
            lineObject.insert(QStringLiteral("elevation"), line.elevation);
            lineObject.insert(QStringLiteral("points"), linePoints);
            lines.append(lineObject);
        }
        result.insert(QStringLiteral("lines"), lines);
        return true;
    }
    if (const auto* curve = dynamic_cast<const MyItem*>(item))
    {
        if (curve->points().isEmpty()
            || curve->points().size() > MaxPointsPerItem)
        {
            setError(errorMessage, QStringLiteral("投影图元点集无效。"));
            return false;
        }
        result.insert(QStringLiteral("type"), QStringLiteral("projection"));
        result.insert(QStringLiteral("projectionType"),
                      static_cast<int>(curve->projectionType()));
        result.insert(QStringLiteral("points"), points(curve->points()));
        result.insert(QStringLiteral("label"), curve->data(1).toString());
        return true;
    }
    if (dynamic_cast<const CompassItem*>(item))
    {
        result.insert(QStringLiteral("type"), QStringLiteral("compass"));
        return true;
    }
    if (const auto* table = dynamic_cast<const FormTabulationItem*>(item))
    {
        result.insert(QStringLiteral("type"), QStringLiteral("form"));
        result.insert(QStringLiteral("designer"), table->designerText());
        result.insert(QStringLiteral("date"), table->dateText());
        result.insert(QStringLiteral("scaleText"), table->scaleText());
        return true;
    }
    if (const auto* text = dynamic_cast<const QGraphicsTextItem*>(item))
    {
        result.insert(QStringLiteral("type"), QStringLiteral("text"));
        result.insert(QStringLiteral("html"), text->toHtml());
        result.insert(QStringLiteral("color"),
                      text->defaultTextColor().name(QColor::HexArgb));
        return true;
    }

    setError(errorMessage,
             QStringLiteral("项目包含暂不支持保存的二维图元类型。"));
    return false;
}

QGraphicsItem* deserializeItem(const QJsonValue& value,
                               int& itemCount,
                               QString* errorMessage)
{
    if (!value.isObject() || ++itemCount > MaxItems)
    {
        setError(errorMessage, QStringLiteral("二维图元列表无效或数量过多。"));
        return nullptr;
    }
    const QJsonObject object = value.toObject();
    const QString type = object.value(QStringLiteral("type")).toString();
    std::unique_ptr<QGraphicsItem> item;

    if (type == QStringLiteral("group"))
    {
        const QJsonValue childValue = object.value(QStringLiteral("children"));
        if (!childValue.isArray())
        {
            setError(errorMessage, QStringLiteral("二维组合图元内容无效。"));
            return nullptr;
        }
        auto group = std::make_unique<QGraphicsItemGroup>();
        for (const QJsonValue& childObject : childValue.toArray())
        {
            std::unique_ptr<QGraphicsItem> child(
                deserializeItem(childObject, itemCount, errorMessage));
            if (!child)
                return nullptr;
            group->addToGroup(child.release());
        }
        item = std::move(group);
    }
    else if (type == QStringLiteral("measurement"))
    {
        const QString kindValue = object.value(QStringLiteral("kind")).toString();
        auto kind = famp::measurement::Kind::Distance;
        if (kindValue == QStringLiteral("area"))
            kind = famp::measurement::Kind::Area;
        else if (kindValue == QStringLiteral("angle"))
            kind = famp::measurement::Kind::Angle;
        if (kindValue != QStringLiteral("area")
            && kindValue != QStringLiteral("distance")
            && kindValue != QStringLiteral("angle"))
        {
            setError(errorMessage, QStringLiteral("测量图元类型无效。"));
            return nullptr;
        }
        QVector<QPointF> meterPoints;
        QPointF sceneUnitsPerMeter;
        const int minimum = famp::measurement::minimumPointCount(kind);
        if (!readPoints(object.value(QStringLiteral("meterPoints")),
                        minimum, meterPoints)
            || (kind == famp::measurement::Kind::Angle
                && meterPoints.size() != 3)
            || !readPoint(object.value(QStringLiteral("sceneUnitsPerMeter")),
                          sceneUnitsPerMeter)
            || sceneUnitsPerMeter.x() <= 0.0
            || sceneUnitsPerMeter.y() <= 0.0)
        {
            setError(errorMessage, QStringLiteral("测量图元坐标无效。"));
            return nullptr;
        }
        item = std::make_unique<MeasurementItem>(
            kind, meterPoints, sceneUnitsPerMeter);
    }
    else if (type == QStringLiteral("terrainContours"))
    {
        QPointF origin;
        QPointF sceneUnitsPerMeter;
        const QJsonValue linesValue = object.value(QStringLiteral("lines"));
        if (!readPoint(object.value(QStringLiteral("origin")), origin)
            || !readPoint(object.value(QStringLiteral("sceneUnitsPerMeter")),
                          sceneUnitsPerMeter)
            || sceneUnitsPerMeter.x() <= 0.0
            || sceneUnitsPerMeter.y() <= 0.0
            || !linesValue.isArray())
        {
            setError(errorMessage, QStringLiteral("等高线图元元数据无效。"));
            return nullptr;
        }
        ContourItemData data;
        data.originX = origin.x();
        data.originY = origin.y();
        data.horizontalUnitToMetre = object.value(
            QStringLiteral("horizontalUnitToMetre")).toDouble(
                std::numeric_limits<double>::quiet_NaN());
        data.sourceCrs = object.value(QStringLiteral("sourceCrs")).toString();
        data.sourceLayerId = object.value(
            QStringLiteral("sourceLayerId")).toString();
        data.sourceLayerName = object.value(
            QStringLiteral("sourceLayerName")).toString();
        data.demPath = object.value(QStringLiteral("demPath")).toString();
        data.interval = object.value(QStringLiteral("interval")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
        data.baseElevation = object.value(
            QStringLiteral("baseElevation")).toDouble(
                std::numeric_limits<double>::quiet_NaN());
        quint64 totalPoints = 0;
        for (const QJsonValue& lineValue : linesValue.toArray())
        {
            if (!lineValue.isObject())
            {
                setError(errorMessage, QStringLiteral("等高线图元线条无效。"));
                return nullptr;
            }
            const QJsonObject lineObject = lineValue.toObject();
            const double elevation = lineObject.value(
                QStringLiteral("elevation")).toDouble(
                    std::numeric_limits<double>::quiet_NaN());
            QVector<QPointF> parsedPoints;
            if (!std::isfinite(elevation)
                || !readPoints(lineObject.value(QStringLiteral("points")),
                               2, parsedPoints))
            {
                setError(errorMessage, QStringLiteral("等高线图元线条无效。"));
                return nullptr;
            }
            totalPoints += static_cast<quint64>(parsedPoints.size());
            if (totalPoints > ContourItem::MaximumDisplayPoints)
            {
                setError(errorMessage, QStringLiteral("等高线图元点数超过安全上限。"));
                return nullptr;
            }
            famp::terrain::ContourLine line;
            line.elevation = elevation;
            line.points.reserve(parsedPoints.size());
            for (const QPointF& parsedPoint : parsedPoints)
                line.points.append({parsedPoint.x(), parsedPoint.y()});
            data.relativeLines.append(std::move(line));
        }
        if (!ContourItem::validateData(data, errorMessage))
            return nullptr;
        item = std::make_unique<ContourItem>(
            std::move(data), sceneUnitsPerMeter);
    }
    else if (type == QStringLiteral("projection"))
    {
        QVector<QPointF> projectedPoints;
        if (!readPoints(object.value(QStringLiteral("points")),
                        1, projectedPoints))
        {
            setError(errorMessage, QStringLiteral("投影图元点集无效。"));
            return nullptr;
        }
        const int projection = object.value(QStringLiteral("projectionType"))
                                   .toInt(static_cast<int>(NONE));
        if (projection < static_cast<int>(XOY)
            || projection > static_cast<int>(NONE))
        {
            setError(errorMessage, QStringLiteral("投影图元方向无效。"));
            return nullptr;
        }
        auto curve = std::make_unique<MyItem>(
            projectedPoints, static_cast<ProjectType>(projection));
        curve->setData(1, object.value(QStringLiteral("label")).toString());
        item = std::move(curve);
    }
    else if (type == QStringLiteral("compass"))
    {
        item = std::make_unique<CompassItem>();
    }
    else if (type == QStringLiteral("form"))
    {
        item = std::make_unique<FormTabulationItem>(
            object.value(QStringLiteral("designer")).toString(),
            object.value(QStringLiteral("date")).toString(),
            object.value(QStringLiteral("scaleText")).toString(),
            nullptr);
    }
    else if (type == QStringLiteral("text"))
    {
        const QJsonValue html = object.value(QStringLiteral("html"));
        const QColor color(object.value(QStringLiteral("color")).toString());
        if (!html.isString() || !color.isValid())
        {
            setError(errorMessage, QStringLiteral("文字图元内容无效。"));
            return nullptr;
        }
        auto text = std::make_unique<QGraphicsTextItem>();
        text->setHtml(html.toString());
        text->setDefaultTextColor(color);
        text->setFlags(QGraphicsItem::ItemIsSelectable
                       | QGraphicsItem::ItemIsMovable
                       | QGraphicsItem::ItemIsFocusable);
        text->setTextInteractionFlags(Qt::TextEditorInteraction);
        item = std::move(text);
    }
    else
    {
        setError(errorMessage, QStringLiteral("二维图元类型无效：%1").arg(type));
        return nullptr;
    }

    if (!applyCommonState(
            item.get(), object.value(QStringLiteral("state")), errorMessage))
    {
        return nullptr;
    }
    return item.release();
}
}

namespace famp::graphicsdoc
{
QJsonObject saveScene(QGraphicsScene* scene, QString* errorMessage)
{
    if (!scene)
    {
        setError(errorMessage, QStringLiteral("二维画布不存在。"));
        return {};
    }

    QJsonArray items;
    int itemCount = 0;
    for (const QGraphicsItem* item : scene->items(Qt::AscendingOrder))
    {
        if (item->parentItem())
            continue;
        if (item->data(TransientItemDataKey).toBool())
            continue;
        QJsonObject serialized;
        if (!serializeItem(item, serialized, itemCount, errorMessage))
            return {};
        items.append(serialized);
    }

    QJsonObject document;
    document.insert(QStringLiteral("schemaVersion"), SchemaVersion);
    document.insert(QStringLiteral("sceneRect"), QJsonArray{
        scene->sceneRect().x(), scene->sceneRect().y(),
        scene->sceneRect().width(), scene->sceneRect().height()});
    document.insert(QStringLiteral("items"), items);
    return document;
}

bool validateSceneDocument(const QJsonObject& document,
                           QString* errorMessage)
{
    QGraphicsScene temporary;
    return restoreScene(&temporary, document, nullptr, errorMessage);
}

bool restoreScene(QGraphicsScene* scene,
                  const QJsonObject& document,
                  QList<QGraphicsItem*>* restoredItems,
                  QString* errorMessage)
{
    if (!scene)
    {
        setError(errorMessage, QStringLiteral("二维画布不存在。"));
        return false;
    }
    const int schemaVersion = document.value(
        QStringLiteral("schemaVersion")).toInt(-1);
    if (schemaVersion < MinimumSupportedSchemaVersion
        || schemaVersion > SchemaVersion)
    {
        setError(errorMessage, QStringLiteral("不支持的二维画布格式版本。"));
        return false;
    }
    const QJsonArray sceneRect = document.value(QStringLiteral("sceneRect")).toArray();
    const QJsonValue itemValue = document.value(QStringLiteral("items"));
    if (sceneRect.size() != 4 || !itemValue.isArray()
        || itemValue.toArray().size() > MaxItems)
    {
        setError(errorMessage, QStringLiteral("二维画布文档结构无效。"));
        return false;
    }
    double rectValues[4];
    for (int index = 0; index < 4; ++index)
    {
        if (!sceneRect.at(index).isDouble())
        {
            setError(errorMessage, QStringLiteral("二维画布范围无效。"));
            return false;
        }
        rectValues[index] = sceneRect.at(index).toDouble();
        if (!std::isfinite(rectValues[index]))
        {
            setError(errorMessage, QStringLiteral("二维画布范围无效。"));
            return false;
        }
    }
    const QRectF targetRect(
        rectValues[0], rectValues[1], rectValues[2], rectValues[3]);
    if (!targetRect.isValid() || targetRect.isEmpty())
    {
        setError(errorMessage, QStringLiteral("二维画布范围无效。"));
        return false;
    }

    QList<QGraphicsItem*> candidates;
    int itemCount = 0;
    for (const QJsonValue& itemObject : itemValue.toArray())
    {
        QGraphicsItem* candidate = deserializeItem(
            itemObject, itemCount, errorMessage);
        if (!candidate)
        {
            qDeleteAll(candidates);
            return false;
        }
        candidates.append(candidate);
    }

    scene->clear();
    scene->setSceneRect(targetRect);
    for (QGraphicsItem* item : candidates)
        scene->addItem(item);
    if (restoredItems)
        *restoredItems = candidates;
    return true;
}
}
