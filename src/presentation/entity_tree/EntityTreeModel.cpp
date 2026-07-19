#include "EntityTreeModel.h"

#include <QApplication>
#include <QDataStream>
#include <QFont>
#include <QIODevice>
#include <QMimeData>
#include <QSet>
#include <QStyle>

#include <algorithm>

namespace famp::presentation
{

using famp::workspace::EntityId;
using famp::workspace::EntityKind;
using famp::workspace::WorkspaceEntity;

EntityTreeModel::EntityTreeModel(famp::workspace::WorkspaceStore* store,
                                 QObject* parent)
    : QAbstractItemModel(parent)
    , m_store(store)
{
    Q_ASSERT(m_store != nullptr);
    connect(m_store, &famp::workspace::WorkspaceStore::entityAboutToBeInserted,
            this, [this](const EntityId& parentId, int row) {
                beginInsertRows(indexForId(parentId), row, row);
            });
    connect(m_store, &famp::workspace::WorkspaceStore::entityInserted,
            this, [this](const EntityId&) { endInsertRows(); });
    connect(m_store, &famp::workspace::WorkspaceStore::entityAboutToBeRemoved,
            this, [this](const EntityId& parentId, int row) {
                m_removePending = true;
                beginRemoveRows(indexForId(parentId), row, row);
            });
    connect(m_store, &famp::workspace::WorkspaceStore::entityRemoved,
            this, [this](const EntityId&, const QVector<EntityId>&) {
                if (m_removePending)
                {
                    endRemoveRows();
                    m_removePending = false;
                }
            });
    connect(m_store, &famp::workspace::WorkspaceStore::entityChanged,
            this, &EntityTreeModel::emitEntityAndParentChanged);
    connect(m_store, &famp::workspace::WorkspaceStore::storeAboutToReset,
            this, &EntityTreeModel::beginResetModel);
    connect(m_store, &famp::workspace::WorkspaceStore::storeReset,
            this, &EntityTreeModel::endResetModel);
}

QModelIndex EntityTreeModel::index(int row,
                                   int column,
                                   const QModelIndex& parentIndex) const
{
    if (!m_store || row < 0 || column != 0)
        return {};
    if (!parentIndex.isValid())
    {
        if (row != 0)
            return {};
        const WorkspaceEntity* root = m_store->entity(m_store->rootId());
        return root ? createIndex(0, 0, const_cast<WorkspaceEntity*>(root))
                    : QModelIndex();
    }
    if (parentIndex.column() != 0)
        return {};
    const WorkspaceEntity* parentEntity = entity(parentIndex);
    if (!parentEntity)
        return {};
    const QVector<EntityId> childIds = m_store->children(parentEntity->id);
    if (row >= childIds.size())
        return {};
    const WorkspaceEntity* child = m_store->entity(childIds.at(row));
    return child ? createIndex(row, 0, const_cast<WorkspaceEntity*>(child))
                 : QModelIndex();
}

QModelIndex EntityTreeModel::parent(const QModelIndex& childIndex) const
{
    const WorkspaceEntity* child = entity(childIndex);
    if (!child || child->id == m_store->rootId() || child->parentId.isNull())
        return {};
    const WorkspaceEntity* parentEntity = m_store->entity(child->parentId);
    if (!parentEntity)
        return {};
    return createIndex(m_store->rowOf(parentEntity->id), 0,
                       const_cast<WorkspaceEntity*>(parentEntity));
}

int EntityTreeModel::rowCount(const QModelIndex& parentIndex) const
{
    if (parentIndex.column() > 0)
        return 0;
    if (!parentIndex.isValid())
        return m_store && m_store->entity(m_store->rootId()) ? 1 : 0;
    const WorkspaceEntity* parentEntity = entity(parentIndex);
    return parentEntity ? m_store->children(parentEntity->id).size() : 0;
}

int EntityTreeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant EntityTreeModel::data(const QModelIndex& modelIndex, int role) const
{
    const WorkspaceEntity* current = entity(modelIndex);
    if (!current)
        return {};
    switch (role)
    {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return current->name;
    case Qt::DecorationRole:
        return iconForKind(current->kind);
    case Qt::CheckStateRole:
        return aggregateCheckState(*current);
    case Qt::ToolTipRole:
    {
        QStringList details{
            QStringLiteral("%1 · %2")
                .arg(famp::workspace::entityKindName(current->kind),
                     current->id.toString(QUuid::WithoutBraces))};
        if (current->assetPath.has_value())
            details.append(*current->assetPath);
        if (current->locked)
            details.append(QStringLiteral("已锁定"));
        if (current->dirty)
            details.append(QStringLiteral("未保存"));
        if (current->provenance.has_value())
            details.append(QStringLiteral("来源：%1").arg(current->provenance->operation));
        return details.join(QLatin1Char('\n'));
    }
    case Qt::FontRole:
    {
        QFont font;
        font.setItalic(current->dirty);
        font.setBold(current->kind == EntityKind::ProjectRoot);
        font.setStrikeOut(current->locked);
        return font;
    }
    case Qt::ForegroundRole:
        return current->visible ? QVariant() : QApplication::palette().brush(QPalette::Disabled, QPalette::Text);
    case EntityIdRole:
        return current->id;
    case EntityKindRole:
        return static_cast<int>(current->kind);
    case LockedRole:
        return current->locked;
    case DirtyRole:
        return current->dirty;
    case AssetPathRole:
        return current->assetPath.has_value() ? QVariant(*current->assetPath) : QVariant();
    case HasPayloadRole:
        return current->hasPayload();
    case HasProvenanceRole:
        return current->provenance.has_value();
    default:
        return {};
    }
}

bool EntityTreeModel::setData(const QModelIndex& modelIndex,
                              const QVariant& value,
                              int role)
{
    const WorkspaceEntity* current = entity(modelIndex);
    if (!current)
        return false;
    if (role == Qt::EditRole)
        return m_store->setName(current->id, value.toString());
    if (role == Qt::CheckStateRole)
    {
        const Qt::CheckState state = static_cast<Qt::CheckState>(value.toInt());
        if (state == Qt::PartiallyChecked)
            return false;
        return m_store->setVisible(current->id, state == Qt::Checked, true);
    }
    return false;
}

QVariant EntityTreeModel::headerData(int section,
                                     Qt::Orientation orientation,
                                     int role) const
{
    if (section == 0 && orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return QStringLiteral("实体");
    return {};
}

Qt::ItemFlags EntityTreeModel::flags(const QModelIndex& modelIndex) const
{
    if (!modelIndex.isValid())
        return Qt::ItemIsDropEnabled;
    const WorkspaceEntity* current = entity(modelIndex);
    if (!current)
        return Qt::NoItemFlags;
    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable
        | Qt::ItemIsUserCheckable;
    if (!current->locked)
    {
        result |= Qt::ItemIsEditable;
        if (current->kind != EntityKind::ProjectRoot)
            result |= Qt::ItemIsDragEnabled;
        if (famp::workspace::entityKindCanHaveChildren(current->kind))
            result |= Qt::ItemIsDropEnabled;
    }
    return result;
}

QStringList EntityTreeModel::mimeTypes() const
{
    return {QString::fromLatin1(MimeType)};
}

QMimeData* EntityTreeModel::mimeData(const QModelIndexList& indexes) const
{
    auto* mime = new QMimeData;
    QSet<EntityId> seen;
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    for (const QModelIndex& modelIndex : indexes)
    {
        if (modelIndex.column() != 0)
            continue;
        const EntityId id = entityId(modelIndex);
        if (id.isNull() || id == m_store->rootId() || seen.contains(id))
            continue;
        seen.insert(id);
        stream << id;
    }
    mime->setData(QString::fromLatin1(MimeType), encoded);
    return mime;
}

bool EntityTreeModel::canDropMimeData(const QMimeData* mime,
                                      Qt::DropAction action,
                                      int,
                                      int column,
                                      const QModelIndex& parentIndex) const
{
    if (action == Qt::IgnoreAction)
        return true;
    if (action != Qt::MoveAction || column > 0 || decodedMimeIds(mime).isEmpty())
        return false;
    const WorkspaceEntity* parentEntity = entity(parentIndex);
    if (!parentEntity)
        parentEntity = m_store->entity(m_store->rootId());
    return parentEntity
        && famp::workspace::entityKindCanHaveChildren(parentEntity->kind)
        && !parentEntity->locked;
}

bool EntityTreeModel::dropMimeData(const QMimeData* mime,
                                   Qt::DropAction action,
                                   int row,
                                   int column,
                                   const QModelIndex& parentIndex)
{
    if (action == Qt::IgnoreAction)
        return true;
    if (!canDropMimeData(mime, action, row, column, parentIndex))
        return false;
    const WorkspaceEntity* parentEntity = entity(parentIndex);
    const EntityId parentId = parentEntity ? parentEntity->id : m_store->rootId();
    const QVector<EntityId> ids = decodedMimeIds(mime);
    int insertionRow = row;
    if (insertionRow >= 0)
    {
        // Qt reports the drop row before the dragged rows are removed. The
        // store accepts the final row, so compensate for selected siblings
        // that were above the drop indicator.
        for (const EntityId& id : ids)
        {
            const WorkspaceEntity* current = m_store->entity(id);
            if (current && current->parentId == parentId
                && m_store->rowOf(id) < row)
            {
                --insertionRow;
            }
        }
        insertionRow = std::max(0, insertionRow);
    }
    return m_store->moveEntities(ids, parentId, insertionRow);
}

Qt::DropActions EntityTreeModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

void EntityTreeModel::sort(int column, Qt::SortOrder order)
{
    if (column == 0)
        m_store->sortChildren(m_store->rootId(), order);
}

QModelIndex EntityTreeModel::indexForId(const EntityId& id) const
{
    const WorkspaceEntity* current = m_store->entity(id);
    if (!current)
        return {};
    if (id == m_store->rootId())
        return createIndex(0, 0, const_cast<WorkspaceEntity*>(current));
    const QModelIndex parentIndex = indexForId(current->parentId);
    return index(m_store->rowOf(id), 0, parentIndex);
}

EntityId EntityTreeModel::entityId(const QModelIndex& modelIndex) const
{
    const WorkspaceEntity* current = entity(modelIndex);
    return current ? current->id : EntityId();
}

const WorkspaceEntity* EntityTreeModel::entity(const QModelIndex& modelIndex) const
{
    if (!modelIndex.isValid())
        return nullptr;
    return static_cast<const WorkspaceEntity*>(modelIndex.internalPointer());
}

famp::workspace::WorkspaceStore* EntityTreeModel::store() const
{
    return m_store;
}

Qt::CheckState EntityTreeModel::aggregateCheckState(
    const WorkspaceEntity& current) const
{
    if (!famp::workspace::entityKindCanHaveChildren(current.kind))
        return current.visible ? Qt::Checked : Qt::Unchecked;
    const QVector<EntityId> childIds = m_store->children(current.id);
    if (childIds.isEmpty())
        return current.visible ? Qt::Checked : Qt::Unchecked;
    int visibleCount = 0;
    int hiddenCount = 0;
    for (const EntityId& childId : childIds)
    {
        const WorkspaceEntity* child = m_store->entity(childId);
        if (!child)
            continue;
        const Qt::CheckState childState = aggregateCheckState(*child);
        if (childState == Qt::PartiallyChecked)
            return Qt::PartiallyChecked;
        if (childState == Qt::Checked)
            ++visibleCount;
        else
            ++hiddenCount;
    }
    if (visibleCount && hiddenCount)
        return Qt::PartiallyChecked;
    return visibleCount ? Qt::Checked : Qt::Unchecked;
}

QIcon EntityTreeModel::iconForKind(EntityKind kind) const
{
    QStyle* style = QApplication::style();
    if (kind == EntityKind::ProjectRoot || kind == EntityKind::Group)
        return style->standardIcon(QStyle::SP_DirIcon);
    if (kind == EntityKind::PointCloud)
        return QIcon(QStringLiteral(":/images/images/dbCloudSymbol.png"));
    return style->standardIcon(QStyle::SP_FileIcon);
}

void EntityTreeModel::emitEntityAndParentChanged(const EntityId& id,
                                                 const QVector<int>& roles)
{
    const QModelIndex currentIndex = indexForId(id);
    if (currentIndex.isValid())
        emit dataChanged(currentIndex, currentIndex, roles);
    const QModelIndex parentIndex = currentIndex.parent();
    if (parentIndex.isValid())
        emit dataChanged(parentIndex, parentIndex, {Qt::CheckStateRole});
}

QVector<EntityId> EntityTreeModel::decodedMimeIds(const QMimeData* mime) const
{
    QVector<EntityId> ids;
    if (!mime || !mime->hasFormat(QString::fromLatin1(MimeType)))
        return ids;
    const QByteArray encoded = mime->data(QString::fromLatin1(MimeType));
    QDataStream stream(encoded);
    while (!stream.atEnd())
    {
        EntityId id;
        stream >> id;
        if (!id.isNull() && !ids.contains(id))
            ids.append(id);
    }
    return ids;
}

} // namespace famp::presentation
