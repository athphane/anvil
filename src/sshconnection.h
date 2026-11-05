#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <functional>

class SshConnection : public QObject {
    Q_OBJECT
public:
    explicit SshConnection(const QUrl &base, const QString &keyDirectory = {}, const QString &ssh = {},
                           const QString &terminal = {}, int pollMilliseconds = 2000, int installTimeout = 60000,
                           QObject *parent = nullptr);
    ~SshConnection() override;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString suggestedPublicKey() const;
    static QString describeSshFailure(const QString &diagnostics, const QString &host, int port, bool timedOut = false);
    bool start(const QString &token, const QVariantMap &entry, const QStringList &arguments,
               const QString &identity, const QString &publicKey, bool installMissing, bool sameHost, bool launchTerminal = true);
    void cancel();
signals:
    void changed();
    void failed(const QString &error);
    void finished(bool success);
private:
    void setStatus(const QString &status);
    void provision();
    bool readPublicKey();
    void listKeys();
    void listPage(const QUrl &url);
    void createKey();
    void request(const QByteArray &method, const QUrl &url, const QJsonObject &body,
                 std::function<void(int, const QJsonObject &)> callback);
    void openTerminal();
    void fail(const QString &error);
    void stop();
    bool safeUrl(const QUrl &url) const;
    QUrl m_base, m_keysUrl;
    QString m_keyDirectory, m_ssh, m_terminal, m_token, m_status, m_identity, m_publicPath, m_publicKey, m_keyName, m_user;
    QVariantMap m_entry;
    QStringList m_arguments;
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QProcess m_probe;
    QTimer m_probeTimeout, m_poll, m_deadline;
    QSet<QString> m_pages;
    bool m_busy = false, m_sameHost = false, m_posted = false, m_timedOut = false;
    bool m_launchTerminal = true;
    int m_installTimeout;
};
