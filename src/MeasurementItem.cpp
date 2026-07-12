#include "MeasurementItem.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <utility>

MeasurementItem::MeasurementItem(famp::measurement::Kind kind,
                                 QVector<QPointF> meterPoints,
                                 const QPointF& sceneUnitsPerMeter,
                                 QGraphicsItem* parent)
    : QGraphicsItem(parent)
    , kind_(kind)
    , meterPoints_(std::move(meterPoints))
    , sceneUnitsPerMeter_(sceneUnitsPerMeter)
{
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setAcceptedMouseButtons(Qt::NoButton);
    setZValue(10000.0);
    rebuildGeometry();
}

QRectF MeasurementItem::boundingRect() const
{
    return bounds_;
}

void MeasurementItem::paint(QPainter* painter,
                            const QStyleOptionGraphicsItem*,
                            QWidget*)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QPen pen(QColor(0, 102, 204));
    pen.setWidthF(2.0);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->setBrush(kind_ == famp::measurement::Kind::Area
                          ? QBrush(QColor(0, 102, 204, 45))
                          : Qt::NoBrush);
    painter->drawPath(path_);

    painter->setBrush(QColor(255, 255, 255));
    for (const QPointF& point : scenePoints_)
        painter->drawEllipse(point, 4.0, 4.0);

    painter->setPen(QPen(QColor(45, 45, 45), 1.0));
    painter->setBrush(QColor(255, 255, 255, 230));
    painter->drawRoundedRect(labelRect_, 3.0, 3.0);
    painter->drawText(labelRect_.adjusted(5.0, 2.0, -5.0, -2.0),
                      Qt::AlignCenter,
                      label_);
    painter->restore();
}

void MeasurementItem::setSceneUnitsPerMeter(
    const QPointF& sceneUnitsPerMeter)
{
    if (qFuzzyCompare(sceneUnitsPerMeter_.x() + 1.0,
                      sceneUnitsPerMeter.x() + 1.0)
        && qFuzzyCompare(sceneUnitsPerMeter_.y() + 1.0,
                         sceneUnitsPerMeter.y() + 1.0))
    {
        return;
    }

    prepareGeometryChange();
    sceneUnitsPerMeter_ = sceneUnitsPerMeter;
    rebuildGeometry();
    update();
}

void MeasurementItem::rebuildGeometry()
{
    scenePoints_.clear();
    scenePoints_.reserve(meterPoints_.size());
    for (const QPointF& point : meterPoints_)
    {
        scenePoints_.append(QPointF(point.x() * sceneUnitsPerMeter_.x(),
                                   point.y() * sceneUnitsPerMeter_.y()));
    }

    path_ = QPainterPath();
    if (!scenePoints_.isEmpty())
    {
        path_.moveTo(scenePoints_.front());
        for (int index = 1; index < scenePoints_.size(); ++index)
            path_.lineTo(scenePoints_.at(index));
        if (kind_ == famp::measurement::Kind::Area)
            path_.closeSubpath();
    }

    value_ = famp::measurement::value(kind_, meterPoints_);
    label_ = famp::measurement::formatSummary(kind_, meterPoints_);

    QRectF pathBounds = path_.boundingRect();
    if (pathBounds.isNull() && !scenePoints_.isEmpty())
        pathBounds = QRectF(scenePoints_.front(), QSizeF(1.0, 1.0));
    const QFont labelFont;
    QFontMetricsF metrics(labelFont);
    const QSizeF labelSize = metrics.size(Qt::TextSingleLine, label_)
        + QSizeF(12.0, 6.0);
    const QPointF labelCenter = pathBounds.center() + QPointF(0.0, -16.0);
    labelRect_ = QRectF(labelCenter - QPointF(labelSize.width() / 2.0,
                                               labelSize.height() / 2.0),
                        labelSize);
    bounds_ = pathBounds.united(labelRect_).adjusted(-7.0, -7.0, 7.0, 7.0);
}
