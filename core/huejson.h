// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUEJSON_H
#define HUEJSON_H

#include <QJsonObject>
#include <QString>

namespace HueJson {

// `patch` laid over `base`: nested objects are merged recursively, everything else (arrays
// included) is replaced. That's how the bridge's update events describe changes.
QJsonObject merged(const QJsonObject &base, const QJsonObject &patch);

// obj[a][b][c]..., or an undefined value if any step is missing.
QJsonValue at(const QJsonObject &obj, const QStringList &path);

// The rid of the first {rid, rtype} reference in obj[key] whose rtype matches.
QString firstRef(const QJsonObject &obj, const QString &key, const QString &rtype);
QStringList refs(const QJsonObject &obj, const QString &key, const QString &rtype);

} // namespace HueJson

#endif // HUEJSON_H
