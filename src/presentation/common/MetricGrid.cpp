#include "MetricGrid.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <cmath>

namespace
{
int gridLinePosition(qint64 index)
{
    return static_cast<int>((index % 10 + 10) % 10);
}

bool validStep(qreal value)
{
    return std::isfinite(value) && value > 0.0;
}

bool gridIndexRange(qreal minimum,
                    qreal maximum,
                    qreal step,
                    qint64& first,
                    qint64& last)
{
    // Keep conversion to qint64 defined on platforms where qreal/long double
    // cannot represent the full integer range exactly. Ten thousand lines per
    // axis is already far beyond an A-series sheet while preventing a corrupt
    // scene transform from freezing the GUI or exhausting memory.
    constexpr long double maximumExactIndex = 9007199254740991.0L;
    constexpr long double maximumLineCount = 10000.0L;
    const long double preciseStep = static_cast<long double>(step);
    const long double firstValue = std::ceil(
        static_cast<long double>(minimum) / preciseStep);
    const long double lastValue = std::floor(
        static_cast<long double>(maximum) / preciseStep);
    if (!std::isfinite(firstValue) || !std::isfinite(lastValue)
        || firstValue < -maximumExactIndex
        || firstValue > maximumExactIndex
        || lastValue < -maximumExactIndex
        || lastValue > maximumExactIndex
        || lastValue < firstValue
        || lastValue - firstValue + 1.0L > maximumLineCount)
    {
        return false;
    }

    first = static_cast<qint64>(firstValue);
    last = static_cast<qint64>(lastValue);
    return true;
}
}

namespace famp::metricgrid
{
void draw(QPainter& painter,
          const QRectF& bounds,
          const QPointF& unitsPerMillimeter,
          const Style& style)
{
    if (!bounds.isValid() || bounds.isEmpty()
        || !validStep(unitsPerMillimeter.x())
        || !validStep(unitsPerMillimeter.y()))
    {
        return;
    }

    const qreal stepX = unitsPerMillimeter.x();
    const qreal stepY = unitsPerMillimeter.y();
    qint64 firstX = 0;
    qint64 lastX = 0;
    qint64 firstY = 0;
    qint64 lastY = 0;
    if (!gridIndexRange(bounds.left(), bounds.right(), stepX, firstX, lastX)
        || !gridIndexRange(
            bounds.top(), bounds.bottom(), stepY, firstY, lastY))
    {
        return;
    }

    QPainterPath minorLines;
    QPainterPath halfCentimeterLines;
    QPainterPath centimeterLines;

    for (qint64 index = firstX;; ++index)
    {
        QPainterPath* path = &minorLines;
        const int position = gridLinePosition(index);
        if (position == 0)
            path = &centimeterLines;
        else if (position == 5)
            path = &halfCentimeterLines;

        const qreal x = static_cast<qreal>(index) * stepX;
        path->moveTo(x, bounds.top());
        path->lineTo(x, bounds.bottom());
        if (index == lastX)
            break;
    }

    for (qint64 index = firstY;; ++index)
    {
        QPainterPath* path = &minorLines;
        const int position = gridLinePosition(index);
        if (position == 0)
            path = &centimeterLines;
        else if (position == 5)
            path = &halfCentimeterLines;

        const qreal y = static_cast<qreal>(index) * stepY;
        path->moveTo(bounds.left(), y);
        path->lineTo(bounds.right(), y);
        if (index == lastY)
            break;
    }

    painter.save();
    painter.setClipRect(bounds, Qt::IntersectClip);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(style.color);
    pen.setCosmetic(style.cosmetic);
    pen.setCapStyle(Qt::FlatCap);

    pen.setWidthF(style.minorLineWidth);
    painter.setPen(pen);
    painter.drawPath(minorLines);

    pen.setWidthF(style.halfCentimeterLineWidth);
    painter.setPen(pen);
    painter.drawPath(halfCentimeterLines);

    pen.setWidthF(style.centimeterLineWidth);
    painter.setPen(pen);
    painter.drawPath(centimeterLines);
    painter.restore();
}
}
