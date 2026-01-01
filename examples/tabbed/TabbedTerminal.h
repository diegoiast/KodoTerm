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
      void addNewTab(const QString &program = QString(), const QString &workingDirectory = QString());
      void closeCurrentTab();
      void closeTab(QWidget *w);
      void nextTab();
      void previousTab();
      void moveTabLeft();
      void moveTabRight();
          void updateTabColors();
          void showConfigDialog();
          void applySettings();
          void saveSession();
      
      protected:
          void closeEvent(QCloseEvent *event) override;
      
      private:
          QTabWidget *m_tabs;
          QTimer *m_autoSaveTimer;
      };
