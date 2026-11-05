#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSslSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSaveFile>
#include <QDir>
#include <QTextStream>
#include <QTimer>
#include "backend.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : "open";
    const bool cli = mode != "open" && mode != "--demo";
    if (cli) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QT_QPA_PLATFORMTHEME", "generic");
    }
    QGuiApplication app(argc, argv);
    app.setApplicationName("anvil");
    app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("anvil");
    app.setWindowIcon(QIcon::fromTheme("anvil"));
    const auto args = app.arguments();
    if (cli) {
        auto output = [](const QJsonObject &value, bool error = false) {
            QTextStream stream(error ? stderr : stdout);
            stream << QJsonDocument(value).toJson(QJsonDocument::Compact) << '\n';
        };
        if (mode == "--help" || mode == "help") {
            QTextStream(stdout) << "Anvil — Laravel Forge SSH directory\n\nanvil open [--demo]\nanvil list --json\nanvil command --target <key> --json\nanvil prepare --target <key> [--public-key <file.pub>] [--identity <file>] --json\nanvil check --json\nanvil skill install [--json]\n\nCLI Forge commands read FORGE_API_TOKEN from the environment.\nPrepare checks SSH access and can add a public key through Forge without opening a terminal.\n";
            return 0;
        }
        if (mode == "skill" && args.contains("install")) {
            const auto directory = QDir::homePath() + "/.agents/skills/anvil";
            const auto path = directory + "/SKILL.md";
            if (QFileInfo::exists(path)) { output({{"error", "Skill already exists; it was left unchanged."}}, true); return 1; }
            QFile source(":/agent-skill.md");
            QSaveFile target(path);
            if (!QDir().mkpath(directory) || !source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly) || target.write(source.readAll()) < 0 || !target.commit()) { output({{"error", "Could not install the agent skill."}}, true); return 1; }
            output({{"installed", path}}); return 0;
        }
        if (mode == "check") {
            const bool ok = !QStandardPaths::findExecutable("ssh").isEmpty() && !QStandardPaths::findExecutable("omarchy").isEmpty() && QSslSocket::supportsSsl();
            output({{"ok",ok},{"ssh",QStandardPaths::findExecutable("ssh")},{"terminal",QStandardPaths::findExecutable("omarchy")},{"keyring",QStandardPaths::findExecutable("secret-tool")},{"tls",QSslSocket::supportsSsl()}}, !ok);
            return ok ? 0 : 1;
        }
        if (mode != "list" && mode != "command" && mode != "prepare") { output({{"error","Unknown command. Run anvil --help."}}, true); return 1; }
        const auto token = qEnvironmentVariable("FORGE_API_TOKEN");
        if (token.isEmpty()) { output({{"error","Set FORGE_API_TOKEN in the environment."}}, true); return 1; }
        const int position = args.indexOf("--target");
        const auto target = position >= 0 ? args.value(position + 1) : QString();
        if (mode != "list" && target.isEmpty()) { output({{"error","Pass --target with a key from anvil list --json."}}, true); return 1; }
        Backend backend;
        QObject::connect(&backend, &Backend::connectionFinished, &app, [&](bool success) {
            if (success) output({{"prepared",true},{"target",target}});
            else output({{"error",backend.error()}}, true);
            app.exit(success ? 0 : 1);
        });
        QObject::connect(&backend, &Backend::refreshFinished, &app, [&] {
            if (!backend.error().isEmpty()) { output({{"error", backend.error()}}, true); app.exit(1); return; }
            if (mode == "list") { output({{"connections", QJsonArray::fromVariantList(backend.allEntries())}}); app.exit(0); return; }
            for (const auto &value : backend.allEntries()) {
                const auto entry = value.toMap();
                if (entry.value("key") != target) continue;
                if (mode == "prepare") {
                    const int keyPosition = args.indexOf("--public-key"), identityPosition = args.indexOf("--identity");
                    if (!backend.prepareEntry(entry, identityPosition < 0 ? QString() : args.value(identityPosition + 1), keyPosition < 0 ? QString() : args.value(keyPosition + 1))) {
                        output({{"error",backend.error()}}, true); app.exit(1);
                    }
                    return;
                }
                const auto command = backend.command(entry);
                if (command.isEmpty()) { output({{"error",backend.error()}}, true); app.exit(1); return; }
                output({{"command",command},{"target",target}}); app.exit(0); return;
            }
            output({{"error","Connection not found."}}, true); app.exit(1);
        });
        QTimer::singleShot(0, &backend, [&] { backend.authenticate(token, false); });
        return app.exec();
    }
    QQuickStyle::setStyle("Material");
    Translator translator;
    app.installTranslator(&translator);
    Theme theme;
    Backend backend(args.contains("--demo"));
    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    QTimer::singleShot(0, &backend, &Backend::restoreToken);
    return app.exec();
}
