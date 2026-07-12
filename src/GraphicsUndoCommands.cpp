#include "GraphicsUndoCommands.h"

#include <QGraphicsItem>
#include <QGraphicsItemGroup>
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

class GroupItemsCommand final : public QUndoCommand
{
public:
    GroupItemsCommand(QGraphicsScene* scene,
                      famp::graphics::ItemHandle group,
                      QVector<famp::graphics::ItemHandle> children,
                      bool initiallyGrouped,
                      const QString& text)
        : scene_(scene)
        , group_(std::move(group))
        , children_(std::move(children))
        , initiallyGrouped_(initiallyGrouped)
    {
        setText(text);
        if (group_)
            group_->deleteWhenDetached = true;
    }

    void undo() override
    {
        if (initiallyGrouped_)
            groupItems();
        else
            ungroupItems();
    }

    void redo() override
    {
        if (initiallyGrouped_)
            ungroupItems();
        else
            groupItems();
    }

private:
    QGraphicsItemGroup* groupItem() const
    {
        return group_ ? dynamic_cast<QGraphicsItemGroup*>(group_->item) : nullptr;
    }

    void groupItems()
    {
        QGraphicsItemGroup* group = groupItem();
        if (!scene_ || !group)
            return;
        if (!group->scene())
            scene_->addItem(group);
        scene_->clearSelection();
        for (const auto& childHandle : children_)
        {
            QGraphicsItem* child = childHandle ? childHandle->item : nullptr;
            if (child && child->scene() == scene_
                && child->parentItem() != group)
            {
                group->addToGroup(child);
            }
        }
        group->setSelected(true);
    }

    void ungroupItems()
    {
        QGraphicsItemGroup* group = groupItem();
        if (!scene_ || !group)
            return;
        group->setSelected(false);
        for (const auto& childHandle : children_)
        {
            QGraphicsItem* child = childHandle ? childHandle->item : nullptr;
            if (child && child->parentItem() == group)
            {
                group->removeFromGroup(child);
                child->setSelected(true);
            }
        }
        if (group->scene() == scene_)
            scene_->removeItem(group);
    }

    QGraphicsScene* scene_ = nullptr;
    famp::graphics::ItemHandle group_;
    QVector<famp::graphics::ItemHandle> children_;
    bool initiallyGrouped_ = false;
};

class CallbackCommand final : public QUndoCommand
{
public:
    CallbackCommand(std::function<void()> undo,
                    std::function<void()> redo,
                    const QString& text)
        : undo_(std::move(undo)), redo_(std::move(redo))
    {
        setText(text);
    }
    void undo() override { if (undo_) undo_(); }
    void redo() override { if (redo_) redo_(); }
private:
    std::function<void()> undo_;
    std::function<void()> redo_;
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

QUndoCommand* makeGroupItemsCommand(
    QGraphicsScene* scene,
    const ItemHandle& group,
    const QVector<ItemHandle>& children,
    const QString& text)
{
    return new GroupItemsCommand(scene, group, children, false, text);
}

QUndoCommand* makeUngroupItemsCommand(
    QGraphicsScene* scene,
    const ItemHandle& group,
    const QVector<ItemHandle>& children,
    const QString& text)
{
    return new GroupItemsCommand(scene, group, children, true, text);
}

QUndoCommand* makeCallbackCommand(std::function<void()> undo,
                                  std::function<void()> redo,
                                  const QString& text)
{
    return new CallbackCommand(std::move(undo), std::move(redo), text);
}
}
