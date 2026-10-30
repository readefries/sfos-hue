// SPDX-License-Identifier: AGPL-3.0-or-later
#include "huetls.h"

#include <QCryptographicHash>
#include <QNetworkReply>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>

namespace HueTls {

void attach(QNetworkReply *reply, const QByteArray &pinnedSha256,
            const std::function<void(const QByteArray &)> &seen,
            const std::function<void()> &mismatch)
{
#ifndef QT_NO_SSL
    QObject::connect(reply, &QNetworkReply::sslErrors, reply,
                     [reply, pinnedSha256, seen, mismatch](const QList<QSslError> &errors) {
        const QSslCertificate cert = reply->sslConfiguration().peerCertificate();
        const QByteArray digest = cert.isNull() ? QByteArray()
                                                : cert.digest(QCryptographicHash::Sha256);
        if (digest.isEmpty())
            return;
        if (pinnedSha256.isEmpty()) {
            if (seen)
                seen(digest);
            reply->ignoreSslErrors(errors);
        } else if (digest == pinnedSha256) {
            reply->ignoreSslErrors(errors);
        } else if (mismatch) {
            mismatch();
        }
    });
#else
    Q_UNUSED(reply) Q_UNUSED(pinnedSha256) Q_UNUSED(seen) Q_UNUSED(mismatch)
#endif
}

} // namespace HueTls
