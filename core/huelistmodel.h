// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUELISTMODEL_H
#define HUELISTMODEL_H

#include "hueresourcestore.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QStringList>

// A list of resource ids from the store, kept in sync with it. Subclasses pick the ids and say
// what each row shows. When the ids stay the same, only the rows a change touches are updated,
// so delegates (and sliders being dragged in them) survive the stream's updates.
class HueListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(HueResourceStore *store READ store WRITE setStore NOTIFY storeChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    explicit HueListModel(QObject *parent = nullptr);

    HueResourceStore *store() const { return m_store; }
    void setStore(HueResourceStore *store);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Q_INVOKABLE QString idAt(int row) const { return m_ids.value(row); }

signals:
    void storeChanged();
    void countChanged();

protected:
    // The ids to show, in order.
    virtual QStringList collectIds() const = 0;
    // Resources whose changes affect the row of this id; by default only the id itself.
    virtual QStringList dependenciesOf(const QString &id) const;
    virtual QVariant dataFor(const QString &id, int role) const = 0;

    void refresh(const QString &changedId = QString(), const QString &changedType = QString());
    QStringList sortedByName(QStringList ids) const;

    QPointer<HueResourceStore> m_store;

private:
    QStringList m_ids;
    QHash<QString, QList<int>> m_rowsByDependency;
};

#endif // HUELISTMODEL_H
