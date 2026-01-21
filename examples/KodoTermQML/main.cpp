// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include <KodoTerm/KodoQuickTerm.hpp>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    // Initialize resources
    Q_INIT_RESOURCE(KodoTermThemes);

    qmlRegisterType<KodoQuickTerm>("KodoTerm", 1, 0, "KodoQuickTerm");

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/KodoTermQML/main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}