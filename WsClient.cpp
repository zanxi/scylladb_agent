#include "WsClient.h"

#include <QJsonDocument>

WsClient::WsClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_ws, &QWebSocket::connected, this, [this] {
        emit log(QStringLiteral("OK"), tr("Соединение установлено: %1").arg(m_url.toString()));
        m_ping.start();
        emit connected();
    });
    connect(&m_ws, &QWebSocket::disconnected, this, &WsClient::onDisconnected);
    connect(&m_ws, &QWebSocket::textMessageReceived, this, &WsClient::onTextMessage);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(&m_ws, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
#else
    connect(&m_ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
            [this](QAbstractSocket::SocketError) {
#endif
        emit log(QStringLiteral("ERROR"), tr("Ошибка WebSocket: %1").arg(m_ws.errorString()));
        emit errorOccurred(m_ws.errorString());
    });

#ifndef QT_NO_SSL
    connect(&m_ws, &QWebSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        QStringList msgs;
        for (const auto &e : errors) msgs << e.errorString();
        emit log(QStringLiteral("WARN"), tr("Ошибки SSL: %1").arg(msgs.join("; ")));
        if (m_sslHandler && m_sslHandler(errors))
            m_ws.ignoreSslErrors();
    });
#endif

    // Поддержание соединения.
    m_ping.setInterval(30000);
    connect(&m_ping, &QTimer::timeout, this, [this] {
        if (isConnected()) m_ws.ping();
    });
}

void WsClient::open(const QUrl &url)
{
    if (m_ws.state() != QAbstractSocket::UnconnectedState)
        m_ws.abort();
    m_url = url;
    emit log(QStringLiteral("INFO"), tr("Подключение к %1 ...").arg(url.toString()));
    m_ws.open(url);
}

void WsClient::close()
{
    m_ping.stop();
    if (m_ws.state() != QAbstractSocket::UnconnectedState)
        m_ws.close();
}

bool WsClient::isConnected() const
{
    return m_ws.state() == QAbstractSocket::ConnectedState;
}

int WsClient::request(const QString &action, QJsonObject params, Callback cb)
{
    const int id = m_nextId++;
    if (!isConnected()) {
        if (cb) cb(QJsonObject{ { "ok", false }, { "error", tr("Нет соединения с сервером") } }, 0);
        return -1;
    }
    params.insert("id", id);
    params.insert("action", action);

    Pending p;
    p.action = action;
    p.cb = std::move(cb);
    p.timer.start();
    m_pending.insert(id, std::move(p));

    m_ws.sendTextMessage(QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact)));
    return id;
}

int WsClient::query(const QString &cql, int maxRows, Callback cb)
{
    QJsonObject p{ { "cql", cql } };
    if (maxRows > 0) p.insert("max_rows", maxRows);
    return request(QStringLiteral("query"), p, std::move(cb));
}

void WsClient::onTextMessage(const QString &message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        emit log(QStringLiteral("ERROR"), tr("Некорректный ответ сервера"));
        return;
    }
    const QJsonObject reply = doc.object();
    const int id = reply.value("id").toInt(-1);
    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        if (!reply.value("ok").toBool())
            emit log(QStringLiteral("ERROR"), reply.value("error").toString());
        return;
    }
    Pending p = std::move(it.value());
    m_pending.erase(it);
    if (p.cb)
        p.cb(reply, p.timer.elapsed());
}

void WsClient::onDisconnected()
{
    m_ping.stop();
    emit log(QStringLiteral("WARN"), tr("Соединение закрыто"));
    failAll(tr("Соединение закрыто"));
    emit disconnected();
}

void WsClient::failAll(const QString &reason)
{
    auto pending = std::move(m_pending);
    m_pending.clear();
    for (auto it = pending.begin(); it != pending.end(); ++it)
        if (it->cb)
            it->cb(QJsonObject{ { "ok", false }, { "error", reason } }, it->timer.elapsed());
}
