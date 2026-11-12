#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSslError>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

#include <functional>

// Асинхронный JSON-RPC поверх WebSocket.
// Запрос: {id, action, ...}  Ответ: {id, ok, error?, ...}
class WsClient : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QJsonObject &reply, qint64 elapsedMs)>;
    using SslErrorHandler = std::function<bool(const QList<QSslError> &)>;

    explicit WsClient(QObject *parent = nullptr);

    void open(const QUrl &url);
    void close();
    bool isConnected() const;
    QUrl url() const { return m_url; }

    int request(const QString &action, QJsonObject params, Callback cb = {});
    int query(const QString &cql, int maxRows, Callback cb);

    // Обработчик ошибок SSL: вернуть true, чтобы их проигнорировать.
    void setSslErrorHandler(SslErrorHandler h) { m_sslHandler = std::move(h); }

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);
    void log(const QString &level, const QString &message);

private slots:
    void onTextMessage(const QString &message);
    void onDisconnected();

private:
    struct Pending
    {
        QString       action;
        Callback      cb;
        QElapsedTimer timer;
    };

    void failAll(const QString &reason);

    QWebSocket           m_ws;
    QUrl                 m_url;
    int                  m_nextId = 1;
    QHash<int, Pending>  m_pending;
    QTimer               m_ping;
    SslErrorHandler      m_sslHandler;
};
