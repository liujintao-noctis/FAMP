#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>

class QPainter;

namespace famp::metricgrid
{
struct Style
{
    QColor color = Qt::red;
    qreal minorLineWidth = 0.5;
    qreal halfCentimeterLineWidth = 1.0;
    qreal centimeterLineWidth = 2.0;
    bool cosmetic = true;
};

// Draws a metric-paper grid whose adjacent minor lines are one physical
// millimetre apart. unitsPerMillimeter is expressed in the painter's current
// coordinate system, so the same renderer works for screens and page devices.
void draw(QPainter& painter,
          const QRectF& bounds,
          const QPointF& unitsPerMillimeter,
          const Style& style = {});
}
