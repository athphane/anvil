#include "backend.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
#include <memory>

Backend::Backend(bool demo, const QUrl &base, QObject *parent) : QObject(parent), m_base(base), m_connection(base), m_demo(demo) {
    connect(&m_connection, &SshConnection::changed, this, [this] { m_message = m_connection.status(); emit stateChanged(); });
    connect(&m_connection, &SshConnection::failed, this, &Backend::fail);
    connect(&m_connection, &SshConnection::finished, this, &Backend::connectionFinished);
    if (demo) {
        m_servers = {
            QVariantMap{{"id","101"},{"org","northstar"},{"name","production-eu"},{"host","203.0.113.24"},{"port",22},{"provider","hetzner"},{"region","Helsinki"},{"ready",true}},
            QVariantMap{{"id","102"},{"org","northstar"},{"name","staging"},{"host","198.51.100.12"},{"port",22},{"provider","ocean2"},{"region","Amsterdam"},{"ready",true}},
            QVariantMap{{"id","103"},{"org","studio"},{"name","worker-01"},{"host","192.0.2.18"},{"port",2222},{"provider","aws"},{"region","Singapore"},{"ready",true}}
        };
        m_sites = {
            QVariantMap{{"id","201"},{"serverId","101"},{"org","northstar"},{"name","app.example.com"},{"user","forge"},{"path","/home/forge/app.example.com/current"}},
            QVariantMap{{"id","202"},{"serverId","101"},{"org","northstar"},{"name","docs.example.com"},{"user","docs"},{"path","/home/docs/docs.example.com"}},
            QVariantMap{{"id","203"},{"serverId","102"},{"org","northstar"},{"name","preview.example.com"},{"user","forge"},{"path","/home/forge/preview.example.com"}}
        };
        rebuild();
    }
}

QVariantList Backend::entries() const {
    QVariantList result;
    for (const auto &value : m_entries) {
        const auto e = value.toMap();
        if (m_filter == 1 && e.value("kind") != "server") continue;
        if (m_filter == 2 && e.value("kind") != "site") continue;
        const QString search = e.value("name").toString() + ' ' + e.value("org").toString() + ' ' + e.value("server").toString() + ' ' + e.value("host").toString() + ' ' + e.value("user").toString();
        if (search.contains(m_query.trimmed(), Qt::CaseInsensitive)) result.append(e);
    }
    return result;
}
void Backend::setQuery(const QString &query) { if (m_query != query) { m_query = query; emit entriesChanged(); } }
void Backend::setFilter(int filter) { if (m_filter != filter) { m_filter = filter; emit entriesChanged(); } }
void Backend::fail(const QString &error) { m_error = error; emit stateChanged(); }
void Backend::clearError() { m_error.clear(); emit stateChanged(); }
QUrl Backend::endpoint(const QString &path) const { return m_base.resolved(QUrl(path)); }

void Backend::secret(const QStringList &arguments, const QByteArray &input, std::function<void(int, const QByteArray &)> callback) {
    const auto executable = QStandardPaths::findExecutable("secret-tool");
    if (executable.isEmpty()) { callback(-1, {}); return; }
    auto *process = new QProcess(this);
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    timeout->setInterval(20000);
    connect(timeout, &QTimer::timeout, process, &QProcess::kill);
    // Secrets go through stdin, so they never appear in process arguments.
    connect(process, &QProcess::started, process, [process, input] { process->write(input); process->closeWriteChannel(); });
    auto done = std::make_shared<bool>(false);
    auto finish = [process, callback, done](int code) {
        if (*done) return;
        *done = true;
        callback(code, process->readAllStandardOutput());
        process->deleteLater();
    };
    connect(process, &QProcess::errorOccurred, process, [finish](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) finish(-1); });
    connect(process, &QProcess::finished, process, [finish](int code, QProcess::ExitStatus status) { finish(status == QProcess::NormalExit ? code : -1); });
    process->start(executable, arguments);
    timeout->start();
}

void Backend::restoreToken() {
    if (m_demo || m_secretBusy || hasToken()) return;
    const int generation = ++m_authGeneration;
    m_secretBusy = true;
    secret({"lookup", "application", "anvil", "service", "laravel-forge"}, {}, [this, generation](int code, const QByteArray &result) {
        m_secretBusy = false;
        if (generation != m_authGeneration) return;
        if (code == 0 && !result.trimmed().isEmpty()) authenticate(QString::fromUtf8(result).trimmed(), false);
        else if (code != 1) fail(tr("The desktop keyring is unavailable. You can use a token for this session."));
    });
}

void Backend::authenticate(const QString &token, bool remember) {
    if (m_demo) return;
    if (m_secretBusy) { fail(tr("Please wait for the desktop keyring.")); return; }
    m_connection.cancel();
    ++m_authGeneration;
    if (token.trimmed().isEmpty() || token.contains('\n') || token.contains('\r')) { fail(tr("Enter a valid Forge API token.")); return; }
    m_token = token.trimmed();
    if (remember) {
        m_secretBusy = true;
        secret({"store", "--label=Anvil — Laravel Forge", "application", "anvil", "service", "laravel-forge"}, m_token.toUtf8(), [this](int code, const QByteArray &) {
            m_secretBusy = false;
            if (code != 0) fail(tr("The token could not be saved to your keyring. It is available for this session."));
        });
    }
    refresh();
}

void Backend::forgetToken() {
    if (m_demo) return;
    if (m_secretBusy) { fail(tr("Please wait for the desktop keyring.")); return; }
    m_connection.cancel();
    ++m_authGeneration;
    ++m_generation;
    if (m_reply) m_reply->abort();
    m_token.clear(); m_tasks.clear(); m_entries.clear(); m_servers.clear(); m_sites.clear();
    m_busy = false; m_error.clear(); m_message.clear();
    emit entriesChanged(); emit stateChanged();
    m_secretBusy = true;
    secret({"clear", "application", "anvil", "service", "laravel-forge"}, {}, [this](int code, const QByteArray &) {
        m_secretBusy = false;
        if (code != 0 && code != 1) fail(tr("The saved token could not be removed. Unlock your keyring and try again."));
    });
}

void Backend::refresh() {
    if (m_demo || m_busy || connecting() || !hasToken()) return;
    ++m_generation; m_error.clear(); m_message.clear(); m_servers.clear(); m_sites.clear(); m_entries.clear(); m_seen.clear(); m_tasks.clear();
    m_busy = true;
    emit entriesChanged(); emit stateChanged();
    m_tasks.enqueue({endpoint("orgs"), "orgs", {}});
    next();
}

void Backend::next() {
    if (m_tasks.isEmpty()) {
        m_busy = false;
        rebuild();
        int unmatched = 0;
        for (const auto &value : m_sites) {
            const auto site = value.toMap();
            const bool matched = std::any_of(m_servers.cbegin(), m_servers.cend(), [&site](const QVariant &serverValue) {
                const auto server = serverValue.toMap();
                return !site.value("serverId").toString().isEmpty() && site.value("serverId") == server.value("id") && site.value("org") == server.value("org");
            });
            if (!matched) ++unmatched;
        }
        if (unmatched > 0 && m_error.isEmpty()) fail(tr("Forge returned %n sites without a matching server. Refresh or check server access.", nullptr, unmatched));
        if (!m_servers.isEmpty() && m_sites.isEmpty() && m_error.isEmpty()) m_message = tr("Loaded %n servers; Forge returned no sites.", nullptr, m_servers.size());
        if (m_entries.isEmpty() && m_error.isEmpty()) m_message = tr("No servers or sites are visible to this token.");
        emit stateChanged(); emit refreshFinished();
        return;
    }
    const Task task = m_tasks.dequeue();
    if (m_seen.contains(task.url.toString())) { fail(tr("Forge returned a repeated pagination link.")); next(); return; }
    m_seen.insert(task.url.toString());
    QNetworkRequest request(task.url);
    request.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("User-Agent", "Anvil/0.1.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(25000);
    const int generation = m_generation;
    QNetworkReply *reply = m_network.get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, task, generation] {
        reply->deleteLater();
        if (m_reply == reply) m_reply = nullptr;
        if (generation != m_generation) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            if (status == 401) { fail(tr("Forge rejected this token. Open Account and enter a new token.")); m_tasks.clear(); }
            else if (status == 403) fail(tr("This token cannot read %1. Required scope: %2.").arg(task.url.path(), task.kind == "orgs" ? "organization:view" : "server:view"));
            else if (status == 429) {
                fail(tr("Forge rate limit reached. Wait %1 seconds, then refresh.").arg(QString::fromUtf8(reply->rawHeader("Retry-After").isEmpty() ? QByteArray("60") : reply->rawHeader("Retry-After"))));
                m_tasks.clear();
            } else fail(tr("Could not load %1 (HTTP %2). Check your connection and try again.").arg(task.url.path()).arg(status));
        } else {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(reply->readAll(), &error);
            if (error.error != QJsonParseError::NoError || !document.object().value("data").isArray()) fail(tr("Forge returned an unexpected response."));
            else consume(document.object(), task);
        }
        next();
    });
}

void Backend::consume(const QJsonObject &document, const Task &task) {
    for (const auto &value : document.value("data").toArray()) {
        const auto item = value.toObject();
        auto attributes = item.value("attributes").toObject().toVariantMap();
        if (task.kind == "servers") attributes["apiId"] = attributes.value("id", item.value("id").toVariant()).toString();
        attributes["id"] = item.value("id").toVariant().toString();
        if (task.kind == "orgs") {
            const auto org = attributes.value("slug").toString();
            if (org.isEmpty()) { fail(tr("An organization has no API slug.")); continue; }
            const auto encoded = QString::fromLatin1(QUrl::toPercentEncoding(org));
            m_tasks.enqueue({endpoint("orgs/" + encoded + "/servers"), "servers", org});
            // The server relationship is optional unless explicitly included.
            m_tasks.enqueue({endpoint("orgs/" + encoded + "/sites?include=server"), "sites", org});
        } else if (task.kind == "servers") {
            attributes["org"] = task.org;
            attributes["host"] = attributes.value("ip_address");
            attributes["port"] = attributes.value("ssh_port", 22);
            attributes["ready"] = attributes.value("is_ready", false).toBool() && !attributes.value("revoked").toBool();
            m_servers.append(attributes);
        } else {
            attributes["org"] = task.org;
            attributes["serverId"] = item.value("relationships").toObject().value("server").toObject().value("data").toObject().value("id").toVariant().toString();
            attributes["user"] = attributes.value("user", "forge");
            attributes["path"] = attributes.value("root_directory");
            m_sites.append(attributes);
        }
    }
    const auto links = document.value("links").toObject();
    QString link = links.value("next").isObject() ? links.value("next").toObject().value("href").toString() : links.value("next").toString();
    QUrl url;
    if (!link.isEmpty()) url = task.url.resolved(QUrl(link));
    else if (!document.value("meta").toObject().value("next_cursor").toString().isEmpty()) {
        url = task.url;
        QUrlQuery query(url); query.removeAllQueryItems("page[cursor]");
        query.addQueryItem("page[cursor]", document.value("meta").toObject().value("next_cursor").toString()); url.setQuery(query);
    }
    if (!url.isEmpty()) {
        // Pagination must never forward a bearer token to another origin or endpoint.
        if (url.scheme() != m_base.scheme() || url.host() != m_base.host() || url.port() != m_base.port() || url.path() != task.url.path() || !url.userInfo().isEmpty()) fail(tr("Forge returned an unsafe pagination link."));
        else {
            if (task.kind == "sites") {
                QUrlQuery query(url);
                query.removeAllQueryItems("include");
                query.addQueryItem("include", "server");
                url.setQuery(query);
            }
            m_tasks.prepend({url, task.kind, task.org});
        }
    }
    rebuild();
}

void Backend::rebuild() {
    m_entries.clear();
    for (const auto &value : m_servers) {
        auto server = value.toMap();
        server["serverId"] = server.value("apiId", server.value("id"));
        server["kind"] = "server"; server["user"] = "forge";
        server["server"] = server.value("name"); server["path"] = "";
        server["key"] = server.value("org").toString() + ":server:" + server.value("id").toString();
        m_entries.append(server);
        for (const auto &siteValue : m_sites) {
            const auto site = siteValue.toMap();
            if (site.value("serverId") != server.value("id") || site.value("org") != server.value("org")) continue;
            auto entry = server;
            for (const auto &key : {"id", "name", "user", "path"}) entry[key] = site.value(key);
            entry["kind"] = "site";
            entry["key"] = site.value("org").toString() + ":site:" + site.value("id").toString();
            m_entries.append(entry);
        }
    }
    std::sort(m_entries.begin(), m_entries.end(), [](const QVariant &a, const QVariant &b) {
        const auto x = a.toMap(), y = b.toMap();
        return (x.value("org").toString() + x.value("server").toString() + x.value("kind").toString() + x.value("name").toString()).toLower() < (y.value("org").toString() + y.value("server").toString() + y.value("kind").toString() + y.value("name").toString()).toLower();
    });
    emit entriesChanged();
}

QString Backend::shellQuote(const QString &text) { QString value = text; value.replace("'", "'\"'\"'"); return "'" + value + "'"; }
QStringList Backend::sshArguments(const QVariantMap &entry, const QString &hostOverride, const QString &userOverride, int portOverride, const QString &identity, QString *error) {
    const QString host = hostOverride.trimmed().isEmpty() ? entry.value("host").toString() : hostOverride.trimmed();
    const QString user = userOverride.trimmed().isEmpty() ? entry.value("user", "forge").toString() : userOverride.trimmed();
    const int port = portOverride > 0 ? portOverride : entry.value("port", 22).toInt();
    const QRegularExpression safeHost("^[A-Za-z0-9][A-Za-z0-9.:_-]*$"), safeUser("^[A-Za-z_][A-Za-z0-9_.-]*[$]?$" );
    if (!safeHost.match(host).hasMatch() || !safeUser.match(user).hasMatch() || port < 1 || port > 65535) {
        if (error) *error = tr("Check the SSH hostname, user, and port (1–65535).");
        return {};
    }
    QStringList args{"-t", "-p", QString::number(port)};
    if (!identity.trimmed().isEmpty()) {
        QString path = identity.trimmed();
        if (path.startsWith("~/")) path.replace(0, 1, QDir::homePath());
        if (!QFileInfo(path).isFile()) { if (error) *error = tr("The SSH identity file does not exist."); return {}; }
        args << "-i" << QFileInfo(path).absoluteFilePath();
    }
    args << "-l" << user << "--" << host;
    const QString directory = entry.value("path").toString();
    if (entry.value("kind") == "site" && !directory.isEmpty()) args << ("cd -- " + shellQuote(directory) + " && exec \"${SHELL:-/bin/bash}\" -l");
    return args;
}
QString Backend::command(const QVariantMap &entry, const QString &host, const QString &user, int port, const QString &identity) {
    QString error;
    const auto args = sshArguments(entry, host, user, port, identity, &error);
    if (args.isEmpty()) { fail(error); return {}; }
    QString result = "ssh";
    for (const auto &arg : args) result += ' ' + shellQuote(arg);
    return result;
}
bool Backend::connectEntry(const QVariantMap &entry, const QString &host, const QString &user, int port, const QString &identity, bool installMissing, const QString &publicKey) {
    if (m_demo) { fail(tr("Demo connections are disabled. Add your Forge token to connect.")); return false; }
    QString error;
    const auto args = sshArguments(entry, host, user, port, identity, &error);
    if (args.isEmpty()) { fail(error); return false; }
    if (connecting()) return false;
    m_error.clear();
    return m_connection.start(m_token, entry, args, identity, publicKey, installMissing, host.trimmed().isEmpty() || host.trimmed() == entry.value("host").toString());
}
void Backend::cancelConnection() { m_connection.cancel(); }
bool Backend::prepareEntry(const QVariantMap &entry, const QString &identity, const QString &publicKey) {
    QString error;
    const auto args = sshArguments(entry, {}, {}, 0, identity, &error);
    if (args.isEmpty()) { fail(error); return false; }
    if (m_demo || connecting()) return false;
    m_error.clear();
    return m_connection.start(m_token, entry, args, identity, publicKey, true, true, false);
}
void Backend::copyCommand(const QString &command) { if (!command.isEmpty()) { QGuiApplication::clipboard()->setText(command); m_message = tr("SSH command copied."); emit stateChanged(); } }
void Backend::openTokenPage() { if (!QDesktopServices::openUrl(QUrl("https://forge.laravel.com/profile/api"))) fail(tr("Could not open your browser.")); }
