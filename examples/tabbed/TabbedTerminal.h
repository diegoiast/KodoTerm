// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#pragma once

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTabWidget>

class TabbedTerminal : public QMainWindow {
    Q_OBJECT
  public:
    TabbedTerminal(QWidget *parent = nullptr);
    ~TabbedTerminal();

  public slots:
    void addNewTab(const QString &program = QString(), const QString &workingDirectory = QString(),
                   const QString &logPath = QString());
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
    void toggleExpanded();
    void toggleWindowVisibility();
    void showAboutDialog();

  protected:
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

  private:
    void setupTrayIcon();

    QTabWidget *m_tabs;
    QTimer *m_autoSaveTimer;
    bool m_useFullScreenMode = false;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QAction *m_toggleWindowAction = nullptr;
};