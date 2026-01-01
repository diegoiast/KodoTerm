// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "TabbedTerminal.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    Q_INIT_RESOURCE(KodoTermThemes);
    QApplication app(argc, argv);
    TabbedTerminal mainWindow;
    mainWindow.setWindowTitle("KodoTerm Tabbed Demo");
    mainWindow.show();
    return app.exec();
}