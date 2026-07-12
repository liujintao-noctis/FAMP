#pragma once

#include <QFont>
#include <QList>
#include <QPointF>
#include <QString>
#include <QTransform>
#include <QVector>

#include <memory>

class QGraphicsItem;
class QGraphicsScene;
class QGraphicsTextItem;
class QUndoCommand;

namespace famp::graphics
{
struct ItemLifetime
{
    explicit ItemLifetime(QGraphicsItem* graphicsItem);
    ~ItemLifetime();

    QGraphicsItem* item = nullptr;
    bool deleteWhenDetached = false;
};

using ItemHandle = std::shared_ptr<ItemLifetime>;

struct ItemState
{
    ItemHandle handle;
    QPointF position;
    QPointF transformOrigin;
    QTransform transform;
    qreal rotation = 0.0;
    qreal scale = 1.0;
    qreal zValue = 0.0;
};

QVector<ItemState> captureItemStates(const QVector<ItemHandle>& handles);
bool itemStatesEqual(const QVector<ItemState>& left,
                     const QVector<ItemState>& right);

QUndoCommand* makeTransformCommand(const QVector<ItemState>& before,
                                   const QVector<ItemState>& after,
                                   const QString& text);
QUndoCommand* makeAddItemCommand(QGraphicsScene* scene,
                                 const ItemHandle& handle,
                                 const QString& text);
QUndoCommand* makeRemoveItemsCommand(QGraphicsScene* scene,
                                     const QVector<ItemHandle>& handles,
                                     const QString& text);
QUndoCommand* makeTextFontCommand(const ItemHandle& handle,
                                  const QFont& before,
                                  const QFont& after,
                                  const QString& text);
}
