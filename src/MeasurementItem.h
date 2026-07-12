#pragma once

#include "Measurement.h"

#include <QGraphicsItem>
#include <QPainterPath>

class MeasurementItem final : public QGraphicsItem
{
public:
    static constexpr int Type = QGraphicsItem::UserType + 101;

    MeasurementItem(famp::measurement::Kind kind,
                    QVector<QPointF> meterPoints,
                    const QPointF& sceneUnitsPerMeter,
                    QGraphicsItem* parent = nullptr);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget = nullptr) override;

    famp::measurement::Kind kind() const { return kind_; }
    const QVector<QPointF>& meterPoints() const { return meterPoints_; }
    QPointF sceneUnitsPerMeter() const { return sceneUnitsPerMeter_; }
    double value() const { return value_; }
    void setSceneUnitsPerMeter(const QPointF& sceneUnitsPerMeter);

private:
    void rebuildGeometry();

    famp::measurement::Kind kind_;
    QVector<QPointF> meterPoints_;
    QPointF sceneUnitsPerMeter_;
    QVector<QPointF> scenePoints_;
    QPainterPath path_;
    QRectF bounds_;
    QRectF labelRect_;
    QString label_;
    double value_ = 0.0;
};
