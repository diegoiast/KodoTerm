// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include <QApplication>
#include <QMainWindow>
#include "KodoTerm.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QMainWindow mainWindow;
    
    auto *console = new KodoTerm(&mainWindow);
    mainWindow.setCentralWidget(console);
    mainWindow.resize(800, 600);
    mainWindow.setWindowTitle("Qt6 Console Widget with LibVTerm");
    mainWindow.show();
    
    return app.exec();
}