#include "GraphicsUndoCommands.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QUndoCommand>

#include <algorithm>

namespace
{
bool fuzzyEqual(qreal left, qreal right)
{
    return qAbs(left - right) <= 1.0e-9;
}

void applyState(const famp::graphics::ItemState& state)
{
    QGraphicsItem* item = state.handle ? state.handle->item : nullptr;
    if (!item)
        return;

    item->setPos(state.position);
    item->setTransformOriginPoint(state.transformOrigin);
    item->setTransform(state.transform);
    item->setRotation(state.rotation);
    item->setScale(state.scale);
    item->setZValue(state.zValue);
}

class TransformItemsCommand final : public QUndoCommand
{
public:
    TransformItemsCommand(QVector<famp::graphics::ItemState> before,
                          QVector<famp::graphics::ItemState> after,
                          const QString& text)
        : before_(std::move(before))
        , after_(std::move(after))
    {
        setText(text);
    }

    void undo() override
    {
        for (const auto& state : before_)
            applyState(state);
    }

    void redo() override
    {
        for (const auto& state : after_)
            applyState(state);
    }

private:
    QVector<famp::graphics::ItemState> before_;
    QVector<famp::graphics::ItemState> after_;
};

class AddItemCommand final : public QUndoCommand
{
public:
    AddItemCommand(QGraphicsScene* scene,
                   famp::graphics::ItemHandle handle,
                   const QString& text)
        : scene_(scene)
        , handle_(std::move(handle))
    {
        setText(text);
        if (handle_)
            handle_->deleteWhenDetached = true;
    }

    void undo() override
    {
        if (scene_ && handle_ && handle_->item
            && handle_->item->scene() == scene_)
        {
            scene_->removeItem(handle_->item);
        }
    }

    void redo() override
    {
        if (scene_ && handle_ && handle_->item
            && !handle_->item->scene())
        {
            scene_->addItem(handle_->item);
            handle_->item->setSelected(true);
        }
    }

private:
    QGraphicsScene* scene_ = nullptr;
    famp::graphics::ItemHandle handle_;
};

struct RemovedItem
{
    famp::graphics::ItemHandle handle;
    QGraphicsItem* parent = nullptr;
    famp::graphics::ItemState state;
    bool selected = false;
};

class RemoveItemsCommand final : public QUndoCommand
{
public:
    RemoveItemsCommand(QGraphicsScene* scene,
                       const QVector<famp::graphics::ItemHandle>& handles,
                       const QString& text)
        : scene_(scene)
    {
        setText(text);

        QVector<QGraphicsItem*> candidates;
        for (const auto& handle : handles)
        {
            if (handle && handle->item && !candidates.contains(handle->item))
                candidates.push_back(handle->item);
        }

        for (const auto& handle : handles)
        {
            QGraphicsItem* item = handle ? handle->item : nullptr;
            if (!item || std::any_of(candidates.cbegin(), candidates.cend(),
                                    [item](QGraphicsItem* candidate) {
                                        return candidate != item
                                            && candidate->isAncestorOf(item);
                                    }))
            {
                continue;
            }

            RemovedItem removed;
            removed.handle = handle;
            removed.handle->deleteWhenDetached = true;
            removed.parent = item->parentItem();
            removed.state = famp::graphics::captureItemStates({handle}).front();
            removed.selected = item->isSelected();
            removed_.push_back(std::move(removed));
        }
    }

    void undo() override
    {
        for (auto& removed : removed_)
        {
            QGraphicsItem* item = removed.handle ? removed.handle->item : nullptr;
            if (!scene_ || !item)
                continue;

            if (!item->scene())
                scene_->addItem(item);
            if (removed.parent && removed.parent->scene() == scene_)
            {
                item->setParentItem(removed.parent);
            }
            applyState(removed.state);
            item->setSelected(removed.selected);
        }
    }

    void redo() override
    {
        for (auto& removed : removed_)
        {
            QGraphicsItem* item = removed.handle ? removed.handle->item : nullptr;
            if (!scene_ || !item || item->scene() != scene_)
                continue;

            if (item->parentItem())
                item->setParentItem(nullptr);
            scene_->removeItem(item);
        }
    }

private:
    QGraphicsScene* scene_ = nullptr;
    QVector<RemovedItem> removed_;
};

class TextFontCommand final : public QUndoCommand
{
public:
    TextFontCommand(famp::graphics::ItemHandle handle,
                    QFont before,
                    QFont after,
                    const QString& text)
        : handle_(std::move(handle))
        , before_(std::move(before))
        , after_(std::move(after))
    {
        setText(text);
    }

    void undo() override
    {
        apply(before_);
    }

    void redo() override
    {
        apply(after_);
    }

private:
    void apply(const QFont& font)
    {
        QGraphicsItem* item = handle_ ? handle_->item : nullptr;
        if (auto* textItem = qgraphicsitem_cast<QGraphicsTextItem*>(item))
            textItem->setFont(font);
    }

    famp::graphics::ItemHandle handle_;
    QFont before_;
    QFont after_;
};
}

namespace famp::graphics
{
ItemLifetime::ItemLifetime(QGraphicsItem* graphicsItem)
    : item(graphicsItem)
{
}

ItemLifetime::~ItemLifetime()
{
    if (deleteWhenDetached && item && !item->scene())
        delete item;
}

QVector<ItemState> captureItemStates(const QVector<ItemHandle>& handles)
{
    QVector<ItemState> states;
    states.reserve(handles.size());
    for (const auto& handle : handles)
    {
        QGraphicsItem* item = handle ? handle->item : nullptr;
        if (!item)
            continue;

        ItemState state;
        state.handle = handle;
        state.position = item->pos();
        state.transformOrigin = item->transformOriginPoint();
        state.transform = item->transform();
        state.rotation = item->rotation();
        state.scale = item->scale();
        state.zValue = item->zValue();
        states.push_back(std::move(state));
    }
    return states;
}

bool itemStatesEqual(const QVector<ItemState>& left,
                     const QVector<ItemState>& right)
{
    if (left.size() != right.size())
        return false;

    for (int index = 0; index < left.size(); ++index)
    {
        const ItemState& lhs = left.at(index);
        const ItemState& rhs = right.at(index);
        if (lhs.handle.get() != rhs.handle.get()
            || lhs.position != rhs.position
            || lhs.transformOrigin != rhs.transformOrigin
            || lhs.transform != rhs.transform
            || !fuzzyEqual(lhs.rotation, rhs.rotation)
            || !fuzzyEqual(lhs.scale, rhs.scale)
            || !fuzzyEqual(lhs.zValue, rhs.zValue))
        {
            return false;
        }
    }
    return true;
}

QUndoCommand* makeTransformCommand(const QVector<ItemState>& before,
                                   const QVector<ItemState>& after,
                                   const QString& text)
{
    return new TransformItemsCommand(before, after, text);
}

QUndoCommand* makeAddItemCommand(QGraphicsScene* scene,
                                 const ItemHandle& handle,
                                 const QString& text)
{
    return new AddItemCommand(scene, handle, text);
}

QUndoCommand* makeRemoveItemsCommand(QGraphicsScene* scene,
                                     const QVector<ItemHandle>& handles,
                                     const QString& text)
{
    return new RemoveItemsCommand(scene, handles, text);
}

QUndoCommand* makeTextFontCommand(const ItemHandle& handle,
                                  const QFont& before,
                                  const QFont& after,
                                  const QString& text)
{
    return new TextFontCommand(handle, before, after, text);
}
}
