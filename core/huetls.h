// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef HUETLS_H
#define HUETLS_H

#include <QByteArray>
#include <functional>

class QNetworkReply;

// The bridge serves HTTPS with a certificate from Signify's own CA (or a self-signed one on older
// firmware), with the bridge id rather than its address as the name. So the usual checks always
// fail; instead the certificate is pinned on first use (pairing) and must match after that.
namespace HueTls {

// Without a pin (pairing, probing), any certificate is accepted and `seen` gets its SHA-256.
// With one, the reply only goes ahead when the certificate matches; otherwise it fails with an
// SSL error and `mismatch` is called.
void attach(QNetworkReply *reply, const QByteArray &pinnedSha256,
            const std::function<void(const QByteArray &sha256)> &seen = nullptr,
            const std::function<void()> &mismatch = nullptr);

} // namespace HueTls

#endif // HUETLS_H
