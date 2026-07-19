#pragma once

#include "WorkspaceStore.h"

#include <QAbstractItemModel>
#include <QIcon>

namespace famp::presentation
{

class EntityTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Role
    {
        EntityIdRole = Qt::UserRole + 1,
        EntityKindRole,
        LockedRole,
        DirtyRole,
        AssetPathRole,
        HasPayloadRole,
        HasProvenanceRole
    };

    explicit EntityTreeModel(famp::workspace::WorkspaceStore* store,
                             QObject* parent = nullptr);

    QModelIndex index(int row,
                      int column,
                      const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index,
                 const QVariant& value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data,
                         Qt::DropAction action,
                         int row,
                         int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data,
                      Qt::DropAction action,
                      int row,
                      int column,
                      const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    QModelIndex indexForId(const famp::workspace::EntityId& id) const;
    famp::workspace::EntityId entityId(const QModelIndex& index) const;
    const famp::workspace::WorkspaceEntity* entity(const QModelIndex& index) const;
    famp::workspace::WorkspaceStore* store() const;

private:
    static constexpr const char* MimeType =
        "application/x-famp-workspace-entities";

    famp::workspace::WorkspaceStore* m_store;
    bool m_removePending = false;

    Qt::CheckState aggregateCheckState(
        const famp::workspace::WorkspaceEntity& entity) const;
    QIcon iconForKind(famp::workspace::EntityKind kind) const;
    void emitEntityAndParentChanged(const famp::workspace::EntityId& id,
                                    const QVector<int>& roles);
    QVector<famp::workspace::EntityId> decodedMimeIds(
        const QMimeData* data) const;
};

} // namespace famp::presentation
