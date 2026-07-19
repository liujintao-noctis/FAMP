#pragma once

#include <QList>
#include <QtGlobal>

class QGraphicsItem;

namespace famp::graphics
{
inline constexpr qreal RotationStepDegrees = 5.0;

qreal normalizedRotation(qreal degrees) noexcept;
int rotateItems(const QList<QGraphicsItem*>& items, qreal deltaDegrees);
}
