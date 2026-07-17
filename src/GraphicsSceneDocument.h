#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

class QGraphicsItem;
class QGraphicsScene;

namespace famp::graphicsdoc
{
inline constexpr int SchemaVersion = 2;
inline constexpr int MinimumSupportedSchemaVersion = 1;
inline constexpr int TransientItemDataKey = 0x46414D50;

QJsonObject saveScene(QGraphicsScene* scene,
                      QString* errorMessage = nullptr);

bool validateSceneDocument(const QJsonObject& document,
                           QString* errorMessage = nullptr);

bool restoreScene(QGraphicsScene* scene,
                  const QJsonObject& document,
                  QList<QGraphicsItem*>* restoredItems = nullptr,
                  QString* errorMessage = nullptr);
}
