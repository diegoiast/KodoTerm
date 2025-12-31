// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#pragma once

#include <QMainWindow>
#include <QTabWidget>

class TabbedTerminal : public QMainWindow {
    Q_OBJECT
public:
    TabbedTerminal(QWidget *parent = nullptr);

public slots:
    void addNewTab();
    void closeCurrentTab();
    void closeTab(QWidget *w);

private:
    QTabWidget *m_tabs;
};
