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
    void addNewTab(const QString &program = QString());
    void closeCurrentTab();
    void closeTab(QWidget *w);
    void nextTab();
    void previousTab();
    void moveTabLeft();
    void moveTabRight();
    void updateTabColors();
    void showConfigDialog();
    void applySettings();

  private:
    QTabWidget *m_tabs;
};
