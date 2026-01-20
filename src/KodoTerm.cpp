// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "KodoTerm/KodoTerm.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>

KodoTerm::KodoTerm(QWidget *parent) : QWidget(parent) {
    m_session = new KodoTermSession(this);

    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    m_scrollBar = new QScrollBar(Qt::Vertical, this);
    m_scrollBar->setRange(0, 0);
    connect(m_scrollBar, &QScrollBar::valueChanged, this, &KodoTerm::onScrollValueChanged);

    m_cursorBlinkTimer = new QTimer(this);
    m_cursorBlinkTimer->setInterval(500);
    connect(m_cursorBlinkTimer, &QTimer::timeout, this, [this]() {
        if (m_session->cursorBlink()) {
            m_cursorBlinkState = !m_cursorBlinkState;
            update();
        }
    });
    m_cursorBlinkTimer->start();

    connect(m_session, &KodoTermSession::contentChanged, this, &KodoTerm::onContentChanged);
    connect(m_session, &KodoTermSession::scrollbackChanged, this, &KodoTerm::onScrollbackChanged);
    connect(m_session, &KodoTermSession::rectMoved, this,
            [this](const QRect &dest, const QRect &src) {
                m_renderer.moveRect(dest, src, m_scrollBar->value(), m_session->scrollbackSize());
                update();
            });
    connect(m_session, &KodoTermSession::finished, this, &KodoTerm::finished);
    connect(m_session, &KodoTermSession::cwdChanged, this, &KodoTerm::cwdChanged);
    connect(m_session, &KodoTermSession::titleChanged, this, &KodoTerm::setWindowTitle);
    connect(m_session, &KodoTermSession::propChanged, this,
            [this](VTermProp prop, const VTermValue *val) {
                if (prop == VTERM_PROP_ALTSCREEN) {
                    if (val->boolean) {
                        m_scrollBar->hide();
                    } else {
                        m_scrollBar->show();
                    }
                    updateTerminalSize();
                }
            });

    updateTerminalSize();
}

KodoTerm::~KodoTerm() {}

bool KodoTerm::start(bool reset) {
    if (reset) {
        m_cursorBlinkState = true;
        m_renderer.setDirty();
    }
    bool ok = m_session->start(reset);
    if (ok) {
        updateTerminalSize();
        setFocus();
    }
    return ok;
}

void KodoTerm::resizeEvent(QResizeEvent *) {
    int sbWidth = m_scrollBar->sizeHint().width();
    m_scrollBar->setGeometry(width() - sbWidth, 0, sbWidth, height());
    updateTerminalSize();
}

void KodoTerm::updateTerminalSize() {
    if (width() <= 0 || height() <= 0) {
        return;
    }
    m_renderer.updateSize(size(), devicePixelRatioF(), m_session,
                          m_scrollBar->isVisible() ? m_scrollBar->width() : 0);
    m_scrollBar->setPageStep(m_session->rows());
    update();
}

void KodoTerm::onContentChanged(const QRect &rect) {
    m_renderer.noteDamage(rect);
    update();
}

void KodoTerm::onScrollbackChanged() {
    bool atBottom = m_scrollBar->value() == m_scrollBar->maximum();
    m_scrollBar->setRange(0, m_session->scrollbackSize());
    if (atBottom) {
        m_scrollBar->setValue(m_session->scrollbackSize());
    }
}

void KodoTerm::onScrollValueChanged(int) {
    m_renderer.setDirty();
    update();
}

void KodoTerm::paintEvent(QPaintEvent *e) {
    QPainter painter(this);
    m_renderer.paint(painter, e->rect(), m_session, m_scrollBar->value(), hasFocus(),
                     m_cursorBlinkState);
}

void KodoTerm::keyPressEvent(QKeyEvent *e) {
    if (e->modifiers() & Qt::ShiftModifier) {
        switch (e->key()) {
        case Qt::Key_PageUp:
            m_scrollBar->setValue(m_scrollBar->value() - m_scrollBar->pageStep());
            return;
        case Qt::Key_PageDown:
            m_scrollBar->setValue(m_scrollBar->value() + m_scrollBar->pageStep());
            return;
        case Qt::Key_Home:
            m_scrollBar->setValue(0);
            return;
        case Qt::Key_End:
            m_scrollBar->setValue(m_scrollBar->maximum());
            return;
        }
    }

    if (m_scrollBar->value() < m_scrollBar->maximum()) {
        m_scrollBar->setValue(m_scrollBar->maximum());
    }
    m_session->sendKey(e->key(), e->modifiers(), e->text());
}

void KodoTerm::wheelEvent(QWheelEvent *e) {
    if (m_session->config().mouseWheelZoom && (e->modifiers() & Qt::ControlModifier)) {
        if (e->angleDelta().y() > 0) {
            zoomIn();
        } else if (e->angleDelta().y() < 0) {
            zoomOut();
        }
        return;
    }

    if (m_session->mouseMode() > 0 && !(e->modifiers() & Qt::ShiftModifier)) {
        int r = e->position().toPoint().y() / m_renderer.cellSize().height();
        int c = e->position().toPoint().x() / m_renderer.cellSize().width();
        int b = e->angleDelta().y() > 0 ? 4 : 5;
        m_session->sendMouse(b, r, c, e->modifiers(), true);
        return;
    }

    m_scrollBar->event(e);
}

void KodoTerm::mousePressEvent(QMouseEvent *e) {
    setFocus();
    int r = e->pos().y() / m_renderer.cellSize().height();
    int c = e->pos().x() / m_renderer.cellSize().width();
    int absR = m_scrollBar->value() + r;

    if (m_session->mouseMode() > 0 && !(e->modifiers() & Qt::ShiftModifier)) {
        int b = 0;
        if (e->button() == Qt::LeftButton) {
            b = 1;
        } else if (e->button() == Qt::MiddleButton) {
            b = 2;
        } else if (e->button() == Qt::RightButton) {
            b = 3;
        }
        if (b > 0) {
            m_session->sendMouse(b, absR, c, e->modifiers(), true);
            return;
        }
    }

    if (e->button() == Qt::LeftButton) {
        m_session->setSelection(QPoint(c, absR), QPoint(c, absR));
    } else if (e->button() == Qt::MiddleButton && m_session->config().pasteOnMiddleClick) {
        pasteFromClipboard();
    }
}

void KodoTerm::mouseDoubleClickEvent(QMouseEvent *) {}

void KodoTerm::mouseMoveEvent(QMouseEvent *e) {
    int r = e->pos().y() / m_renderer.cellSize().height();
    int c = e->pos().x() / m_renderer.cellSize().width();
    int absR = m_scrollBar->value() + r;

    if (m_session->mouseMode() > 0 && !(e->modifiers() & Qt::ShiftModifier)) {
        if (e->buttons() == Qt::NoButton) {
            m_session->sendMouseMove(absR, c, e->modifiers());
        } else {
            int b = 0;
            if (e->buttons() & Qt::LeftButton) {
                b = 1;
            } else if (e->buttons() & Qt::MiddleButton) {
                b = 2;
            } else if (e->buttons() & Qt::RightButton) {
                b = 3;
            }
            if (b > 0) {
                m_session->sendMouse(b, absR, c, e->modifiers(), true);
            }
        }
        return;
    }

    if (e->buttons() & Qt::LeftButton) {
        m_session->setSelection(m_session->selectionStart(), QPoint(c, absR));
    }
}

void KodoTerm::mouseReleaseEvent(QMouseEvent *e) {
    int r = e->pos().y() / m_renderer.cellSize().height();
    int c = e->pos().x() / m_renderer.cellSize().width();
    int absR = m_scrollBar->value() + r;

    if (m_session->mouseMode() > 0 && !(e->modifiers() & Qt::ShiftModifier)) {
        int b = 0;
        if (e->button() == Qt::LeftButton) {
            b = 1;
        } else if (e->button() == Qt::MiddleButton) {
            b = 2;
        } else if (e->button() == Qt::RightButton) {
            b = 3;
        }
        if (b > 0) {
            m_session->sendMouse(b, absR, c, e->modifiers(), false);
            return;
        }
    }

    if (e->button() == Qt::LeftButton) {
        m_session->setSelection(m_session->selectionStart(), QPoint(c, absR));
        if (m_session->config().copyOnSelect) {
            copyToClipboard();
        }
    }
}

void KodoTerm::contextMenuEvent(QContextMenuEvent *e) {
    QMenu menu(this);
    menu.addAction(tr("Copy"), this, &KodoTerm::copyToClipboard);
    menu.addAction(tr("Paste"), this, &KodoTerm::pasteFromClipboard);
    menu.addSeparator();

    QMenu *themeMenu = menu.addMenu(tr("Themes"));
    populateThemeMenu(themeMenu, "Konsole", TerminalTheme::ThemeFormat::Konsole,
                      [this](auto t) { m_session->setTheme(TerminalTheme::loadTheme(t.path)); });
    populateThemeMenu(themeMenu, "Windows Terminal", TerminalTheme::ThemeFormat::WindowsTerminal,
                      [this](auto t) { m_session->setTheme(TerminalTheme::loadTheme(t.path)); });

    menu.addSeparator();
    menu.addAction(tr("Clear Scrollback"), this, &KodoTerm::clearScrollback);
    menu.addAction(tr("Reset Terminal"), this, &KodoTerm::resetTerminal);
    menu.exec(e->globalPos());
}

bool KodoTerm::focusNextPrevChild(bool) { return false; }
void KodoTerm::focusInEvent(QFocusEvent *) {
    m_cursorBlinkState = true;
    update();
}
void KodoTerm::focusOutEvent(QFocusEvent *) { update(); }

void KodoTerm::copyToClipboard() {
    QString text = m_session->selectedText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void KodoTerm::pasteFromClipboard() { m_session->sendText(QApplication::clipboard()->text()); }

void KodoTerm::selectAll() { m_session->selectAll(); }
void KodoTerm::clearScrollback() { m_session->clearScrollback(); }
void KodoTerm::resetTerminal() {
    m_session->resetTerminal();
    m_renderer.setDirty();
    update();
}
void KodoTerm::openFileBrowser() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_session->workingDirectory()));
}

void KodoTerm::zoomIn() {
    KodoTermConfig c = m_session->config();
    c.font.setPointSizeF(c.font.pointSizeF() + 1);
    m_session->setConfig(c);
    updateTerminalSize();
}

void KodoTerm::zoomOut() {
    KodoTermConfig c = m_session->config();
    if (c.font.pointSizeF() > 4) {
        c.font.setPointSizeF(c.font.pointSizeF() - 1);
        m_session->setConfig(c);
        updateTerminalSize();
    }
}

void KodoTerm::resetZoom() {
    KodoTermConfig c = m_session->config();
    c.font.setPointSizeF(10);
    m_session->setConfig(c);
    updateTerminalSize();
}

void KodoTerm::populateThemeMenu(QMenu *pM, const QString &t, TerminalTheme::ThemeFormat f,
                                 const std::function<void(const TerminalTheme::ThemeInfo &)> &c) {
    QList<TerminalTheme::ThemeInfo> ths = TerminalTheme::builtInThemes();
    QList<TerminalTheme::ThemeInfo> fT;
    for (const auto &theme : ths) {
        if (theme.format == f) {
            fT.append(theme);
        }
    }
    if (fT.isEmpty()) {
        return;
    }
    QMenu *mT = pM->addMenu(t);
    auto aTA = [&](QMenu *m, const TerminalTheme::ThemeInfo &i) {
        m->addAction(i.name, [c, i]() { c(i); });
    };
    if (fT.size() < 26) {
        for (const auto &i : fT) {
            aTA(mT, i);
        }
    } else {
        QMap<QString, QMenu *> sM;
        for (const auto &i : fT) {
            QChar fLC = i.name.isEmpty() ? QChar('#') : i.name[0].toUpper();
            if (!fLC.isLetter()) {
                fLC = QChar('#');
            }
            QString fL(fLC);
            if (!sM.contains(fL)) {
                sM[fL] = mT->addMenu(fL);
            }
            aTA(sM[fL], i);
        }
    }
}