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
#ifdef Q_OS_WIN
    console->setProgram("powershell.exe");
#else
    console->setProgram("/bin/bash");
#endif
    console->setTheme(TerminalTheme::loadKonsoleTheme(":/KodoTermThemes/konsole/Breeze.colorscheme"));

    /*
     * Example: How to kill current program and start a new one with custom config:
     *
     * console->kill();
     * console->setProgram("/usr/bin/python3");
     * console->setArguments({"--version"});
     * console->setWorkingDirectory("/tmp");
     *
     * QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
     * env.insert("MY_VAR", "my_value");
     * console->setProcessEnvironment(env);
     *
     * console->start();
     */

    mainWindow.setCentralWidget(console);
    mainWindow.resize(800, 600);
    mainWindow.setWindowTitle("KodoTerm example");
    mainWindow.show();
    return app.exec();
}