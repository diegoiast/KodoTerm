// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "TabbedTerminal.h"
#include <QToolButton>
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
    m_tabs->setCornerWidget(newTabBtn, Qt::TopLeftCorner);
    connect(newTabBtn, &QToolButton::clicked, this, &TabbedTerminal::addNewTab);

    // Close Tab button (Right corner)
    QToolButton *closeTabBtn = new QToolButton(m_tabs);
    closeTabBtn->setText("x");
    closeTabBtn->setToolTip(tr("Close Current Tab"));
    m_tabs->setCornerWidget(closeTabBtn, Qt::TopRightCorner);
    connect(closeTabBtn, &QToolButton::clicked, this, &TabbedTerminal::closeCurrentTab);

    addNewTab();
    resize(1024, 768);
}

void TabbedTerminal::addNewTab() {
    KodoTerm *console = new KodoTerm(m_tabs);
    
#ifdef Q_OS_WIN
    console->setProgram("powershell.exe");
#else
    console->setProgram("/bin/bash");
#endif
    console->setTheme(TerminalTheme::loadKonsoleTheme(":/KodoTermThemes/konsole/Breeze.colorscheme"));
    
    connect(console, &KodoTerm::windowTitleChanged, [this, console](const QString &title) {
        int index = m_tabs->indexOf(console);
        if (index != -1) {
            m_tabs->setTabText(index, title);
        }
    });

    connect(console, &KodoTerm::finished, this, [this, console]() {
        closeTab(console);
    });

    int index = m_tabs->addTab(console, tr("Terminal"));
    m_tabs->setCurrentIndex(index);
    console->setFocus();
    console->start();
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
    }
    if (m_tabs->count() == 0) {
        close();
    }
}
