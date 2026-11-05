#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QScopeGuard>
#include <QCryptographicHash>
#include <QRegularExpression>
#include "anvil_tests.h"
#include "backend.h"
#include "theme.h"
#include "translator.h"

class FakeForge : public QTcpServer {
public:
    QList<QByteArray> requests;
    int status = 200;
    std::function<QByteArray(const QByteArray &)> respond;
    FakeForge() {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto socket = nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                    *buffer += socket->readAll();
                    if (!buffer->contains("\r\n\r\n")) return;
                    const int headerEnd = buffer->indexOf("\r\n\r\n");
                    const QRegularExpression length("Content-Length: (\\d+)", QRegularExpression::CaseInsensitiveOption);
                    const int bodySize = length.match(QString::fromLatin1(buffer->left(headerEnd))).captured(1).toInt();
                    if (buffer->size() < headerEnd + 4 + bodySize || socket->property("responded").toBool()) return;
                    socket->setProperty("responded", true);
                    const auto request = *buffer;
                    requests.append(request);
                    const auto body = respond(request.split(' ').value(1));
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Response\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl base() const { return QUrl(QString("http://127.0.0.1:%1/api/").arg(serverPort())); }
};

class SshRig {
public:
    QTemporaryDir directory;
    QString ssh = directory.filePath("ssh"), terminal = directory.filePath("omarchy"), publicPath = directory.filePath("id_ed25519.pub"), output = directory.filePath("terminal-args"), probeOutput = directory.filePath("probe-args");
    QByteArray key;
    QString name;
    QVariantMap entry{{"id","81"},{"serverId","44"},{"org","studio"},{"server","prod"},{"name","example.com"},{"host","203.0.113.7"},{"user","isolated"},{"port",2222},{"kind","site"},{"path","/home/isolated/example.com/current"}};
    SshRig() {
        QByteArray blob = QByteArray::fromHex("0000000b") + "ssh-ed25519" + QByteArray::fromHex("00000020") + QByteArray(32, 'x');
        key = "ssh-ed25519 " + blob.toBase64();
        name = "Anvil-" + QString::fromLatin1(QCryptographicHash::hash(blob, QCryptographicHash::Sha256).toHex().left(24)) + "-isolated";
        write(publicPath, key + " test@example.invalid\n");
        write(directory.filePath("id_ed25519"), "PRIVATE KEY MUST NEVER BE UPLOADED\n");
        write(terminal, "#!/bin/sh\nprintf '%s\\n' \"$@\" > " + Backend::shellQuote(output).toUtf8() + "\n", true);
        probe("Permission denied (publickey).", 255);
    }
    void probe(const QByteArray &error, int code) {
        write(ssh, "#!/bin/sh\nprintf '%s\\n' \"$@\" > " + Backend::shellQuote(probeOutput).toUtf8() + "\nprintf '%s\\n' " + Backend::shellQuote(QString::fromUtf8(error)).toUtf8() + " >&2\nexit " + QByteArray::number(code) + "\n", true);
    }
    static void write(const QString &path, const QByteArray &bytes, bool executable = false) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("Could not write test fixture");
        file.close();
        if (executable) QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    QByteArray keys(const QString &status) const {
        return QJsonDocument(QJsonObject{{"data", QJsonArray{QJsonObject{{"id","9"},{"attributes",QJsonObject{{"name",name},{"user","isolated"},{"status",status}}}}}}}).toJson();
    }
    QStringList arguments() const { return Backend::sshArguments(entry); }
    bool start(SshConnection &connection, bool sameHost = true) const { return connection.start("test-token", entry, arguments(), {}, publicPath, true, sameHost); }
};

void AnvilTests::demoSearchAndFilters() {
        Backend backend(true);
        QCOMPARE(backend.total(), 6);
        backend.setQuery("DOCS"); QCOMPARE(backend.entries().size(), 1);
        QCOMPARE(backend.entries().first().toMap().value("user").toString(), "docs");
        backend.setQuery("northstar"); QCOMPARE(backend.entries().size(), 5);
        backend.setFilter(1); QCOMPARE(backend.entries().size(), 2);
        backend.setFilter(2); QCOMPARE(backend.entries().size(), 3);
        QVERIFY(!backend.connectEntry(backend.entries().first().toMap()));
    }
void AnvilTests::apiDiscoversAndPaginates() {
        FakeForge server;
        server.respond = [](const QByteArray &path) -> QByteArray {
            if (path == "/api/orgs") return R"({"data":[{"id":"1","attributes":{"slug":"studio","name":"Studio"}}],"links":{"next":null}})";
            if (path == "/api/orgs/studio/servers") return R"({"data":[{"id":"44","attributes":{"name":"prod","ip_address":"203.0.113.7","ssh_port":2222,"is_ready":true,"revoked":false}}],"links":{"next":"/api/orgs/studio/servers?page%5Bcursor%5D=next"}})";
            if (path.contains("cursor")) return R"({"data":[{"id":"45","attributes":{"name":"stage","ip_address":"198.51.100.3","ssh_port":22,"is_ready":true}}],"links":{"next":null}})";
            return R"({"data":[{"id":"81","attributes":{"name":"example.com","user":"isolated","root_directory":"/home/isolated/example.com/current"},"relationships":{"server":{"data":{"type":"servers","id":"44"}}}}]})";
        };
        Backend backend(false, server.base());
        QSignalSpy finished(&backend, &Backend::refreshFinished);
        backend.authenticate("test-token", false);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(backend.error(), QString());
        QCOMPARE(backend.total(), 3);
        QCOMPARE(server.requests.size(), 4);
        QVERIFY(server.requests.first().contains("Authorization: Bearer test-token"));
        backend.setFilter(2);
        const auto entry = backend.entries().first().toMap();
        QCOMPARE(entry.value("host").toString(), "203.0.113.7");
        QCOMPARE(entry.value("port").toInt(), 2222);
        QCOMPARE(entry.value("user").toString(), "isolated");
        QCOMPARE(entry.value("path").toString(), "/home/isolated/example.com/current");
        QCOMPARE(entry.value("serverId").toString(), "44");
        QVERIFY(backend.command(entry).contains("current"));
    }
void AnvilTests::apiRejectsForeignPagination() {
        FakeForge server;
        server.respond = [](const QByteArray &) { return QByteArray(R"({"data":[],"links":{"next":"https://attacker.invalid/api/orgs"}})"); };
        Backend backend(false, server.base());
        QSignalSpy finished(&backend, &Backend::refreshFinished);
        backend.authenticate("test-token", false);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(backend.error().contains("unsafe"));
        QCOMPARE(server.requests.size(), 1);
    }
void AnvilTests::apiIncludesServersOnEverySitePage() {
        FakeForge server;
        server.respond = [](const QByteArray &path) -> QByteArray {
            if (path == "/api/orgs") return R"({"data":[{"id":"1","attributes":{"slug":"studio"}}]})";
            if (path == "/api/orgs/studio/servers") return R"({"data":[{"id":"44","attributes":{"name":"prod","ip_address":"203.0.113.7","ssh_port":22,"is_ready":true}}]})";
            // Forge may omit linkage unless the relationship is requested.
            if (!path.contains("include=server")) return R"({"data":[{"id":"81","attributes":{"name":"example.com"}}]})";
            if (!path.contains("cursor")) return R"({"data":[{"id":"81","attributes":{"name":"example.com","user":"forge","root_directory":"/home/forge/example.com"},"relationships":{"server":{"data":{"id":"44"}}}}],"links":{"next":"/api/orgs/studio/sites?page%5Bcursor%5D=next"}})";
            return R"({"data":[{"id":"82","attributes":{"name":"docs.example.com","user":"docs","root_directory":"/home/docs/docs.example.com"},"relationships":{"server":{"data":{"id":"44"}}}}]})";
        };
        Backend backend(false, server.base());
        QSignalSpy finished(&backend, &Backend::refreshFinished);
        backend.authenticate("test-token", false);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY2(backend.error().isEmpty(), qPrintable(backend.error()));
        backend.setFilter(2);
        QCOMPARE(backend.entries().size(), 2);
        QCOMPARE(server.requests.size(), 4);
        for (const auto &request : server.requests) {
            if (request.startsWith("GET /api/orgs/studio/sites")) QVERIFY(request.split(' ').at(1).contains("include=server"));
        }
    }
void AnvilTests::apiReportsUnmatchedSites() {
        FakeForge server;
        server.respond = [](const QByteArray &path) -> QByteArray {
            if (path == "/api/orgs") return R"({"data":[{"id":"1","attributes":{"slug":"studio"}}]})";
            if (path.endsWith("/servers")) return R"({"data":[{"id":"44","attributes":{"name":"prod","ip_address":"203.0.113.7","ssh_port":22}}]})";
            return R"({"data":[{"id":"81","attributes":{"name":"example.com"}}]})";
        };
        Backend backend(false, server.base());
        QSignalSpy finished(&backend, &Backend::refreshFinished);
        backend.authenticate("test-token", false);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(backend.error().contains("without a matching server"));
        QCOMPARE(backend.total(), 1);
    }
void AnvilTests::apiReportsRequiredScopes() {
        FakeForge server;
        server.status = 403;
        server.respond = [](const QByteArray &) { return QByteArray("{}"); };
        Backend backend(false, server.base());
        QSignalSpy finished(&backend, &Backend::refreshFinished);
        backend.authenticate("test-token", false);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(backend.error().contains("organization:view"));
        server.status = 200;
        server.respond = [&server](const QByteArray &path) -> QByteArray {
            server.status = path == "/api/orgs" ? 200 : 403;
            if (path == "/api/orgs") return R"({"data":[{"id":"1","attributes":{"slug":"studio"}}]})";
            return "{}";
        };
        backend.refresh();
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY(backend.error().contains("server:view"));
    }
void AnvilTests::apiRejectsMalformedResponses() {
        FakeForge server;
        server.respond = [](const QByteArray &) { return QByteArray("not json"); };
        Backend backend(false, server.base());
        QSignalSpy finished(&backend, &Backend::refreshFinished);
        backend.authenticate("test-token", false);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(backend.error().contains("unexpected"));
    }
void AnvilTests::sshKeepsRemoteDataQuoted() {
        QVariantMap entry{{"host","203.0.113.8"},{"user","forge"},{"port",22},{"kind","site"},{"path","/home/forge/a'; touch /tmp/never; '"}};
        QString error;
        const auto args = Backend::sshArguments(entry, {}, {}, 0, {}, &error);
        QVERIFY(!args.isEmpty());
        QCOMPARE(args.at(5), QString("--"));
        QCOMPARE(args.at(6), QString("203.0.113.8"));
        QVERIFY(args.last().contains("'\"'\"'"));
        QVERIFY(Backend::sshArguments(entry, "-oProxyCommand=evil", {}, 0, {}, &error).isEmpty());
        QVERIFY(Backend::sshArguments(entry, {}, "x;evil", 0, {}, &error).isEmpty());
        QVERIFY(Backend::sshArguments(entry, {}, {}, 70000, {}, &error).isEmpty());
        QVERIFY(Backend::sshArguments(entry, {}, {}, 0, "/missing/anvil/key", &error).isEmpty());
        const auto custom = Backend::sshArguments(entry, "my-prod-alias", "deploy", 2022);
        QVERIFY(custom.contains("my-prod-alias")); QVERIFY(custom.contains("deploy")); QVERIFY(custom.contains("2022"));
    }
void AnvilTests::apiReportsRateLimits() {
        FakeForge server;
        server.status = 429;
        server.respond = [](const QByteArray &) { return QByteArray("{}"); };
        Backend backend(false, server.base());
        QSignalSpy finished(&backend, &Backend::refreshFinished);
        backend.authenticate("test-token", false);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(backend.error().contains("rate limit"));
        QVERIFY(!backend.busy());
        QCOMPARE(server.requests.size(), 1);
    }
void AnvilTests::connectionLaunchesOmarchyTerminal() {
        QTemporaryDir dir;
        const auto output = dir.filePath("arguments");
        const auto executable = dir.filePath("omarchy");
        write(executable, "#!/bin/sh\nprintf '%s\\n' \"$@\" > " + Backend::shellQuote(output).toUtf8() + "\n");
        QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(QFile::link("/usr/bin/true", dir.filePath("ssh")));
        const auto previous = qgetenv("PATH");
        auto restore = qScopeGuard([previous] { qputenv("PATH", previous); });
        qputenv("PATH", dir.path().toUtf8());
        Backend backend;
        const QVariantMap entry{{"host","203.0.113.8"},{"user","forge"},{"port",2222},{"kind","server"},{"name","test"}};
        QVERIFY(backend.connectEntry(entry));
        QTRY_VERIFY(QFileInfo(output).size() > 0);
        QFile file(output); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto data = file.readAll();
        QVERIFY(data.startsWith("launch\nterminal\n"));
        QVERIFY(data.contains("-p\n2222\n"));
        QVERIFY(data.contains("-l\nforge\n--\n203.0.113.8\n"));
    }
void AnvilTests::themeReloadsAfterAtomicReplacement() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        const auto path = dir.filePath("theme/colors.toml");
        write(path, "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QFile::remove(path); write(path, "accent = \"#ffffff\"\n");
        QTRY_COMPARE(theme.accent(), QString("#ffffff"));
        QCOMPARE(theme.accentForeground(), QString("black"));
        QVERIFY(QFile::remove(path));
        QTRY_COMPARE(theme.accent(), QString("#FFD60A"));
    }
void AnvilTests::sshWorkingAccessSkipsForge() {
        FakeForge server;
        SshRig rig;
        server.respond = [](const QByteArray &) { return QByteArray("{}"); };
        for (const auto &accepted : {false, true}) {
            QFile::remove(rig.output);
            rig.probe(accepted ? "debug1: Server accepts key: /test/key\nPermission denied (publickey)." : "", accepted ? 255 : 0);
            SshConnection connection(server.base(), rig.directory.path(), rig.ssh, rig.terminal, 5, 300);
            QSignalSpy finished(&connection, &SshConnection::finished);
            QVERIFY(rig.start(connection));
            QTRY_COMPARE(finished.count(), 1);
            QVERIFY(finished.first().first().toBool());
            QTRY_VERIFY(QFileInfo(rig.output).size() > 0);
        }
        QCOMPARE(server.requests.size(), 0);
    }
void AnvilTests::sshInstallsPublicKeyForSiteUser() {
        FakeForge server;
        SshRig rig;
        int posts = 0, polls = 0;
        server.respond = [&](const QByteArray &) -> QByteArray {
            if (server.requests.last().startsWith("POST ")) { ++posts; server.status = 202; return {}; }
            server.status = 200;
            if (!posts) return R"({"data":[]})";
            ++polls;
            return rig.keys(polls == 1 ? "installing" : "installed");
        };
        SshConnection connection(server.base(), rig.directory.path(), rig.ssh, rig.terminal, 5, 1000);
        QSignalSpy finished(&connection, &SshConnection::finished);
        QSignalSpy failed(&connection, &SshConnection::failed);
        QVERIFY(rig.start(connection));
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(failed.count(), 0);
        QVERIFY(finished.first().first().toBool());
        QCOMPARE(posts, 1); QCOMPARE(polls, 2);
        QTRY_VERIFY(QFileInfo(rig.output).size() > 0);
        for (const auto &request : server.requests) {
            QVERIFY(request.split(' ').value(1).startsWith("/api/orgs/studio/servers/44/ssh-keys"));
            QVERIFY(!request.contains("PRIVATE KEY"));
            if (!request.startsWith("POST ")) continue;
            const auto body = QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
            QCOMPARE(body.value("key").toString().toUtf8(), rig.key);
            QCOMPARE(body.value("user").toString(), "isolated");
        }
        QFile probe(rig.probeOutput); QVERIFY(probe.open(QIODevice::ReadOnly));
        const auto probeArgs = probe.readAll();
        QVERIFY(probeArgs.contains("StrictHostKeyChecking=yes"));
        QVERIFY(probeArgs.endsWith("true\n"));
        QVERIFY(!probeArgs.contains("cd --"));
        QFile terminal(rig.output); QVERIFY(terminal.open(QIODevice::ReadOnly));
        const auto terminalArgs = terminal.readAll();
        QVERIFY(terminalArgs.contains("-i\n"));
        QVERIFY(terminalArgs.contains("cd -- '/home/isolated/example.com/current'"));
    }
void AnvilTests::sshExistingKeyIsNotUploadedAgain() {
        FakeForge server;
        SshRig rig;
        rig.probe("No ED25519 host key is known for 203.0.113.7 and you have requested strict checking.\nHost key verification failed.", 255);
        server.respond = [&](const QByteArray &path) -> QByteArray {
            if (!path.contains("cursor")) return R"({"data":[],"links":{"next":"/api/orgs/studio/servers/44/ssh-keys?page%5Bcursor%5D=next"}})";
            return rig.keys("installed");
        };
        SshConnection connection(server.base(), rig.directory.path(), rig.ssh, rig.terminal, 5, 300);
        QSignalSpy finished(&connection, &SshConnection::finished);
        QVERIFY(rig.start(connection));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.first().first().toBool());
        QCOMPARE(server.requests.size(), 2);
        for (const auto &request : server.requests) QVERIFY(request.startsWith("GET "));
    }
void AnvilTests::sshStopsOnHostOrNetworkFailures() {
        for (const auto &error : {QByteArray("Connection timed out"), QByteArray("REMOTE HOST IDENTIFICATION HAS CHANGED!")}) {
            FakeForge server;
            SshRig rig;
            rig.probe(error, 255);
            SshConnection connection(server.base(), rig.directory.path(), rig.ssh, rig.terminal, 5, 300);
            QSignalSpy finished(&connection, &SshConnection::finished);
            QVERIFY(rig.start(connection));
            QTRY_COMPARE(finished.count(), 1);
            QVERIFY(!finished.first().first().toBool());
            QCOMPARE(server.requests.size(), 0);
            QVERIFY(!QFileInfo::exists(rig.output));
        }
        FakeForge server;
        SshRig rig;
        SshConnection connection(server.base(), rig.directory.path(), rig.ssh, rig.terminal, 5, 300);
        QSignalSpy finished(&connection, &SshConnection::finished);
        QVERIFY(rig.start(connection, false));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished.first().first().toBool());
        QCOMPARE(server.requests.size(), 0);
    }
void AnvilTests::sshErrorsExplainTheNextStep() {
        const QString noise = "debug1: no pubkey loaded from /home/example/.ssh/id_optional\ndebug1: identity file type -1\n";
        const QList<QPair<QString, QString>> errors{
            {"ssh: connect to host 203.0.113.7 port 2222: Connection timed out", "Connection timed out\n"},
            {"Connection refused", "Connection refused\n"},
            {"Could not resolve hostname example.invalid: Name or service not known", "Server not found\n"},
            {"No route to host", "Server is unreachable\n"},
            {"REMOTE HOST IDENTIFICATION HAS CHANGED!", "Server identity changed\n"},
            {"Host key verification failed.", "Server identity needs verification\n"},
            {"Permission denied (password).", "SSH access denied\n"},
            {"Unable to negotiate: no matching host key type found", "SSH settings aren't compatible\n"},
            {"Connection reset by peer", "Server closed the connection\n"},
            {"unexpected OpenSSH failure", "SSH connection failed\n"}
        };
        for (const auto &example : errors) {
            const auto message = SshConnection::describeSshFailure(noise + example.first, "203.0.113.7", 2222);
            QVERIFY2(message.startsWith(example.second), qPrintable(message));
            QVERIFY(!message.contains("debug1:"));
            QVERIFY(!message.contains("/home/example"));
            QVERIFY(!message.contains("type -1"));
            QVERIFY(message.size() < 260);
        }
        const auto timeout = SshConnection::describeSshFailure({}, "203.0.113.7", 2222, true);
        QVERIFY(timeout.contains("203.0.113.7"));
        QVERIFY(timeout.contains("2222"));
        QVERIFY(timeout.contains("firewall"));
    }
void AnvilTests::sshReportsCreatePermission() {
        FakeForge server;
        SshRig rig;
        server.respond = [&](const QByteArray &) {
            server.status = server.requests.last().startsWith("POST ") ? 403 : 200;
            return QByteArray(R"({"data":[]})");
        };
        SshConnection connection(server.base(), rig.directory.path(), rig.ssh, rig.terminal, 5, 300);
        QSignalSpy finished(&connection, &SshConnection::finished), failed(&connection, &SshConnection::failed);
        QVERIFY(rig.start(connection));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished.first().first().toBool());
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toString().contains("server:create-keys"));
        QVERIFY(!QFileInfo::exists(rig.output));
    }
void AnvilTests::sshWaitsWithoutRepeatedUploads() {
        FakeForge server;
        SshRig rig;
        int posts = 0;
        server.respond = [&](const QByteArray &) -> QByteArray {
            if (server.requests.last().startsWith("POST ")) { server.status = 202; ++posts; return {}; }
            server.status = 200; return R"({"data":[]})";
        };
        SshConnection connection(server.base(), rig.directory.path(), rig.ssh, rig.terminal, 10, 130);
        QSignalSpy finished(&connection, &SshConnection::finished), failed(&connection, &SshConnection::failed);
        QVERIFY(rig.start(connection));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished.first().first().toBool());
        QCOMPARE(posts, 1);
        QVERIFY(failed.first().first().toString().contains("not finished"));
        QVERIFY(!QFileInfo::exists(rig.output));
    }
void AnvilTests::sshRejectsPrivateFilesAndForeignLinks() {
        FakeForge server;
        SshRig rig;
        SshConnection connection(server.base(), rig.directory.path(), rig.ssh, rig.terminal, 5, 300);
        QSignalSpy finished(&connection, &SshConnection::finished);
        QVERIFY(connection.start("test-token", rig.entry, rig.arguments(), {}, rig.directory.filePath("id_ed25519"), true, true));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished.first().first().toBool());
        QCOMPARE(server.requests.size(), 0);
        rig.write(rig.publicPath, "-----BEGIN OPENSSH PRIVATE KEY-----\nsecret\n");
        QVERIFY(rig.start(connection));
        QTRY_COMPARE(finished.count(), 2);
        QCOMPARE(server.requests.size(), 0);
        rig.write(rig.publicPath, rig.key);
        server.respond = [](const QByteArray &) { return QByteArray(R"({"data":[],"links":{"next":"https://attacker.invalid/api/keys"}})"); };
        QVERIFY(rig.start(connection));
        QTRY_COMPARE(finished.count(), 3);
        QVERIFY(!finished.last().first().toBool());
        QCOMPARE(server.requests.size(), 1);
    }
void AnvilTests::sshCancellationStopsPolling() {
        FakeForge server;
        SshRig rig;
        server.respond = [&](const QByteArray &) { return rig.keys("installing"); };
        SshConnection connection(server.base(), rig.directory.path(), rig.ssh, rig.terminal, 200, 1000);
        QSignalSpy finished(&connection, &SshConnection::finished);
        QVERIFY(rig.start(connection));
        QTRY_COMPARE(server.requests.size(), 1);
        connection.cancel();
        QTRY_COMPARE(finished.count(), 1);
        QTest::qWait(250);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(!QFileInfo::exists(rig.output));
        QVERIFY(!connection.busy());
    }
void AnvilTests::sshPreparationCanRunWithoutTerminal() {
        FakeForge server;
        SshRig rig;
        server.respond = [&](const QByteArray &) { return rig.keys("installed"); };
        SshConnection connection(server.base(), rig.directory.path(), rig.ssh, rig.terminal, 5, 300);
        QSignalSpy finished(&connection, &SshConnection::finished);
        QVERIFY(connection.start("test-token", rig.entry, rig.arguments(), {}, rig.publicPath, true, true, false));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.first().first().toBool());
        QVERIFY(!QFileInfo::exists(rig.output));
        QCOMPARE(server.requests.size(), 1);
    }
void AnvilTests::translatorFollowsLiveLanguageAndFallbacks() {
        QTemporaryDir dir;
        const auto language = dir.filePath("language");
        write(language, "pt-BR\n");
        write(dir.filePath("pt.json"), R"({"Save":"Guardar","Open":"Abrir"})");
        write(dir.filePath("pt-BR.json"), R"({"Save":"Salvar"})");
        Translator translator(language, dir.path());
        QCOMPARE(translator.translate("Main","Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main","Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main","Missing").isNull());
        write(language, "da\n");
        QTRY_COMPARE(translator.language(), QString("da"));
        QCOMPARE(Translator::pluralCategory("ar", 2), QString("two"));
        QCOMPARE(Translator::pluralCategory("ru", 22), QString("few"));
    }
void AnvilTests::realQmlInteractionAndScreenshots() {
        QTemporaryDir dir;
        const auto language = dir.filePath("language");
        write(language, "en\n");
        Translator translator(language);
        qApp->installTranslator(&translator);
        Theme theme(dir.path());
        Backend backend(true);
        QQmlApplicationEngine engine;
        connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto search = window->findChild<QQuickItem *>("search");
        QVERIFY(search);
        QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
        QTRY_VERIFY(search->hasActiveFocus());
        for (const auto key : {Qt::Key_D, Qt::Key_O, Qt::Key_C, Qt::Key_S}) QTest::keyClick(window, key);
        QTRY_COMPARE(backend.entries().size(), 1);
        QCOMPARE(backend.entries().first().toMap().value("name").toString(), "docs.example.com");
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_COMPARE(backend.entries().size(), 6);
        QTest::qWait(150);
        QDir().mkpath("../../docs");
        QVERIFY(window->grabWindow().save("../../docs/screenshot.png"));
        backend.fail(SshConnection::describeSshFailure("debug1: missing optional identity\nConnection timed out", "203.0.113.24", 22));
        QTest::qWait(100);
        auto heading = window->findChild<QObject *>("errorHeading"), body = window->findChild<QObject *>("errorBody");
        auto dismiss = window->findChild<QQuickItem *>("errorDismiss");
        QVERIFY(heading && body && dismiss);
        QCOMPARE(heading->property("text").toString(), QString("Connection timed out"));
        QVERIFY(body->property("text").toString().contains("firewall"));
        QVERIFY(!body->property("text").toString().contains("debug1:"));
        QVERIFY(window->grabWindow().save("../../docs/error.png"));
        const auto dismissPosition = dismiss->mapToScene(QPointF(dismiss->width() / 2, dismiss->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, dismissPosition.toPoint());
        QTRY_VERIFY(backend.error().isEmpty());
        write(language, "da\n");
        QTRY_COMPARE(translator.language(), QString("da"));
        QTest::qWait(150);
        QVERIFY(window->grabWindow().save("../../docs/screenshot-da.png"));
        QTest::keyClick(window, Qt::Key_Comma, Qt::ControlModifier);
        auto account = window->findChild<QObject *>("account");
        QVERIFY(account);
        QTRY_VERIFY(account->property("visible").toBool());
        QTest::qWait(200);
        QVERIFY(window->grabWindow().save("../../docs/account-da.png"));
        QTest::keyClick(window, Qt::Key_Escape);
        window->setMinimumSize(QSize(600, 420));
        window->resize(749, 450);
        QTest::qWait(200);
        auto compactConnect = window->findChild<QQuickItem *>("compactConnect");
        QVERIFY(compactConnect && compactConnect->isVisible());
        const auto position = compactConnect->mapToScene(QPointF(0, 0));
        QVERIFY(position.y() + compactConnect->height() <= window->height());
        QVERIFY(window->grabWindow().save("../../docs/screenshot-compact-da.png"));
        backend.fail(SshConnection::describeSshFailure("Connection timed out", "203.0.113.24", 22));
        QTest::qWait(100);
        QCOMPARE(heading->property("text").toString(), QString("Forbindelsen fik intet svar"));
        QVERIFY(window->grabWindow().save("../../docs/error-compact-da.png"));
        backend.clearError();
        auto options = window->findChild<QObject *>("options");
        QVERIFY(options);
        QVERIFY(QMetaObject::invokeMethod(options, "open"));
        QTRY_VERIFY(options->property("visible").toBool());
        auto installMissing = window->findChild<QObject *>("installMissing");
        auto publicKey = window->findChild<QObject *>("publicKey");
        QVERIFY(installMissing && publicKey);
        QVERIFY(installMissing->property("checked").toBool());
        QCOMPARE(publicKey->property("text").toString(), QString("~/.ssh/id_ed25519.pub"));
        QTest::qWait(200);
        QVERIFY(options->property("height").toDouble() <= window->height());
        QVERIFY(window->grabWindow().save("../../docs/options-compact-da.png"));
        window->resize(1120, 740);
        QTest::qWait(200);
        QVERIFY(window->grabWindow().save("../../docs/options-da.png"));
        QMetaObject::invokeMethod(options, "close");
        qApp->removeTranslator(&translator);
    }
void AnvilTests::write(const QString &path, const QByteArray &contents) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(contents), contents.size());
    }

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Material");
    AnvilTests tests;
    return QTest::qExec(&tests, argc, argv);
}
