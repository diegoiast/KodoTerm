// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include <QApplication>
#include <QMainWindow>
#include <KodoTerm/KodoTerm.hpp>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QMainWindow mainWindow;
    
    auto *console = new KodoTerm(&mainWindow);
    mainWindow.setCentralWidget(console);
    mainWindow.resize(800, 600);
    mainWindow.setWindowTitle("KodoTerm example");
    mainWindow.show();
    
    return app.exec();
}