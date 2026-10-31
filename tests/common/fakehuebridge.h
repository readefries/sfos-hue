// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef FAKEHUEBRIDGE_H
#define FAKEHUEBRIDGE_H

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

// Plain HTTP stand-in for a bridge: serves the fixture's resources, keeps event streams open to
// push events into, records every request and answers pairing with scripted replies.
class FakeHueBridge : public QTcpServer
{
    Q_OBJECT
public:
    struct Request {
        QByteArray method;
        QByteArray path;
        QByteArray appKey;
        QByteArray body;
    };

    QList<Request> requests;
    QJsonObject resources = fixture();
    int resourceStatus = 200;
    int streamStatus = 200;
    // Answers to POST /api, in order; the last one repeats.
    QList<QByteArray> pairReplies;
    QList<QPointer<QTcpSocket>> streams;

    FakeHueBridge() { listen(QHostAddress::LocalHost); }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }
    QString host() const { return url().toString(); }

    static QJsonObject fixture()
    {
        QFile file(QStringLiteral(FIXTURE_DIR "/fixture.json"));
        file.open(QIODevice::ReadOnly);
        return QJsonDocument::fromJson(file.readAll()).object();
    }

    int count(const QByteArray &method, const QByteArray &pathPrefix) const
    {
        int n = 0;
        for (const Request &r : requests)
            n += r.method == method && r.path.startsWith(pathPrefix);
        return n;
    }

    QList<Request> puts() const { return withMethod("PUT"); }

    QList<Request> withMethod(const QByteArray &method) const
    {
        QList<Request> list;
        for (const Request &r : requests) {
            if (r.method == method)
                list.append(r);
        }
        return list;
    }

    // Sends one SSE message carrying these events to every open stream.
    void push(const QJsonArray &events)
    {
        const QByteArray message = "id: 1:0\ndata: " + QJsonDocument(events).toJson(QJsonDocument::Compact) + "\n\n";
        for (QTcpSocket *socket : streams) {
            if (socket)
                socket->write(message);
        }
    }

    void closeStreams()
    {
        for (QTcpSocket *socket : streams) {
            if (socket)
                socket->disconnectFromHost();
        }
        streams.clear();
    }

protected:
    void incomingConnection(qintptr handle) override
    {
        QTcpSocket *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray &buffer = m_pending[socket];
            buffer += socket->readAll();
            const int headerEnd = buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
            Request request;
            const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
            request.method = requestLine.value(0);
            request.path = requestLine.value(1);
            int contentLength = 0;
            for (const QByteArray &line : lines) {
                const QByteArray lower = line.toLower();
                const QByteArray value = line.mid(line.indexOf(':') + 1).trimmed();
                if (lower.startsWith("content-length:"))
                    contentLength = value.toInt();
                if (lower.startsWith("hue-application-key:"))
                    request.appKey = value;
            }
            if (buffer.size() < headerEnd + 4 + contentLength)
                return;
            request.body = buffer.mid(headerEnd + 4, contentLength);
            m_pending.remove(socket);
            requests.append(request);
            respond(socket, request);
        });
    }

private:
    void reply(QTcpSocket *socket, int status, const QByteArray &body)
    {
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                      "Connection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    void respond(QTcpSocket *socket, const Request &request)
    {
        if (request.path == "/eventstream/clip/v2") {
            if (streamStatus != 200) {
                reply(socket, streamStatus, "{}");
                return;
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                          "Connection: close\r\n\r\n: hi\n\n");
            streams.append(socket);
        } else if (request.path == "/clip/v2/resource") {
            reply(socket, resourceStatus, QJsonDocument(resources).toJson(QJsonDocument::Compact));
        } else if (request.method == "PUT" && request.path.startsWith("/clip/v2/resource/")) {
            const QByteArray id = request.path.split('/').last();
            reply(socket, 200, "{\"data\":[{\"rid\":\"" + id + "\"}],\"errors\":[]}");
        } else if (request.method == "POST" && request.path == "/clip/v2/resource/scene") {
            reply(socket, 200, "{\"data\":[{\"rid\":\"scene-created\",\"rtype\":\"scene\"}],\"errors\":[]}");
        } else if (request.method == "DELETE" && request.path.startsWith("/clip/v2/resource/")) {
            const QByteArray id = request.path.split('/').last();
            reply(socket, 200, "{\"data\":[{\"rid\":\"" + id + "\"}],\"errors\":[]}");
        } else if (request.method == "POST" && request.path == "/api") {
            const QByteArray body = pairReplies.isEmpty() ? QByteArray("[]")
                    : pairReplies.size() == 1 ? pairReplies.first() : pairReplies.takeFirst();
            reply(socket, 200, body);
        } else if (request.path == "/api/0/config") {
            reply(socket, 200, "{\"name\":\"Fake bridge\",\"bridgeid\":\"001788FFFE00CAFE\","
                               "\"modelid\":\"BSB002\",\"apiversion\":\"1.70.0\"}");
        } else {
            reply(socket, 404, "{}");
        }
    }

    QHash<QTcpSocket *, QByteArray> m_pending;
};

#endif // FAKEHUEBRIDGE_H
