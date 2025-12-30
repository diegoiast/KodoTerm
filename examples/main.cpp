// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include <QApplication>
#include <QMainWindow>
#include <KodoTerm/KodoTerm.hpp>

int main(int argc, char *argv[]) {
    Q_INIT_RESOURCE(KodoTermThemes);
    QApplication app(argc, argv);
    QMainWindow mainWindow;

    auto *console = new KodoTerm(&mainWindow);
    console->setTheme(TerminalTheme::loadKonsoleTheme(":/KodoTermThemes/konsole/Breeze.colorscheme"));
    mainWindow.setCentralWidget(console);
    mainWindow.resize(800, 600);
    mainWindow.setWindowTitle("KodoTerm example");
    mainWindow.show();
    return app.exec();
}