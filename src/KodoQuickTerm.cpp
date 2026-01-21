// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "KodoTerm/KodoQuickTerm.hpp"
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDebug>
#include <QFileDialog> // For open file dialog
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QQuickWindow>

KodoQuickTerm::KodoQuickTerm(QQuickItem *parent) : QQuickPaintedItem(parent) {
    m_session = new KodoTermSession(this);

    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(ItemIsFocusScope, true);
    setFlag(ItemHasContents, true);

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(500);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        if (m_session->cursorBlink()) {
            m_blinkState = !m_blinkState;
            update();
        }
    });
    m_blinkTimer->start();

    connect(m_session, &KodoTermSession::contentChanged, this, &KodoQuickTerm::onContentChanged);

    connect(m_session, &KodoTermSession::rectMoved, this,
            [this](const QRect &dest, const QRect &src) {
                m_renderer.moveRect(dest, src, m_scrollValue, m_session->scrollbackSize());

                update();
            });

    connect(m_session, &KodoTermSession::scrollbackChanged, this,
            &KodoQuickTerm::onScrollbackChanged);

    connect(m_session, &KodoTermSession::cwdChanged, this, &KodoQuickTerm::cwdChanged);
    connect(m_session, &KodoTermSession::finished, this, &KodoQuickTerm::finished);
    connect(m_session, &KodoTermSession::bell, this, &KodoQuickTerm::bell); // Propagate bell
    connect(m_session, &KodoTermSession::propChanged, this,
            [this](VTermProp prop, const VTermValue *val) {
                if (prop == VTERM_PROP_ALTSCREEN) {
                    updateTerminalSize();
                }
            });
}

KodoQuickTerm::~KodoQuickTerm() {
    // Session is owned by this QObject, will be deleted automatically
}

void KodoQuickTerm::paint(QPainter *painter) {
    m_renderer.paint(*painter, boundingRect().toRect(), m_session, m_scrollValue, hasActiveFocus(),
                     m_blinkState);
}

void KodoQuickTerm::setTheme(const TerminalTheme &theme) {
    m_session->setTheme(theme);
    m_renderer.setDirty();
    update();
}

void KodoQuickTerm::setConfig(const KodoTermConfig &config) {
    m_session->setConfig(config);
    updateTerminalSize(); // Font size change might affect cell size
}

void KodoQuickTerm::setProgram(const QString &program) {
    if (m_session->program() != program) {
        m_session->setProgram(program);
        emit programChanged();
    }
}

QString KodoQuickTerm::program() const { return m_session->program(); }

void KodoQuickTerm::setArguments(const QStringList &arguments) {
    if (m_session->arguments() != arguments) {
        m_session->setArguments(arguments);
        emit argumentsChanged();
    }
}

QStringList KodoQuickTerm::arguments() const { return m_session->arguments(); }

void KodoQuickTerm::setWorkingDirectory(const QString &workingDirectory) {
    if (m_session->workingDirectory() != workingDirectory) {
        m_session->setWorkingDirectory(workingDirectory);
        emit workingDirectoryChanged();
    }
}

QString KodoQuickTerm::workingDirectory() const { return m_session->workingDirectory(); }

bool KodoQuickTerm::start(bool reset) {
    if (reset) {
        m_scrollValue = 0;
        emit scrollValueChanged();
        m_renderer.setDirty();
    }
    bool ok = m_session->start(reset);
    if (ok) {
        updateTerminalSize();
    }
    return ok;
}

void KodoQuickTerm::kill() { m_session->kill(); }

void KodoQuickTerm::setScrollValue(int value) {
    int max = m_session->scrollbackSize();
    value = std::clamp(value, 0, max);
    if (m_scrollValue != value) {
        m_scrollValue = value;
        m_renderer.setDirty();
        update();
        emit scrollValueChanged();
    }
}

void KodoQuickTerm::onContentChanged(const QRect &rect) {
    m_renderer.noteDamage(rect);
    update();
}

void KodoQuickTerm::onScrollbackChanged() {
    emit scrollMaxChanged();
    // If we were at the bottom, stay at the bottom
    if (m_scrollValue == m_session->scrollbackSize()) {
        m_scrollValue = m_session->scrollbackSize(); // Keep at new bottom
        emit scrollValueChanged();
    }
}

void KodoQuickTerm::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    updateTerminalSize();
}

void KodoQuickTerm::updateTerminalSize() {
    qreal dpr = window() ? window()->devicePixelRatio() : 1.0;
    m_renderer.updateSize(QSize(width(), height()), dpr, m_session);
    update();
}

void KodoQuickTerm::keyPressEvent(QKeyEvent *event) {
    if (event->modifiers() & Qt::ShiftModifier) {
        int rows = height() / m_renderer.cellSize().height();
        if (rows <= 0) {
            rows = 1;
        }
        switch (event->key()) {
        case Qt::Key_PageUp:
            setScrollValue(m_scrollValue - rows);
            return;
        case Qt::Key_PageDown:
            setScrollValue(m_scrollValue + rows);
            return;
        case Qt::Key_Home:
            setScrollValue(0);
            return;
        case Qt::Key_End:
            setScrollValue(m_session->scrollbackSize());
            return;
        }
    }

    if (m_scrollValue < m_session->scrollbackSize()) {
        setScrollValue(m_session->scrollbackSize());
    }
    m_session->sendKey(event->key(), event->modifiers(), event->text());
    if (event->isAccepted()) {
        update(); // Request redraw
    }
}

void KodoQuickTerm::wheelEvent(QWheelEvent *event) {
    int delta = event->angleDelta().y();
    if (delta != 0) {
        setScrollValue(m_scrollValue - (delta / 120));
        event->accept();
    }
}

void KodoQuickTerm::mousePressEvent(QMouseEvent *event) {
    forceActiveFocus();

    int r = event->position().y() / m_renderer.cellSize().height();
    int c = event->position().x() / m_renderer.cellSize().width();
    int absR = m_scrollValue + r;

    if (m_session->mouseMode() > 0 && !(event->modifiers() & Qt::ShiftModifier)) {
        int b = 0;
        if (event->button() == Qt::LeftButton) {
            b = 1;
        } else if (event->button() == Qt::MiddleButton) {
            b = 2;
        } else if (event->button() == Qt::RightButton) {
            b = 3;
        }

        if (b > 0) {
            m_session->sendMouse(b, absR, c, (Qt::KeyboardModifiers)event->modifiers(), true);
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton) {
        m_session->setSelection(QPoint(c, absR), QPoint(c, absR));
        update();
    }
    event->accept();
}

void KodoQuickTerm::mouseDoubleClickEvent(QMouseEvent *event) {
    // Implement word selection or line selection based on double click
    int r = event->position().y() / m_renderer.cellSize().height();
    int c = event->position().x() / m_renderer.cellSize().width();
    int absR = m_scrollValue + r;
    m_session->setSelection(QPoint(c, absR),
                            QPoint(c, absR)); // Placeholder: Needs proper word/line logic
    update();
    event->accept();
}

void KodoQuickTerm::mouseMoveEvent(QMouseEvent *event) {
    int r = event->position().y() / m_renderer.cellSize().height();
    int c = event->position().x() / m_renderer.cellSize().width();
    int absR = m_scrollValue + r;

    if (m_session->mouseMode() > 0 && !(event->modifiers() & Qt::ShiftModifier)) {
        if (event->buttons() == Qt::NoButton) {
            m_session->sendMouseMove(absR, c, (Qt::KeyboardModifiers)event->modifiers());
        } else {
            int b = 0;
            if (event->buttons() & Qt::LeftButton) {
                b = 1;
            } else if (event->buttons() & Qt::MiddleButton) {
                b = 2;
            } else if (event->buttons() & Qt::RightButton) {
                b = 3;
            }
            if (b > 0) {
                m_session->sendMouse(b, absR, c, (Qt::KeyboardModifiers)event->modifiers(), true);
            }
        }
        event->accept();
        return;
    }

    if (event->buttons() & Qt::LeftButton) {
        m_session->setSelection(m_session->selectionStart(), QPoint(c, absR));
        update();
    }
    event->accept();
}

void KodoQuickTerm::mouseReleaseEvent(QMouseEvent *event) {
    int r = event->position().y() / m_renderer.cellSize().height();
    int c = event->position().x() / m_renderer.cellSize().width();
    int absR = m_scrollValue + r;

    if (m_session->mouseMode() > 0 && !(event->modifiers() & Qt::ShiftModifier)) {
        int b = 0;
        if (event->button() == Qt::LeftButton) {
            b = 1;
        } else if (event->button() == Qt::MiddleButton) {
            b = 2;
        } else if (event->button() == Qt::RightButton) {
            b = 3;
        }

        if (b > 0) {
            m_session->sendMouse(b, absR, c, (Qt::KeyboardModifiers)event->modifiers(), false);
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton) {
        m_session->setSelection(m_session->selectionStart(), QPoint(c, absR));
        if (m_session->config().copyOnSelect && m_session->selectedText().size() > 0) {
            QApplication::clipboard()->setText(m_session->selectedText());
        }
        update();
    }
    event->accept();
}
