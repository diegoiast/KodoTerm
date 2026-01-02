// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "TabbedTerminal.h"
#include "AppConfig.h"
#include "ConfigDialog.h"
#include <KodoTerm/KodoTerm.hpp>
#include <QAction>
#include <QCloseEvent>
#include <QDebug>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>
#include <QTabBar>
#include <QToolButton>
#include <QMessageBox>
#include <QApplication>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

TabbedTerminal::TabbedTerminal(QWidget *parent) : QMainWindow(parent) {
    m_tabs = new QTabWidget(this);
    m_tabs->setTabPosition(QTabWidget::South);
    m_tabs->setDocumentMode(true);
    m_tabs->setMovable(true);
    setCentralWidget(m_tabs);

    setupTrayIcon();

    // New Tab button (Left corner)
    QToolButton *newTabBtn = new QToolButton(m_tabs);
    newTabBtn->setText(QString(QChar(0x2795))); // ➕
    newTabBtn->setToolTip(tr("New Tab"));
    newTabBtn->setAutoRaise(true);
    newTabBtn->setPopupMode(QToolButton::MenuButtonPopup);
    m_tabs->setCornerWidget(newTabBtn, Qt::TopLeftCorner);
    
    QMenu *shellsMenu = new QMenu(newTabBtn);
    
    auto updateMenu = [this, shellsMenu, newTabBtn]() {
        shellsMenu->clear();
        QList<AppConfig::ShellInfo> shells = AppConfig::loadShells();
        for (const auto &shell : shells) {
            shellsMenu->addAction(shell.name, this, [this, shell]() {
                addNewTab(shell.path);
            });
        }
        shellsMenu->addSeparator();
        shellsMenu->addAction(tr("Configure..."), this, &TabbedTerminal::showConfigDialog);
        shellsMenu->addAction(tr("About..."), this, &TabbedTerminal::showAboutDialog);

        QSettings s;
        if (s.value("Window/EnableTray", false).toBool()) {
            shellsMenu->addSeparator();
            shellsMenu->addAction(tr("Quit"), qApp, &QApplication::quit);
        }
    };
    updateMenu();
    connect(shellsMenu, &QMenu::aboutToShow, this, updateMenu); // Refresh menu on show

    newTabBtn->setMenu(shellsMenu);
    connect(newTabBtn, &QToolButton::clicked, this, [this](){ addNewTab(); });

    // Close Tab button (Right corner)
    QToolButton *closeTabBtn = new QToolButton(m_tabs);
    closeTabBtn->setText(QString(QChar(0x2715))); // ✕
    closeTabBtn->setAutoRaise(true);
    closeTabBtn->setToolTip(tr("Close Current Tab"));
    m_tabs->setCornerWidget(closeTabBtn, Qt::TopRightCorner);
    connect(closeTabBtn, &QToolButton::clicked, this, &TabbedTerminal::closeCurrentTab);

    // Actions
    QAction *newTabAction = new QAction(tr("New Tab"), this);
    newTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    newTabAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(newTabAction, &QAction::triggered, this, [this]() { addNewTab(); });
    addAction(newTabAction);

    QAction *closeTabAction = new QAction(tr("Close Tab"), this);
    closeTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    closeTabAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(closeTabAction, &QAction::triggered, this, &TabbedTerminal::closeCurrentTab);
    addAction(closeTabAction);

    QAction *prevTabAction = new QAction(tr("Previous Tab"), this);
    prevTabAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Left));
    prevTabAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(prevTabAction, &QAction::triggered, this, &TabbedTerminal::previousTab);
    addAction(prevTabAction);

    QAction *nextTabAction = new QAction(tr("Next Tab"), this);
    nextTabAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Right));
    nextTabAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(nextTabAction, &QAction::triggered, this, &TabbedTerminal::nextTab);
    addAction(nextTabAction);

    QAction *moveTabLeftAction = new QAction(tr("Move Tab Left"), this);
    moveTabLeftAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Left));
    moveTabLeftAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(moveTabLeftAction, &QAction::triggered, this, &TabbedTerminal::moveTabLeft);
    addAction(moveTabLeftAction);

    QAction *moveTabRightAction = new QAction(tr("Move Tab Right"), this);
    moveTabRightAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Right));
    moveTabRightAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(moveTabRightAction, &QAction::triggered, this, &TabbedTerminal::moveTabRight);
    addAction(moveTabRightAction);

    QAction *configAction = new QAction(tr("Configure..."), this);
    configAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    configAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(configAction, &QAction::triggered, this, &TabbedTerminal::showConfigDialog);
    addAction(configAction);

    QAction *fullScreenAction = new QAction(tr("Toggle Full Screen"), this);
    fullScreenAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
    fullScreenAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(fullScreenAction, &QAction::triggered, this, &TabbedTerminal::toggleExpanded);
    addAction(fullScreenAction);

    for (int i = 1; i <= 9; ++i) {
        QAction *selectTabAction = new QAction(this);
        selectTabAction->setShortcut(QKeySequence(Qt::ALT | (Qt::Key_0 + i)));
        selectTabAction->setShortcutContext(Qt::ApplicationShortcut);
        connect(selectTabAction, &QAction::triggered, this, [this, i]() {
            if (i == 9) {
                m_tabs->setCurrentIndex(m_tabs->count() - 1);
            } else if (i <= m_tabs->count()) {
                m_tabs->setCurrentIndex(i - 1);
            }
        });
        addAction(selectTabAction);
    }

    QTimer *colorTimer = new QTimer(this);
    colorTimer->setInterval(1000);
    connect(colorTimer, &QTimer::timeout, this, &TabbedTerminal::updateTabColors);
    colorTimer->start();

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setInterval(60000); // 1 minute
    connect(m_autoSaveTimer, &QTimer::timeout, this, &TabbedTerminal::saveSession);
    m_autoSaveTimer->start();

    AppConfig::cleanupOldLogs();

    resize(1024, 768);
    // Restore Session
    QTimer::singleShot(0, this, [this]() {
        QSettings s;
        restoreGeometry(s.value("Window/Geometry").toByteArray());

        int tabCount = s.beginReadArray("Session/Tabs");
        if (tabCount > 0) {
            for (int i = 0; i < tabCount; ++i) {
                s.setArrayIndex(i);
                QString program = s.value("program").toString();
                QString cwd = s.value("cwd").toString();
                QString logPath = s.value("logPath").toString();
                addNewTab(program, cwd, logPath);
            }
            s.endArray();

            int activeTab = s.value("Session/ActiveTab", 0).toInt();
            if (activeTab >= 0 && activeTab < m_tabs->count()) {
                m_tabs->setCurrentIndex(activeTab);
            }
        } else {
            addNewTab();
        }
    });
}

TabbedTerminal::~TabbedTerminal() {
#ifdef Q_OS_WIN
    UnregisterHotKey((HWND)winId(), 100);
#endif
}

void TabbedTerminal::saveSession() {
    QSettings s;
    s.setValue("Window/Geometry", saveGeometry());

    // Clear previous array to ensure no artifacts remain
    s.remove("Session/Tabs");

    s.beginWriteArray("Session/Tabs");
    for (int i = 0; i < m_tabs->count(); ++i) {
        KodoTerm *console = qobject_cast<KodoTerm *>(m_tabs->widget(i));
        if (console) {
            s.setArrayIndex(i);
            s.setValue("program", console->program());
            s.setValue("cwd", console->cwd());
            s.setValue("logPath", console->logPath());
        }
    }
    s.endArray();
    s.setValue("Session/ActiveTab", m_tabs->currentIndex());
    s.sync(); // Force write to disk immediately
}

void TabbedTerminal::closeEvent(QCloseEvent *event) {
    QSettings s;
    bool enableTray = s.value("Window/EnableTray", false).toBool();
    if (enableTray && m_trayIcon && m_trayIcon->isVisible()) {
        hide();
        event->ignore();
    } else {
        saveSession();
        QMainWindow::closeEvent(event);
    }
}

void TabbedTerminal::addNewTab(const QString &program, const QString &workingDirectory,
                               const QString &logPath) {
    KodoTerm *console = new KodoTerm(m_tabs);

    // Load config
    QSettings settings;
    KodoTermConfig config;
    config.load(settings);
    console->setConfig(config);

    if (!program.isEmpty()) {
        console->setProgram(program);
    } else {
        QString defName = AppConfig::defaultShell();
        AppConfig::ShellInfo info = AppConfig::getShellInfo(defName);
        console->setProgram(info.path);
    }

    // Attempt to inject shell integration for CWD tracking (Bash mostly)
    QProcessEnvironment env = console->processEnvironment();
    QString progName = QFileInfo(console->program()).baseName();
    if (progName == "bash") {
        env.insert("PROMPT_COMMAND", "printf \"\\033]7;file://localhost%s\\033\\\\\" \"$PWD\"");
    }
    console->setProcessEnvironment(env);

    if (!workingDirectory.isEmpty()) {
        console->setWorkingDirectory(workingDirectory);
    }

    connect(console, &KodoTerm::windowTitleChanged, [this, console](const QString &title) {
        int index = m_tabs->indexOf(console);
        if (index != -1) {
            m_tabs->setTabText(index, title);
            updateTabColors();
        }
    });

    connect(console, &KodoTerm::cwdChanged, [this, console](const QString &) {
        console->setProperty("cwdReceived", true);
        updateTabColors();
    });

    connect(console, &KodoTerm::finished, this,
            [this, console](int exitCode, int exitStatus) { closeTab(console); });

    int index = m_tabs->addTab(console, tr("Terminal"));
    m_tabs->setCurrentIndex(index);
    console->setFocus();

    if (!logPath.isEmpty()) {
        console->setRestoreLog(logPath);
        console->start(false); // Do not reset if restoring
    } else {
        console->start(true);
    }
}

void TabbedTerminal::showConfigDialog() {
    ConfigDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        applySettings();
    }
}

void TabbedTerminal::toggleExpanded() {
    if (m_useFullScreenMode) {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
    } else {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
    }
}

void TabbedTerminal::applySettings() {
    QSettings s;
    m_useFullScreenMode = s.value("Window/UseFullScreenMode", false).toBool();
    setupTrayIcon();

    KodoTermConfig config;
    config.load(s);

    for (int i = 0; i < m_tabs->count(); ++i) {
        KodoTerm *console = qobject_cast<KodoTerm *>(m_tabs->widget(i));
        if (console) {
            console->setConfig(config);
        }
    }
}

void TabbedTerminal::closeCurrentTab() {
    int index = m_tabs->currentIndex();
    if (index != -1) {
        closeTab(m_tabs->widget(index));
    }
}

void TabbedTerminal::closeTab(QWidget *w) {
    if (m_tabs->count() == 1) {
        close();
        return;
    }
    int index = m_tabs->indexOf(w);
    if (index != -1) {
        m_tabs->removeTab(index);
        w->deleteLater();
        if (m_tabs->currentWidget()) {
            m_tabs->currentWidget()->setFocus();
        }
    }
}

void TabbedTerminal::nextTab() {
    int count = m_tabs->count();
    if (count <= 1) {
        return;
    }
    int index = m_tabs->currentIndex();
    m_tabs->setCurrentIndex((index + 1) % count);
}

void TabbedTerminal::previousTab() {
    int count = m_tabs->count();
    if (count <= 1) {
        return;
    }
    int index = m_tabs->currentIndex();
    m_tabs->setCurrentIndex((index - 1 + count) % count);
}

void TabbedTerminal::moveTabLeft() {
    int index = m_tabs->currentIndex();
    if (index > 0) {
        m_tabs->tabBar()->moveTab(index, index - 1);
    }
}

void TabbedTerminal::moveTabRight() {
    int index = m_tabs->currentIndex();
    if (index != -1 && index < m_tabs->count() - 1) {
        m_tabs->tabBar()->moveTab(index, index + 1);
    }
}

void TabbedTerminal::updateTabColors() {
    QTabBar *bar = m_tabs->tabBar();
    for (int i = 0; i < m_tabs->count(); ++i) {
        KodoTerm *console = qobject_cast<KodoTerm *>(m_tabs->widget(i));
        if (!console) {
            continue;
        }

        QString title = console->windowTitle();
        if (!title.isEmpty()) {
            QFileInfo titleInfo(title);
            QFileInfo progInfo(console->program());
            if (titleInfo.fileName().compare(progInfo.fileName(), Qt::CaseInsensitive) == 0) {
                title = titleInfo.baseName();
                if (!title.isEmpty() && title[0].isLower()) {
                    title[0] = title[0].toUpper();
                }
            }
        }

        if (title.isEmpty()) {
            title = tr("Terminal");
        }

        if (console->isRoot()) {
            bar->setTabTextColor(i, Qt::red);
            if (!title.startsWith("root@")) {
                title = "root@" + title;
            }
        } else {
            bar->setTabTextColor(i, QPalette().color(QPalette::WindowText));
            if (title.startsWith("root@")) {
                title = title.mid(5);
            }
        }
        m_tabs->setTabText(i, title);
        if (console->property("cwdReceived").toBool()) {
            m_tabs->setTabToolTip(i, console->cwd());
        } else {
            m_tabs->setTabToolTip(i, QString());
        }
    }
}

void TabbedTerminal::setupTrayIcon() {
    QSettings s;
    if (!s.value("Window/EnableTray", false).toBool()) {
        if (m_trayIcon) {
            m_trayIcon->hide();
            m_trayIcon->deleteLater();
            m_trayIcon = nullptr;
        }
        return;
    }

    if (m_trayIcon) {
        return;
    }

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(windowIcon());
    m_trayIcon->setToolTip("KodoShell");

#ifdef Q_OS_WIN
    // Register Ctrl+Alt+T globally (ID 100)
    RegisterHotKey((HWND)winId(), 100, MOD_CONTROL | MOD_ALT, 'T');
#endif

    QMenu *trayMenu = new QMenu(this);

    m_toggleWindowAction = trayMenu->addAction(tr("Show/Hide Window"), this, &TabbedTerminal::toggleWindowVisibility);
    m_toggleWindowAction->setShortcut(QKeySequence("Ctrl+Alt+T"));
    m_toggleWindowAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(m_toggleWindowAction);

    trayMenu->addAction(tr("Configure..."), this, &TabbedTerminal::showConfigDialog);
    trayMenu->addAction(tr("About..."), this, &TabbedTerminal::showAboutDialog);
    trayMenu->addSeparator();
    trayMenu->addAction(tr("Quit"), qApp, &QApplication::quit);

    m_trayIcon->setContextMenu(trayMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            toggleWindowVisibility();
        }
    });

    m_trayIcon->show();
}

void TabbedTerminal::toggleWindowVisibility() {
    if (isVisible() && !isMinimized()) {
        hide();
    } else {
        show();
        showNormal();
        activateWindow();
        raise();
    }
}

void TabbedTerminal::showAboutDialog() {
    QMessageBox::about(this, tr("About KodoShell"),
                       tr("KodoShell - A modern terminal emulator based on libvterm.\n\n"
                          "Copyright (C) 2025-2026 Diego Iastrubni"));
}

bool TabbedTerminal::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == 100) {
            toggleWindowVisibility();
            return true;
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}
