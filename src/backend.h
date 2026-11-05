#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QVariantList>
#include <functional>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool hasToken READ hasToken NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY entriesChanged)
    Q_PROPERTY(int filter READ filter WRITE setFilter NOTIFY entriesChanged)
    Q_PROPERTY(int total READ total NOTIFY entriesChanged)
    Q_PROPERTY(bool demo READ demo CONSTANT)
public:
    explicit Backend(bool demo = false, const QUrl &base = QUrl("https://forge.laravel.com/api/"), QObject *parent = nullptr);
    QVariantList entries() const;
    QVariantList allEntries() const { return m_entries; }
    bool busy() const { return m_busy; }
    bool hasToken() const { return !m_token.isEmpty(); }
    QString error() const { return m_error; }
    QString message() const { return m_message; }
    QString query() const { return m_query; }
    int filter() const { return m_filter; }
    int total() const { return m_entries.size(); }
    bool demo() const { return m_demo; }
    void setQuery(const QString &query);
    void setFilter(int filter);
    Q_INVOKABLE void authenticate(const QString &token, bool remember);
    Q_INVOKABLE void restoreToken();
    Q_INVOKABLE void forgetToken();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString command(const QVariantMap &entry, const QString &host = {}, const QString &user = {}, int port = 0, const QString &identity = {});
    Q_INVOKABLE bool connectEntry(const QVariantMap &entry, const QString &host = {}, const QString &user = {}, int port = 0, const QString &identity = {});
    Q_INVOKABLE void copyCommand(const QString &command);
    Q_INVOKABLE void openTokenPage();
    Q_INVOKABLE void clearError();
    static QStringList sshArguments(const QVariantMap &entry, const QString &host = {}, const QString &user = {}, int port = 0, const QString &identity = {}, QString *error = nullptr);
    static QString shellQuote(const QString &text);
signals:
    void entriesChanged();
    void stateChanged();
    void refreshFinished();
private:
    struct Task { QUrl url; QString kind; QString org; };
    void next();
    void consume(const QJsonObject &document, const Task &task);
    void rebuild();
    void fail(const QString &error);
    void secret(const QStringList &arguments, const QByteArray &input, std::function<void(int, const QByteArray &)> callback);
    QUrl endpoint(const QString &path) const;
    QUrl m_base;
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QQueue<Task> m_tasks;
    QSet<QString> m_seen;
    QVariantList m_entries, m_servers, m_sites;
    QString m_token, m_query, m_error, m_message;
    bool m_busy = false, m_demo = false, m_secretBusy = false;
    int m_filter = 0, m_generation = 0, m_authGeneration = 0;
};
