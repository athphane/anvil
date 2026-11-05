#include "sshconnection.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

SshConnection::SshConnection(const QUrl &base, const QString &directory, const QString &ssh,
                             const QString &terminal, int pollMilliseconds, int installTimeout, QObject *parent)
    : QObject(parent), m_base(base), m_keyDirectory(directory.isEmpty() ? QDir::homePath() + "/.ssh" : directory),
      m_ssh(ssh.isEmpty() ? QStandardPaths::findExecutable("ssh") : ssh),
      m_terminal(terminal.isEmpty() ? QStandardPaths::findExecutable("omarchy") : terminal), m_installTimeout(installTimeout) {
    m_probeTimeout.setSingleShot(true);
    m_probeTimeout.setInterval(15000);
    m_poll.setSingleShot(true);
    m_poll.setInterval(pollMilliseconds);
    m_deadline.setSingleShot(true);
    connect(&m_probeTimeout, &QTimer::timeout, this, [this] { m_timedOut = true; m_probe.kill(); });
    connect(&m_poll, &QTimer::timeout, this, &SshConnection::listKeys);
    connect(&m_deadline, &QTimer::timeout, this, [this] { fail(tr("Forge has not finished installing the SSH key. Wait a moment, then connect again.")); });
    connect(&m_probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_busy && error == QProcess::FailedToStart) fail(tr("OpenSSH could not be started."));
    });
    connect(&m_probe, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_probeTimeout.stop();
        if (!m_busy) return;
        const auto error = QString::fromUtf8(m_probe.readAllStandardError());
        if (status == QProcess::NormalExit && code == 0) { openTerminal(); return; }
        const auto host = m_arguments.value(m_arguments.indexOf("--") + 1);
        const int port = m_arguments.value(m_arguments.indexOf("-p") + 1).toInt();
        if (m_timedOut) { fail(describeSshFailure(error, host, port, true)); return; }
        if (error.contains("REMOTE HOST IDENTIFICATION HAS CHANGED")) {
            fail(describeSshFailure(error, host, port)); return;
        }
        if (error.contains("Server accepts key:")) { openTerminal(); return; }
        const bool denied = error.contains("Permission denied") && error.contains("publickey");
        const bool unknownHost = error.contains("host key is known") && error.contains("strict checking");
        if (status == QProcess::NormalExit && code == 255 && (denied || unknownHost)) { provision(); return; }
        fail(describeSshFailure(error, host, port));
    });
}

QString SshConnection::describeSshFailure(const QString &diagnostics, const QString &host, int port, bool timedOut) {
    // Verbose SSH output is useful for detecting key acceptance, but the
    // window should show the cause and a next step rather than debug traces.
    const auto error = diagnostics.toLower();
    if (error.contains("remote host identification has changed"))
        return tr("Server identity changed\nThis server's SSH identity differs from the one saved on this computer. Verify the change before connecting.");
    if (timedOut || error.contains("connection timed out") || error.contains("operation timed out"))
        return tr("Connection timed out\n%1 did not respond on port %2. Check the SSH port, firewall, or VPN, then try again.").arg(host).arg(port);
    if (error.contains("connection refused"))
        return tr("Connection refused\n%1 refused the connection on port %2. Check that SSH is running and the port is correct.").arg(host).arg(port);
    if (error.contains("could not resolve hostname") || error.contains("name or service not known"))
        return tr("Server not found\nCouldn't find %1. Check the hostname in Connection options.").arg(host);
    if (error.contains("no route to host") || error.contains("network is unreachable"))
        return tr("Server is unreachable\nCan't reach %1 on port %2. Check your network or VPN, then try again.").arg(host).arg(port);
    if (error.contains("host key verification failed"))
        return tr("Server identity needs verification\nOpen a normal SSH session to verify this server's identity, then try again.");
    if (error.contains("permission denied"))
        return tr("SSH access denied\nThe server declined access. Check the SSH user and authentication settings in Connection options.");
    if (error.contains("no matching") || error.contains("unable to negotiate"))
        return tr("SSH settings aren't compatible\nThe server and OpenSSH couldn't agree on connection settings. Check the server's SSH configuration.");
    if (error.contains("connection reset") || error.contains("connection closed"))
        return tr("Server closed the connection\n%1 ended the SSH connection. Check the server's SSH service and access rules, then try again.").arg(host);
    return tr("SSH connection failed\nCouldn't connect to %1 on port %2. Check Connection options and try again.").arg(host).arg(port);
}

SshConnection::~SshConnection() {
    stop();
    if (m_probe.state() != QProcess::NotRunning) m_probe.waitForFinished(1000);
}

QString SshConnection::suggestedPublicKey() const {
    for (const auto &name : {"id_ed25519.pub", "id_rsa.pub"}) {
        const auto path = QDir(m_keyDirectory).filePath(name);
        if (QFileInfo(path).isFile()) return path;
    }
    const auto files = QDir(m_keyDirectory).entryList({"*.pub"}, QDir::Files, QDir::Name);
    return files.size() == 1 ? QDir(m_keyDirectory).filePath(files.first()) : QString();
}

void SshConnection::setStatus(const QString &status) { m_status = status; emit changed(); }
bool SshConnection::start(const QString &token, const QVariantMap &entry, const QStringList &arguments,
                          const QString &identity, const QString &publicKey, bool installMissing, bool sameHost, bool launchTerminal) {
    if (m_busy) return false;
    if (m_probe.state() != QProcess::NotRunning) { emit failed(tr("Please wait for the previous SSH check to stop.")); return false; }
    m_entry = entry; m_arguments = arguments; m_identity = identity; m_publicPath = publicKey.trimmed();
    m_token = token; m_sameHost = sameHost; m_posted = false; m_timedOut = false;
    m_launchTerminal = launchTerminal;
    m_user = arguments.value(arguments.indexOf("-l") + 1);
    m_busy = true;
    if (m_ssh.isEmpty() || (launchTerminal && m_terminal.isEmpty())) { fail(tr("Anvil needs Omarchy and OpenSSH to open a connection.")); return false; }
    if (!installMissing) { openTerminal(); return true; }
    setStatus(tr("Checking SSH access to %1…").arg(entry.value("name").toString()));
    // A noninteractive probe must not change host trust, forward ports, or
    // reuse a multiplexed session authenticated with a different identity.
    auto probe = arguments.mid(0, arguments.indexOf("--") + 2);
    probe.removeAll("-t");
    probe = QStringList{"-v", "-T", "-o", "BatchMode=yes", "-o", "NumberOfPasswordPrompts=0",
                        "-o", "StrictHostKeyChecking=yes", "-o", "ConnectTimeout=8", "-o", "ConnectionAttempts=1",
                        "-o", "ControlMaster=no", "-o", "ControlPath=none", "-o", "ClearAllForwardings=yes",
                        "-o", "RemoteCommand=none", "-o", "RequestTTY=no"} + probe;
    probe << "true";
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    m_probe.setProcessEnvironment(environment);
    m_probe.start(m_ssh, probe);
    m_probeTimeout.start();
    return true;
}

void SshConnection::provision() {
    if (!m_sameHost) { fail(tr("Automatic key installation requires the server's Forge IP. Clear the hostname override or disable automatic key installation.")); return; }
    if (m_token.isEmpty()) { fail(tr("Add a Forge API token to install your SSH key.")); return; }
    if (!readPublicKey()) return;
    const auto org = m_entry.value("org").toString(), server = m_entry.value("serverId").toString();
    if (org.isEmpty() || server.isEmpty()) { fail(tr("This connection has no Forge server ID. Refresh your inventory.")); return; }
    m_keysUrl = m_base.resolved(QUrl("orgs/" + QString::fromLatin1(QUrl::toPercentEncoding(org)) + "/servers/" + QString::fromLatin1(QUrl::toPercentEncoding(server)) + "/ssh-keys"));
    m_deadline.start(m_installTimeout);
    setStatus(tr("Checking your public key in Forge…"));
    listKeys();
}

bool SshConnection::readPublicKey() {
    QString identity = m_identity.trimmed();
    if (identity.startsWith("~/")) identity.replace(0, 1, QDir::homePath());
    // An explicit identity always selects its matching public sidecar. Private
    // key bytes are never read by Anvil or sent to Forge.
    if (!identity.isEmpty()) m_publicPath = identity.endsWith(".pub") ? identity : identity + ".pub";
    if (m_publicPath.isEmpty()) m_publicPath = suggestedPublicKey();
    if (m_publicPath.startsWith("~/")) m_publicPath.replace(0, 1, QDir::homePath());
    QFile file(m_publicPath);
    if (!m_publicPath.endsWith(".pub") || !file.open(QIODevice::ReadOnly) || file.size() > 65536) {
        fail(tr("Choose a readable .pub public key file in Connection options. An explicit identity needs its matching .pub file.")); return false;
    }
    const auto line = QString::fromUtf8(file.readAll()).trimmed();
    const auto fields = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    const QStringList types{"ssh-ed25519", "ssh-rsa", "ecdsa-sha2-nistp256", "ecdsa-sha2-nistp384", "ecdsa-sha2-nistp521", "sk-ssh-ed25519@openssh.com", "sk-ecdsa-sha2-nistp256@openssh.com"};
    const auto decoded = fields.size() >= 2 ? QByteArray::fromBase64Encoding(fields.at(1).toLatin1(), QByteArray::AbortOnBase64DecodingErrors) : QByteArray::FromBase64Result();
    const auto blob = decoded.decoded;
    quint32 length = 0;
    if (blob.size() >= 4) for (int i = 0; i < 4; ++i) length = (length << 8) | quint8(blob.at(i));
    if (line.contains('\n') || fields.size() < 2 || !types.contains(fields.first()) || !decoded || length > quint32(blob.size() - 4) || blob.mid(4, length) != fields.first().toLatin1() || blob.size() <= int(length + 4)) {
        fail(tr("The selected file is not a supported OpenSSH public key. Private keys cannot be uploaded.")); return false;
    }
    m_publicKey = fields.at(0) + ' ' + fields.at(1);
    m_keyName = "Anvil-" + QString::fromLatin1(QCryptographicHash::hash(blob, QCryptographicHash::Sha256).toHex().left(24)) + '-' + m_user;
    if (m_identity.isEmpty()) {
        // OpenSSH can use a public identity to select the matching key from
        // the agent; prefer its private sidecar when it is on disk.
        const auto privatePath = m_publicPath.left(m_publicPath.size() - 4);
        const auto path = QFileInfo(privatePath).isFile() ? privatePath : m_publicPath;
        m_arguments = QStringList{"-i", QFileInfo(path).absoluteFilePath(), "-o", "IdentitiesOnly=yes"} + m_arguments;
    }
    return true;
}

bool SshConnection::safeUrl(const QUrl &url) const {
    return url.scheme() == m_base.scheme() && url.host() == m_base.host() && url.port() == m_base.port()
           && url.userInfo().isEmpty() && url.path() == m_keysUrl.path();
}
void SshConnection::request(const QByteArray &method, const QUrl &url, const QJsonObject &body,
                             std::function<void(int, const QJsonObject &)> callback) {
    if (!m_busy) return;
    if (!safeUrl(url)) { fail(tr("Forge returned an unsafe SSH key link.")); return; }
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Content-Type", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    auto *reply = method == "POST" ? m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)) : m_network.get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, method, callback] {
        reply->deleteLater();
        if (m_reply != reply) return;
        m_reply = nullptr;
        if (!m_busy) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 403) { fail(tr("Forge permission needed\nYour token needs %1 for this server. Update its permissions in Forge, then try again.").arg(method == "POST" ? "server:create-keys" : "server:view")); return; }
        if (status == 401) { fail(tr("Forge rejected this token. Open Account and enter a new token.")); return; }
        if (status == 429) { fail(tr("Too many Forge requests\nWait a minute, then connect again.")); return; }
        if (status == 422) { fail(tr("Public key couldn't be added\nForge didn't accept this key. Check the .pub file and whether the key is already registered for this user.")); return; }
        if (status == 404) { fail(tr("Server wasn't found in Forge\nRefresh your connections and check that you still have access to this server.")); return; }
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            fail(status == 0 ? tr("Can't reach Forge\nCheck your internet connection and try again.")
                             : tr("Forge couldn't finish the request\nTry again in a moment. If it keeps happening, check the server in Forge.")); return;
        }
        const auto bytes = reply->readAll();
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        if (method != "POST" && (error.error != QJsonParseError::NoError || !document.object().value("data").isArray())) {
            fail(tr("Forge returned an unexpected SSH key response.")); return;
        }
        callback(status, document.object());
    });
}

void SshConnection::listKeys() {
    if (!m_busy) return;
    m_pages.clear();
    auto url = m_keysUrl;
    QUrlQuery query;
    query.addQueryItem("filter[name]", m_keyName);
    query.addQueryItem("filter[user]", m_user);
    url.setQuery(query);
    listPage(url);
}
void SshConnection::listPage(const QUrl &url) {
    if (m_pages.contains(url.toString()) || m_pages.size() >= 100) { fail(tr("Forge returned a repeated SSH key page.")); return; }
    m_pages.insert(url.toString());
    request("GET", url, {}, [this, url](int, const QJsonObject &document) {
        for (const auto &value : document.value("data").toArray()) {
            const auto attributes = value.toObject().value("attributes").toObject();
            if (attributes.value("name") != m_keyName || attributes.value("user") != m_user) continue;
            const auto status = attributes.value("status").toString();
            if (status == "installed") { openTerminal(); return; }
            if (status == "failed") { fail(tr("Forge failed to install this SSH key. Check the server in Forge.")); return; }
            setStatus(tr("Waiting for Forge to install your public key…"));
            m_poll.start(); return;
        }
        const auto link = document.value("links").toObject().value("next");
        const auto next = link.isObject() ? link.toObject().value("href").toString() : link.toString();
        if (!next.isEmpty()) { listPage(url.resolved(QUrl(next))); return; }
        const auto cursor = document.value("meta").toObject().value("next_cursor").toString();
        if (!cursor.isEmpty()) {
            auto nextUrl = url;
            QUrlQuery query(nextUrl); query.removeAllQueryItems("page[cursor]"); query.addQueryItem("page[cursor]", cursor); nextUrl.setQuery(query);
            listPage(nextUrl); return;
        }
        if (!m_posted) createKey();
        else { setStatus(tr("Waiting for Forge to install your public key…")); m_poll.start(); }
    });
}
void SshConnection::createKey() {
    // The deterministic public-key name and user check make retries idempotent
    // for keys installed by Anvil. Never repeat a POST after an uncertain reply.
    m_posted = true;
    setStatus(tr("Adding your public key to %1 as %2…").arg(m_entry.value("server").toString(), m_user));
    request("POST", m_keysUrl, {{"name", m_keyName}, {"key", m_publicKey}, {"user", m_user}}, [this](int, const QJsonObject &) {
        setStatus(tr("Waiting for Forge to install your public key…"));
        m_poll.start();
    });
}
void SshConnection::openTerminal() {
    if (!m_busy) return;
    const bool launch = m_launchTerminal;
    if (launch && !QProcess::startDetached(m_terminal, QStringList{"launch", "terminal", m_ssh} + m_arguments)) {
        fail(tr("The terminal could not be started.")); return;
    }
    const auto name = m_entry.value("name").toString();
    stop();
    setStatus(launch ? tr("Opened a terminal for %1.").arg(name) : tr("SSH key preparation completed for %1.").arg(name));
    emit finished(true);
}
void SshConnection::fail(const QString &error) { stop(); m_status.clear(); emit changed(); emit failed(error); emit finished(false); }
void SshConnection::stop() {
    m_busy = false;
    m_probeTimeout.stop(); m_poll.stop(); m_deadline.stop();
    m_token.clear(); m_publicKey.clear();
    if (m_reply) { auto *reply = m_reply; m_reply = nullptr; reply->abort(); }
    if (m_probe.state() != QProcess::NotRunning) m_probe.kill();
}
void SshConnection::cancel() { if (m_busy) { stop(); setStatus(tr("Connection preparation canceled.")); emit finished(false); } }
