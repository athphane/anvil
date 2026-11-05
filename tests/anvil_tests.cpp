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
