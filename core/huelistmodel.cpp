// SPDX-License-Identifier: AGPL-3.0-or-later
#include "huelistmodel.h"

#include <algorithm>

HueListModel::HueListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void HueListModel::setStore(HueResourceStore *store)
{
    if (m_store == store)
        return;
    if (m_store)
        m_store->disconnect(this);
    m_store = store;
    if (m_store) {
        connect(m_store.data(), &HueResourceStore::resourcesReset, this, [this]() { refresh(); });
        connect(m_store.data(), &HueResourceStore::resourceAdded, this, &HueListModel::refresh);
        connect(m_store.data(), &HueResourceStore::resourceChanged, this, &HueListModel::refresh);
        connect(m_store.data(), &HueResourceStore::resourceRemoved, this, &HueListModel::refresh);
    }
    emit storeChanged();
    refresh();
}

int HueListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_ids.size();
}

QVariant HueListModel::data(const QModelIndex &index, int role) const
{
    if (!m_store || !index.isValid() || index.row() >= m_ids.size())
        return QVariant();
    return dataFor(m_ids.at(index.row()), role);
}

QStringList HueListModel::dependenciesOf(const QString &id) const
{
    return QStringList(id);
}

void HueListModel::refresh(const QString &changedId, const QString &changedType)
{
    const QStringList ids = m_store ? collectIds() : QStringList();
    const bool reset = ids != m_ids;
    if (reset) {
        const int oldCount = m_ids.size();
        beginResetModel();
        m_ids = ids;
        endResetModel();
        if (oldCount != m_ids.size())
            emit countChanged();
    }

    m_rowsByDependency.clear();
    for (int row = 0; row < m_ids.size(); ++row) {
        for (const QString &dependency : dependenciesOf(m_ids.at(row)))
            m_rowsByDependency[dependency].append(row);
    }
    if (reset || m_ids.isEmpty())
        return;

    // Containers (and relations between resources) changed: anything may look different.
    static const QStringList broad = {QStringLiteral("room"), QStringLiteral("zone"),
                                      QStringLiteral("device")};
    if (changedId.isEmpty() || broad.contains(changedType)) {
        emit dataChanged(index(0), index(m_ids.size() - 1));
        return;
    }
    for (int row : m_rowsByDependency.value(changedId))
        emit dataChanged(index(row), index(row));
}

QStringList HueListModel::sortedByName(QStringList ids) const
{
    HueResourceStore *store = m_store;
    std::sort(ids.begin(), ids.end(), [store](const QString &a, const QString &b) {
        const int c = store->name(a).localeAwareCompare(store->name(b));
        return c != 0 ? c < 0 : a < b;
    });
    return ids;
}
