// SPDX-License-Identifier: AGPL-3.0-or-later
#include "huejson.h"

#include <QJsonArray>
#include <QStringList>

namespace HueJson {

QJsonObject merged(const QJsonObject &base, const QJsonObject &patch)
{
    QJsonObject result = base;
    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it) {
        const QJsonValue old = result.value(it.key());
        if (it.value().isObject() && old.isObject())
            result.insert(it.key(), merged(old.toObject(), it.value().toObject()));
        else
            result.insert(it.key(), it.value());
    }
    return result;
}

QJsonValue at(const QJsonObject &obj, const QStringList &path)
{
    QJsonValue value(obj);
    for (const QString &key : path) {
        if (!value.isObject())
            return QJsonValue(QJsonValue::Undefined);
        value = value.toObject().value(key);
    }
    return value;
}

QStringList refs(const QJsonObject &obj, const QString &key, const QString &rtype)
{
    QStringList ids;
    const QJsonArray list = obj.value(key).toArray();
    for (const QJsonValue &ref : list) {
        const QJsonObject r = ref.toObject();
        if (r.value(QStringLiteral("rtype")).toString() == rtype)
            ids.append(r.value(QStringLiteral("rid")).toString());
    }
    return ids;
}

QString firstRef(const QJsonObject &obj, const QString &key, const QString &rtype)
{
    const QStringList ids = refs(obj, key, rtype);
    return ids.isEmpty() ? QString() : ids.first();
}

} // namespace HueJson
