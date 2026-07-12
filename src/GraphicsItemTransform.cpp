#include "GraphicsItemTransform.h"

#include <QGraphicsItem>

#include <cmath>

namespace famp::graphics
{
qreal normalizedRotation(qreal degrees) noexcept
{
    if (!std::isfinite(degrees))
        return 0.0;

    qreal normalized = std::fmod(degrees, 360.0);
    if (normalized <= -180.0)
        normalized += 360.0;
    else if (normalized > 180.0)
        normalized -= 360.0;
    return normalized;
}

int rotateItems(const QList<QGraphicsItem*>& items, qreal deltaDegrees)
{
    if (!std::isfinite(deltaDegrees) || qFuzzyIsNull(deltaDegrees))
        return 0;

    int rotatedCount = 0;
    for (QGraphicsItem* item : items)
    {
        if (!item)
            continue;

        item->setTransformOriginPoint(item->boundingRect().center());
        item->setRotation(normalizedRotation(item->rotation() + deltaDegrees));
        ++rotatedCount;
    }
    return rotatedCount;
}
}
