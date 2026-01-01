// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "TabbedTerminal.h"
#include "AppConfig.h"
#include "ConfigDialog.h"
#include <QToolButton>
#include <QTabBar>
#include <QMenu>
#include <QAction>
#include <QSettings>
#include <KodoTerm/KodoTerm.hpp>

TabbedTerminal::TabbedTerminal(QWidget *parent) : QMainWindow(parent) {
    m_tabs = new QTabWidget(this);
    m_tabs->setTabPosition(QTabWidget::South);
    m_tabs->setDocumentMode(true);
    m_tabs->setMovable(true);
    setCentralWidget(m_tabs);

    // New Tab button (Left corner)
    QToolButton *newTabBtn = new QToolButton(m_tabs);
    newTabBtn->setText("+");
    newTabBtn->setToolTip(tr("New Tab"));
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
    };
    updateMenu();
    connect(shellsMenu, &QMenu::aboutToShow, this, updateMenu); // Refresh menu on show

    newTabBtn->setMenu(shellsMenu);
    connect(newTabBtn, &QToolButton::clicked, this, [this](){ addNewTab(); });

    // Close Tab button (Right corner)
    QToolButton *closeTabBtn = new QToolButton(m_tabs);
    closeTabBtn->setText("x");
    closeTabBtn->setToolTip(tr("Close Current Tab"));
    m_tabs->setCornerWidget(closeTabBtn, Qt::TopRightCorner);
    connect(closeTabBtn, &QToolButton::clicked, this, &TabbedTerminal::closeCurrentTab);

    // Actions
    QAction *newTabAction = new QAction(tr("New Tab"), this);
    newTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    newTabAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(newTabAction, &QAction::triggered, this, [this](){ addNewTab(); });
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

    QTimer *colorTimer = new QTimer(this);
    colorTimer->setInterval(1000);
    connect(colorTimer, &QTimer::timeout, this, &TabbedTerminal::updateTabColors);
    colorTimer->start();

    addNewTab();
    resize(1024, 768);
}

void TabbedTerminal::addNewTab(const QString &program) {
    KodoTerm *console = new KodoTerm(m_tabs);
    
    // Load config
    QSettings s("Diego Iastrubni", "KodoTermTabbed");
    KodoTermConfig config;
    config.load(s);
    console->setConfig(config);

    if (!program.isEmpty()) {
        console->setProgram(program);
    } else {
        QString defName = AppConfig::defaultShell();
        AppConfig::ShellInfo info = AppConfig::getShellInfo(defName);
        console->setProgram(info.path);
    }
    
    // Theme is applied via setConfig, but let's make sure it's set if config was empty
    // config.load(s) might have loaded "Default" theme if settings were empty.
    
    connect(console, &KodoTerm::windowTitleChanged, [this, console](const QString &title) {
        int index = m_tabs->indexOf(console);
        if (index != -1) {
            m_tabs->setTabText(index, title);
            updateTabColors();
        }
    });

    connect(console, &KodoTerm::cwdChanged, this, &TabbedTerminal::updateTabColors);

    connect(console, &KodoTerm::finished, this, [this, console]() {
        closeTab(console);
    });

    int index = m_tabs->addTab(console, tr("Terminal"));
    m_tabs->setCurrentIndex(index);
    console->setFocus();
    console->start();
}

void TabbedTerminal::showConfigDialog() {
    ConfigDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        applySettings();
    }
}

void TabbedTerminal::applySettings() {
    QSettings s("Diego Iastrubni", "KodoTermTabbed");
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
    int index = m_tabs->indexOf(w);
    if (index != -1) {
        m_tabs->removeTab(index);
        w->deleteLater();
        if (m_tabs->currentWidget()) {
            m_tabs->currentWidget()->setFocus();
        }
    }
    if (m_tabs->count() == 0) {
        close();
    }
}

void TabbedTerminal::nextTab() {
    int count = m_tabs->count();
    if (count <= 1) return;
    int index = m_tabs->currentIndex();
    m_tabs->setCurrentIndex((index + 1) % count);
}

void TabbedTerminal::previousTab() {
    int count = m_tabs->count();
    if (count <= 1) return;
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
        if (!console)
            continue;

        QString title = console->windowTitle();
        if (title.isEmpty())
            title = tr("Terminal");

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
    }
}