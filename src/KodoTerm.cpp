// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "KodoTerm/KodoTerm.hpp"
#include "PtyProcess.h"

#include <vterm.h>

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMap>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QTextStream>
#include <QUrl>

static void vterm_output_callback(const char *s, size_t len, void *user) {
    auto *pty = static_cast<PtyProcess *>(user);
    if (pty) {
        pty->write(QByteArray(s, (int)len));
    }
}

static VTermColor toVTermColor(const QColor &c) {
    VTermColor vc;
    vc.type = VTERM_COLOR_RGB;
    vc.rgb.red = c.red();
    vc.rgb.green = c.green();
    vc.rgb.blue = c.blue();
    return vc;
}

void KodoTerm::setConfig(const KodoTermConfig &config) {
    m_config = config;
    setTheme(m_config.theme); // Applies colors
    updateTerminalSize();     // Updates font/cell size
    update();
}

void KodoTerm::setTheme(const TerminalTheme &theme) {
    m_config.theme = theme;
    VTermState *state = vterm_obtain_state(m_vterm);
    VTermColor fg = toVTermColor(theme.foreground);
    VTermColor bg = toVTermColor(theme.background);
    vterm_state_set_default_colors(state, &fg, &bg);

    for (int i = 0; i < 16; ++i) {
        VTermColor c = toVTermColor(theme.palette[i]);
        vterm_state_set_palette_color(state, i, &c);
    }
    update();
}

KodoTerm::KodoTerm(QWidget *parent) : QWidget(parent) {
    m_config.font.setStyleHint(QFont::Monospace);

    setFocusPolicy(Qt::StrongFocus);
    m_scrollBar = new QScrollBar(Qt::Vertical, this);
    m_scrollBar->setRange(0, 0);
    connect(m_scrollBar, &QScrollBar::valueChanged, this, &KodoTerm::onScrollValueChanged);

    updateTerminalSize(); // Calculates m_cellSize

    m_cursorBlinkTimer = new QTimer(this);
    m_cursorBlinkTimer->setInterval(500);
    connect(m_cursorBlinkTimer, &QTimer::timeout, this, [this]() {
        if (m_cursorBlink) {
            m_cursorBlinkState = !m_cursorBlinkState;
            update();
        }
    });

    m_vterm = vterm_new(25, 80);
    if (!m_vterm) {
        return;
    }
    vterm_set_utf8(m_vterm, 1);

    m_vtermScreen = vterm_obtain_screen(m_vterm);
    if (!m_vtermScreen) {
        return;
    }
    vterm_screen_enable_altscreen(m_vtermScreen, 1);

    m_cwd = QDir::currentPath();

    static VTermScreenCallbacks callbacks = {.damage = &KodoTerm::onDamage,
                                             .moverect = nullptr,
                                             .movecursor = &KodoTerm::onMoveCursor,
                                             .settermprop = &KodoTerm::onSetTermProp,
                                             .bell = &KodoTerm::onBell,
                                             .resize = nullptr,
                                             .sb_pushline = &KodoTerm::onSbPushLine,
                                             .sb_popline = &KodoTerm::onSbPopLine,
                                             .sb_clear = nullptr,
                                             .sb_pushline4 = nullptr};

    vterm_screen_set_callbacks(m_vtermScreen, &callbacks, this);
    vterm_screen_reset(m_vtermScreen, 1);

    if (!m_environment.contains("TERM")) {
        m_environment.insert("TERM", "xterm-256color");
    }
    if (!m_environment.contains("COLORTERM")) {
        m_environment.insert("COLORTERM", "truecolor");
    }

    setTheme(m_config.theme);

    VTermState *state = vterm_obtain_state(m_vterm);
    static VTermStateFallbacks fallbacks = {
        .control = nullptr,
        .csi = nullptr,
        .osc = &KodoTerm::onOsc,
        .dcs = nullptr,
        .apc = nullptr,
        .pm = nullptr,
        .sos = nullptr,
    };
    vterm_state_set_unrecognised_fallbacks(state, &fallbacks, this);

    setFocusPolicy(Qt::StrongFocus);
}

KodoTerm::~KodoTerm() {
    if (m_pty) {
        m_pty->kill();
    }
    if (m_vterm) {
        vterm_free(m_vterm);
    }
}

bool KodoTerm::start(bool reset) {
    if (m_pty) {
        m_pty->kill();
        delete m_pty;
        m_pty = nullptr;
    }

    if (reset) {
        resetTerminal();
    }
    setupPty();

    if (m_program.isEmpty()) {
        return false;
    }

    m_pty->setProgram(m_program);
    m_pty->setArguments(m_arguments);
    m_pty->setWorkingDirectory(m_workingDirectory);
    m_pty->setProcessEnvironment(m_environment);

    if (m_config.enableLogging) {
        if (m_logFile.isOpen()) {
            m_logFile.close();
        }
        QDir logDir(m_config.logDirectory);
        if (!logDir.exists()) {
            logDir.mkpath(".");
        }
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
        QString logName = QString("kodoterm_%1.log").arg(timestamp);
        m_logFile.setFileName(logDir.filePath(logName));
        if (m_logFile.open(QIODevice::WriteOnly)) {
            QString header = QString("--- KodoTerm Session Log ---\n"
                                     "Program: %1\n"
                                     "Arguments: %2\n"
                                     "CWD: %3\n"
                                     "Start: %4\n"
                                     "LOG_START_MARKER\n")
                                 .arg(m_program)
                                 .arg(m_arguments.join(" "))
                                 .arg(m_workingDirectory)
                                 .arg(timestamp);
            m_logFile.write(header.toUtf8());
            m_logFile.flush();
        }
    }

    // Initial size
    QSize size(80, 25);
    if (!m_cellSize.isEmpty()) {
        int rows = height() / m_cellSize.height();
        int sbWidth = m_scrollBar->isVisible() ? m_scrollBar->sizeHint().width() : 0;
        int cols = (width() - sbWidth) / m_cellSize.width();

        size = QSize(cols, rows);
        if (size.width() <= 0) {
            size.setWidth(80);
        }
        if (size.height() <= 0) {
            size.setHeight(25);
        }
    }

    updateTerminalSize();
    bool success = m_pty->start(size);
    return success;
}

void KodoTerm::setupPty() {
    if (m_pty) {
        return;
    }

    m_pty = PtyProcess::create(this);
    if (!m_pty) {
        qWarning() << "Failed to create PtyProcess backend";
        return;
    }
    connect(m_pty, &PtyProcess::readyRead, this, &KodoTerm::onPtyReadyRead);
    connect(m_pty, &PtyProcess::finished, this, &KodoTerm::finished);
    vterm_output_set_callback(m_vterm, vterm_output_callback, m_pty);
}

void KodoTerm::onPtyReadyRead(const QByteArray &data) {
    if (!data.isEmpty()) {
        if (m_logFile.isOpen()) {
            m_logFile.write(data);
            m_logFile.flush();
        }
        vterm_input_write(m_vterm, data.constData(), data.size());
        vterm_screen_flush_damage(m_vtermScreen);
    }
}

void KodoTerm::onScrollValueChanged(int value) { update(); }

void KodoTerm::scrollUp(int lines) { m_scrollBar->setValue(m_scrollBar->value() - lines); }

void KodoTerm::scrollDown(int lines) { m_scrollBar->setValue(m_scrollBar->value() + lines); }

void KodoTerm::pageUp() { scrollUp(m_scrollBar->pageStep()); }

void KodoTerm::pageDown() { scrollDown(m_scrollBar->pageStep()); }

int KodoTerm::onSbPushLine(int cols, const VTermScreenCell *cells, void *user) {
    auto *widget = static_cast<KodoTerm *>(user);
    return widget->pushScrollback(cols, cells);
}

int KodoTerm::onSbPopLine(int cols, VTermScreenCell *cells, void *user) {
    auto *widget = static_cast<KodoTerm *>(user);
    return widget->popScrollback(cols, cells);
}

int KodoTerm::onOsc(int command, VTermStringFragment frag, void *user) {
    auto *widget = static_cast<KodoTerm *>(user);
    if (frag.initial) {
        widget->m_oscBuffer.clear();
    }
    widget->m_oscBuffer.append(frag.str, frag.len);
    if (frag.final) {
        if (command == 7) {
            QString urlStr = QString::fromUtf8(widget->m_oscBuffer);
            // Robust cleanup of trailing control/separator characters
            while (!urlStr.isEmpty() && (urlStr.endsWith(';') || urlStr.endsWith('\a') ||
                                         urlStr.endsWith('\r') || urlStr.endsWith('\n'))) {
                urlStr.chop(1);
            }

            if (urlStr.startsWith("file://")) {
                QUrl qurl(urlStr);
                QString path = qurl.toLocalFile();
                if (path.isEmpty() || (path.startsWith("//") && !qurl.host().isEmpty())) {
                    path = qurl.path();
                }

                if (!path.isEmpty() && widget->m_cwd != path) {
                    widget->m_cwd = path;
                    emit widget->cwdChanged(path);
                }
            } else {
                if (!urlStr.isEmpty() && widget->m_cwd != urlStr) {
                    widget->m_cwd = urlStr;
                    emit widget->cwdChanged(urlStr);
                }
            }
        }
        widget->m_oscBuffer.clear();
    }
    return 1;
}

int KodoTerm::pushScrollback(int cols, const VTermScreenCell *cells) {
    if (m_altScreen) {
        return 0;
    }
    SavedLine line;
    line.reserve(cols);
    for (int i = 0; i < cols; ++i) {
        SavedCell sc;
        memcpy(sc.chars, cells[i].chars, sizeof(sc.chars));
        sc.attrs = cells[i].attrs;
        sc.fg = cells[i].fg;
        sc.bg = cells[i].bg;
        sc.width = cells[i].width;
        line.push_back(sc);
    }
    m_scrollback.push_back(std::move(line));

    if ((int)m_scrollback.size() > m_config.maxScrollback) {
        m_scrollback.pop_front();
    }

    bool atBottom = m_scrollBar->value() == m_scrollBar->maximum();
    m_scrollBar->setRange(0, (int)m_scrollback.size());
    if (atBottom) {
        m_scrollBar->setValue(m_scrollBar->maximum());
    }

    return 1;
}

int KodoTerm::popScrollback(int cols, VTermScreenCell *cells) {
    if (m_scrollback.empty()) {
        return 0;
    }

    const SavedLine &line = m_scrollback.back();
    int to_copy = std::min(cols, (int)line.size());
    for (int i = 0; i < to_copy; ++i) {
        memcpy(cells[i].chars, line[i].chars, sizeof(cells[i].chars));
        cells[i].attrs = line[i].attrs;
        cells[i].fg = line[i].fg;
        cells[i].bg = line[i].bg;
        cells[i].width = line[i].width;
    }
    for (int i = to_copy; i < cols; ++i) {
        memset(&cells[i], 0, sizeof(VTermScreenCell));
        cells[i].width = 1;
    }

    m_scrollback.pop_back();
    m_scrollBar->setRange(0, (int)m_scrollback.size());
    return 1;
}

void KodoTerm::updateTerminalSize() {
    QFontMetrics fm(m_config.font);
    m_cellSize = QSize(fm.horizontalAdvance('W'), fm.height());
    if (m_cellSize.width() <= 0 || m_cellSize.height() <= 0) {
        m_cellSize = QSize(10, 20);
    }

    int rows = height() / m_cellSize.height();
    int sbWidth = m_scrollBar->isVisible() ? m_scrollBar->sizeHint().width() : 0;
    int cols = (width() - sbWidth) / m_cellSize.width();
    if (rows <= 0) {
        rows = 1;
    }
    if (cols <= 0) {
        cols = 1;
    }

    int oldRows, oldCols;
    if (m_vterm) {
        vterm_get_size(m_vterm, &oldRows, &oldCols);
        if (rows == oldRows && cols == oldCols) {
            // Even if size is the same, check if we need to replay log
            if (m_pendingLogReplay.isEmpty()) {
                return;
            }
        }

        bool atBottom = m_scrollBar->value() == m_scrollBar->maximum();
        vterm_set_size(m_vterm, rows, cols);
        vterm_screen_flush_damage(m_vtermScreen);
        m_scrollBar->setPageStep(rows);
        if (atBottom) {
            m_scrollBar->setValue(m_scrollBar->maximum());
        }

        // Perform deferred log restoration if size is sane
        if (!m_pendingLogReplay.isEmpty() && cols > 40) {
            QString logPath = m_pendingLogReplay;
            m_pendingLogReplay.clear(); // Clear first to avoid recursion

            QFile oldLog(logPath);
            if (oldLog.open(QIODevice::ReadOnly)) {
                QByteArray data = oldLog.readAll();
                int headerEnd = data.indexOf("LOG_START_MARKER\n");
                if (headerEnd != -1) {
                    data = data.mid(headerEnd + 17);
                }
                if (!data.isEmpty()) {
                    onPtyReadyRead(data);
                    onPtyReadyRead("\r\n");
                    scrollToBottom();
                }
                oldLog.close();
            }
        }
    }

    if (m_pty) {
        m_pty->resize(QSize(cols, rows));
    }
    update();
}

int KodoTerm::onDamage(VTermRect rect, void *user) {
    auto *widget = static_cast<KodoTerm *>(user);
    int scrollbackLines = (int)widget->m_scrollback.size();
    int currentScrollPos = widget->m_scrollBar->value();

    if (currentScrollPos < scrollbackLines) {
        widget->update();
        return 1;
    }

    int w = widget->m_cellSize.width();
    int h = widget->m_cellSize.height();
    widget->update(rect.start_col * w, rect.start_row * h, (rect.end_col - rect.start_col) * w,
                   (rect.end_row - rect.start_row) * h);
    return 1;
}

int KodoTerm::onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user) {
    auto *widget = static_cast<KodoTerm *>(user);
    widget->m_cursorRow = pos.row;
    widget->m_cursorCol = pos.col;
    widget->m_cursorVisible = visible;

    int scrollbackLines = (int)widget->m_scrollback.size();
    int currentScrollPos = widget->m_scrollBar->value();
    if (currentScrollPos == scrollbackLines) {
        widget->update();
    }
    return 1;
}

int KodoTerm::onSetTermProp(VTermProp prop, VTermValue *val, void *user) {
    auto *widget = static_cast<KodoTerm *>(user);
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        widget->m_cursorVisible = val->boolean;
        break;
    case VTERM_PROP_CURSORBLINK:
        widget->m_cursorBlink = val->boolean;
        if (widget->m_cursorBlink) {
            widget->m_cursorBlinkTimer->start();
        } else {
            widget->m_cursorBlinkTimer->stop();
            widget->m_cursorBlinkState = true;
        }
        break;
    case VTERM_PROP_CURSORSHAPE:
        widget->m_cursorShape = val->number;
        break;
    case VTERM_PROP_ALTSCREEN:
        widget->m_altScreen = val->boolean;
        if (widget->m_altScreen) {
            widget->m_scrollBar->hide();
        } else {
            widget->m_scrollBar->show();
        }
        widget->updateTerminalSize();
        break;
    case VTERM_PROP_TITLE:
        widget->setWindowTitle(QString::fromUtf8(val->string.str, (int)val->string.len));
        break;
    case VTERM_PROP_MOUSE:
        widget->m_mouseMode = val->number;
        break;
    default:
        break;
    }
    widget->update();
    return 1;
}

int KodoTerm::onBell(void *user) {
    auto *widget = static_cast<KodoTerm *>(user);
    if (widget->m_config.audibleBell) {
        QApplication::beep();
    }
    if (widget->m_config.visualBell) {
        widget->m_visualBellActive = true;
        widget->update();
        QTimer::singleShot(100, widget, [widget]() {
            widget->m_visualBellActive = false;
            widget->update();
        });
    }
    return 1;
}

void KodoTerm::resizeEvent(QResizeEvent *event) {
    int sbWidth = m_scrollBar->sizeHint().width();
    m_scrollBar->setGeometry(width() - sbWidth, 0, sbWidth, height());
    updateTerminalSize();
    QWidget::resizeEvent(event);
}

void KodoTerm::wheelEvent(QWheelEvent *event) {
    if (m_config.mouseWheelZoom && (event->modifiers() & Qt::ControlModifier)) {
        if (event->angleDelta().y() > 0) {
            zoomIn();
        } else if (event->angleDelta().y() < 0) {
            zoomOut();
        }
        return;
    }

    if (m_mouseMode > 0 && !(event->modifiers() & Qt::ShiftModifier)) {
        VTermModifier mod = VTERM_MOD_NONE;
        if (event->modifiers() & Qt::ShiftModifier) {
            mod = (VTermModifier)(mod | VTERM_MOD_SHIFT);
        }
        if (event->modifiers() & Qt::ControlModifier) {
            mod = (VTermModifier)(mod | VTERM_MOD_CTRL);
        }
        if (event->modifiers() & Qt::AltModifier) {
            mod = (VTermModifier)(mod | VTERM_MOD_ALT);
        }

        int screenRow = event->position().toPoint().y() / m_cellSize.height();
        int screenCol = event->position().toPoint().x() / m_cellSize.width();
        int button = event->angleDelta().y() > 0 ? 4 : 5;

        vterm_mouse_move(m_vterm, screenRow, screenCol, mod);
        vterm_mouse_button(m_vterm, button, true, mod);
        vterm_screen_flush_damage(m_vtermScreen);
        return;
    }

    m_scrollBar->event(event);
}

void KodoTerm::mousePressEvent(QMouseEvent *event) {
    VTermModifier mod = VTERM_MOD_NONE;
    if (event->modifiers() & Qt::ShiftModifier) {
        mod = (VTermModifier)(mod | VTERM_MOD_SHIFT);
    }
    if (event->modifiers() & Qt::ControlModifier) {
        mod = (VTermModifier)(mod | VTERM_MOD_CTRL);
    }
    if (event->modifiers() & Qt::AltModifier) {
        mod = (VTermModifier)(mod | VTERM_MOD_ALT);
    }

    VTermPos vpos = mouseToPos(event->pos());
    int screenRow = event->pos().y() / m_cellSize.height();
    int screenCol = event->pos().x() / m_cellSize.width();

    if (m_mouseMode > 0 && !(event->modifiers() & Qt::ShiftModifier)) {
        int button = 0;
        if (event->button() == Qt::LeftButton) {
            button = 1;
        } else if (event->button() == Qt::MiddleButton) {
            button = 2;
        } else if (event->button() == Qt::RightButton) {
            button = 3;
        }

        if (button > 0) {
            vterm_mouse_move(m_vterm, screenRow, screenCol, mod);
            vterm_mouse_button(m_vterm, button, true, mod);
            vterm_screen_flush_damage(m_vtermScreen);
            return;
        }
    }

    if (event->button() == Qt::LeftButton) {
        m_selecting = true;
        m_selectionStart = vpos;
        m_selectionEnd = m_selectionStart;
        update();
    } else if (event->button() == Qt::MiddleButton && m_config.pasteOnMiddleClick) {
        pasteFromClipboard();
    }
}

void KodoTerm::mouseMoveEvent(QMouseEvent *event) {
    VTermModifier mod = VTERM_MOD_NONE;
    if (event->modifiers() & Qt::ShiftModifier) {
        mod = (VTermModifier)(mod | VTERM_MOD_SHIFT);
    }
    if (event->modifiers() & Qt::ControlModifier) {
        mod = (VTermModifier)(mod | VTERM_MOD_CTRL);
    }
    if (event->modifiers() & Qt::AltModifier) {
        mod = (VTermModifier)(mod | VTERM_MOD_ALT);
    }

    VTermPos vpos = mouseToPos(event->pos());
    int screenRow = event->pos().y() / m_cellSize.height();
    int screenCol = event->pos().x() / m_cellSize.width();

    if (m_mouseMode > 0 && !(event->modifiers() & Qt::ShiftModifier)) {
        vterm_mouse_move(m_vterm, screenRow, screenCol, mod);
        vterm_screen_flush_damage(m_vtermScreen);
        return;
    }

    if (m_selecting) {
        m_selectionEnd = vpos;
        update();
    }
}

void KodoTerm::mouseReleaseEvent(QMouseEvent *event) {
    VTermModifier mod = VTERM_MOD_NONE;
    if (event->modifiers() & Qt::ShiftModifier) {
        mod = (VTermModifier)(mod | VTERM_MOD_SHIFT);
    }
    if (event->modifiers() & Qt::ControlModifier) {
        mod = (VTermModifier)(mod | VTERM_MOD_CTRL);
    }
    if (event->modifiers() & Qt::AltModifier) {
        mod = (VTermModifier)(mod | VTERM_MOD_ALT);
    }

    VTermPos vpos = mouseToPos(event->pos());
    int screenRow = event->pos().y() / m_cellSize.height();
    int screenCol = event->pos().x() / m_cellSize.width();

    if (m_mouseMode > 0 && !(event->modifiers() & Qt::ShiftModifier)) {
        int button = 0;
        if (event->button() == Qt::LeftButton) {
            button = 1;
        } else if (event->button() == Qt::MiddleButton) {
            button = 2;
        } else if (event->button() == Qt::RightButton) {
            button = 3;
        }

        if (button > 0) {
            vterm_mouse_move(m_vterm, screenRow, screenCol, mod);
            vterm_mouse_button(m_vterm, button, false, mod);
            vterm_screen_flush_damage(m_vtermScreen);
            return;
        }
    }

    if (event->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        m_selectionEnd = mouseToPos(event->pos());
        if (m_selectionStart.row == m_selectionEnd.row &&
            m_selectionStart.col == m_selectionEnd.col) {
            m_selectionStart = {-1, -1};
            m_selectionEnd = {-1, -1};
        } else if (m_config.copyOnSelect) {
            copyToClipboard();
        }
        update();
    }
}

VTermPos KodoTerm::mouseToPos(const QPoint &pos) const {
    if (m_cellSize.width() <= 0 || m_cellSize.height() <= 0) {
        return {0, 0};
    }
    int row = pos.y() / m_cellSize.height();
    int col = pos.x() / m_cellSize.width();
    int scrollbackLines = (int)m_scrollback.size();
    int currentScrollPos = m_scrollBar->value();

    VTermPos vpos;
    vpos.row = currentScrollPos + row;
    vpos.col = col;
    return vpos;
}

bool KodoTerm::isSelected(int row, int col) const {
    if (m_selectionStart.row == -1) {
        return false;
    }

    VTermPos start = m_selectionStart;
    VTermPos end = m_selectionEnd;
    if (start.row > end.row || (start.row == end.row && start.col > end.col)) {
        std::swap(start, end);
    }
    if (row < start.row || row > end.row) {
        return false;
    }
    if (row == start.row && row == end.row) {
        return col >= start.col && col <= end.col;
    }
    if (row == start.row) {
        return col >= start.col;
    }
    if (row == end.row) {
        return col <= end.col;
    }
    return true;
}

QString KodoTerm::getTextRange(VTermPos start, VTermPos end) {
    if (start.row > end.row || (start.row == end.row && start.col > end.col)) {
        std::swap(start, end);
    }

    QString text;
    int scrollbackLines = (int)m_scrollback.size();
    int rows, cols;
    vterm_get_size(m_vterm, &rows, &cols);

    for (int r = start.row; r <= end.row; ++r) {
        int startCol = (r == start.row) ? start.col : 0;
        int endCol = (r == end.row) ? end.col : 1000; // Arbitrary large number

        if (r < scrollbackLines) {
            const SavedLine &line = m_scrollback[r];
            for (int c = startCol; c <= endCol && c < (int)line.size(); ++c) {
                for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && line[c].chars[i]; ++i) {
                    text.append(QChar::fromUcs4(line[c].chars[i]));
                }
            }
        } else {
            int vtermRow = r - scrollbackLines;
            if (vtermRow < rows) {
                for (int c = startCol; c <= endCol && c < cols; ++c) {
                    VTermScreenCell cell;
                    vterm_screen_get_cell(m_vtermScreen, {vtermRow, c}, &cell);
                    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i) {
                        text.append(QChar::fromUcs4(cell.chars[i]));
                    }
                }
            }
        }
        if (r < end.row) {
            text.append('\n');
        }
    }
    return text;
}

void KodoTerm::copyToClipboard() {
    if (m_selectionStart.row == -1) {
        return;
    }
    QString text = getTextRange(m_selectionStart, m_selectionEnd);
    QApplication::clipboard()->setText(text);
}

void KodoTerm::pasteFromClipboard() {
    QString text = QApplication::clipboard()->text();
    if (!text.isEmpty()) {
        m_pty->write(text.toUtf8());
    }
}

void KodoTerm::selectAll() {
    int rows, cols;
    vterm_get_size(m_vterm, &rows, &cols);
    int scrollbackLines = (int)m_scrollback.size();

    m_selectionStart = {0, 0};
    m_selectionEnd = {scrollbackLines + rows - 1, cols - 1};
    update();
}

void KodoTerm::clearScrollback() {
    m_scrollback.clear();
    m_scrollBar->setRange(0, 0);
    m_scrollBar->setValue(0);
    update();
}

void KodoTerm::resetTerminal() {
    vterm_screen_reset(m_vtermScreen, 1);
    m_flowControlStopped = false;
    clearScrollback();
}

void KodoTerm::openFileBrowser() {
    if (!m_cwd.isEmpty()) {
        QDir dir(m_cwd);
        if (dir.exists()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
        }
    }
}

void KodoTerm::kill() {
    if (m_pty) {
        m_pty->kill();
    }
}

void KodoTerm::logData(const QByteArray &data) {
    if (m_logFile.isOpen()) {
        m_logFile.write(data);
        m_logFile.flush();
    }
}

void KodoTerm::scrollToBottom() {
    if (m_scrollBar) {
        m_scrollBar->setValue(m_scrollBar->maximum());
    }
}

void KodoTerm::contextMenuEvent(QContextMenuEvent *event) {
    if (m_mouseMode > 0 && !(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)) {
        return;
    }
    auto *menu = new QMenu(this);

    auto *copyAction = menu->addAction(tr("Copy"), this, &KodoTerm::copyToClipboard);
    copyAction->setEnabled(m_selectionStart.row != -1);
    copyAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));

    auto *pasteAction = menu->addAction(tr("Paste"), this, &KodoTerm::pasteFromClipboard);
    pasteAction->setEnabled(!QApplication::clipboard()->text().isEmpty());
    pasteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));

    menu->addSeparator();
    menu->addAction(tr("Select All"), this, &KodoTerm::selectAll);
    menu->addSeparator();
    menu->addAction(tr("Clear Scrollback"), this, &KodoTerm::clearScrollback);
    menu->addAction(tr("Reset"), this, &KodoTerm::resetTerminal);

    menu->addSeparator();

    auto *openBrowserAction = menu->addAction(tr("Open current directory in file browser"), this,
                                              &KodoTerm::openFileBrowser);
    openBrowserAction->setEnabled(!m_cwd.isEmpty() && QDir(m_cwd).exists());

    menu->addSeparator();

    menu->addAction(tr("Zoom In"), this, &KodoTerm::zoomIn);
    menu->addAction(tr("Zoom Out"), this, &KodoTerm::zoomOut);
    menu->addAction(tr("Reset Zoom"), this, &KodoTerm::resetZoom);
    menu->addSeparator();
    auto *themesMenu = menu->addMenu(tr("Themes"));
    auto *konsoleMenu = themesMenu->addMenu(tr("Konsole"));
    auto *wtMenu = themesMenu->addMenu(tr("Windows Terminal"));

    populateThemeMenu(konsoleMenu, ":/KodoTermThemes/konsole", TerminalTheme::ThemeFormat::Konsole);
    populateThemeMenu(wtMenu, ":/KodoTermThemes/windowsterminal",
                      TerminalTheme::ThemeFormat::WindowsTerminal);

    emit contextMenuRequested(menu, event->globalPos());
    menu->exec(event->globalPos());
    delete menu;
}

void KodoTerm::zoomIn() {
    qreal size = m_config.font.pointSizeF();
    if (size <= 0) {
        size = m_config.font.pointSize();
    }
    m_config.font.setPointSizeF(size + 1.0);
    updateTerminalSize();
    update();
}

void KodoTerm::zoomOut() {
    qreal size = m_config.font.pointSizeF();
    if (size <= 0) {
        size = m_config.font.pointSize();
    }
    if (size > 1.0) {
        m_config.font.setPointSizeF(size - 1.0);
        updateTerminalSize();
        update();
    }
}

void KodoTerm::resetZoom() {
    m_config.font.setPointSize(10);
    updateTerminalSize();
    update();
}

bool KodoTerm::isRoot() const { return m_pty && m_pty->isRoot(); }

QColor KodoTerm::mapColor(const VTermColor &c, const VTermState *state) const {
    if (VTERM_COLOR_IS_RGB(&c)) {
        return QColor(c.rgb.red, c.rgb.green, c.rgb.blue);
    } else if (VTERM_COLOR_IS_INDEXED(&c)) {
        VTermColor rgb = c;
        vterm_state_convert_color_to_rgb(state, &rgb);
        return QColor(rgb.rgb.red, rgb.rgb.green, rgb.rgb.blue);
    }
    return Qt::white;
}

void KodoTerm::drawCell(QPainter &painter, int row, int col, const VTermScreenCell &cell,
                        bool selected) {
    VTermState *state = vterm_obtain_state(m_vterm);
    VTermColor vdefault_fg, vdefault_bg;
    vterm_state_get_default_colors(state, &vdefault_fg, &vdefault_bg);

    QColor fg = mapColor(vdefault_fg, state);
    QColor bg = mapColor(vdefault_bg, state);

    if (!VTERM_COLOR_IS_DEFAULT_FG(&cell.fg)) {
        fg = mapColor(cell.fg, state);
    }
    if (!VTERM_COLOR_IS_DEFAULT_BG(&cell.bg)) {
        bg = mapColor(cell.bg, state);
    }
    if (cell.attrs.reverse ^ selected) {
        std::swap(fg, bg);
    }

    int cellWidth = cell.width;
    if (cellWidth <= 0) cellWidth = 1;

    QRect rect(col * m_cellSize.width(), row * m_cellSize.height(), m_cellSize.width() * cellWidth,
               m_cellSize.height());
    painter.fillRect(rect, bg);
    if (cell.chars[0] != 0) {
        painter.setPen(fg);
        QString s;
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; i++) {
            s.append(QChar::fromUcs4(cell.chars[i]));
        }
        painter.drawText(rect, Qt::AlignCenter, s);
    }
}

void KodoTerm::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setFont(m_config.font);

    VTermState *state = vterm_obtain_state(m_vterm);
    VTermColor vdefault_fg, vdefault_bg;
    vterm_state_get_default_colors(state, &vdefault_fg, &vdefault_bg);
    QColor defaultBg = mapColor(vdefault_bg, state);

    painter.fillRect(rect(), defaultBg);

    int rows, cols;
    vterm_get_size(m_vterm, &rows, &cols);
    int scrollbackLines = (int)m_scrollback.size();
    int currentScrollPos = m_scrollBar->value();
    for (int row = 0; row < rows; ++row) {
        int absoluteRow = currentScrollPos + row;
        if (absoluteRow < scrollbackLines) {
            const SavedLine &line = m_scrollback[absoluteRow];
            for (int col = 0; col < (int)line.size() && col < cols; ++col) {
                VTermScreenCell cell;
                const SavedCell &sc = line[col];
                memcpy(cell.chars, sc.chars, sizeof(cell.chars));
                cell.attrs = sc.attrs;
                cell.fg = sc.fg;
                cell.bg = sc.bg;
                cell.width = sc.width;
                drawCell(painter, row, col, cell, isSelected(absoluteRow, col));
                if (cell.width > 1) col += (cell.width - 1);
            }
        } else {
            int vtermRow = absoluteRow - scrollbackLines;
            for (int col = 0; col < cols; ++col) {
                VTermScreenCell cell;
                vterm_screen_get_cell(m_vtermScreen, {vtermRow, col}, &cell);
                drawCell(painter, row, col, cell, isSelected(absoluteRow, col));
                if (cell.width > 1) col += (cell.width - 1);
            }
        }
    }

    if (m_cursorVisible && currentScrollPos == scrollbackLines &&
        (!m_cursorBlink || m_cursorBlinkState)) {
        QRect cursorRect(m_cursorCol * m_cellSize.width(), m_cursorRow * m_cellSize.height(),
                         m_cellSize.width(), m_cellSize.height());

        painter.setCompositionMode(QPainter::CompositionMode_Difference);
        switch (m_cursorShape) {
        case 2: // Underline
        case 3: // VTERM_PROP_CURSORSHAPE_UNDERLINE
            painter.fillRect(cursorRect.x(), cursorRect.y() + cursorRect.height() - 2,
                             cursorRect.width(), 2, Qt::white);
            break;
        case 4: // Bar
        case 5: // VTERM_PROP_CURSORSHAPE_BAR_LEFT
            painter.fillRect(cursorRect.x(), cursorRect.y(), 2, cursorRect.height(), Qt::white);
            break;
        case 1: // Block
        default:
            painter.fillRect(cursorRect, Qt::white);
            break;
        }
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }

    if (m_visualBellActive) {
        painter.setCompositionMode(QPainter::CompositionMode_Difference);
        painter.fillRect(rect(), Qt::white);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }

    if (m_flowControlStopped) {
        QString msg = tr("Terminal stopped (Ctrl+S). Press Ctrl+Q to resume.");
        QFont msgFont = font();
        msgFont.setBold(true);
        painter.setFont(msgFont);
        QFontMetrics fmm(msgFont);
        QRect msgRect = fmm.boundingRect(msg).adjusted(-5, -2, 5, 2);
        msgRect.moveCenter(QPoint(width() / 2, msgRect.height() / 2 + 10));

        painter.fillRect(msgRect, Qt::yellow);
        painter.setPen(Qt::black);
        painter.drawText(msgRect, Qt::AlignCenter, msg);
    }
}

void KodoTerm::keyPressEvent(QKeyEvent *event) {
    VTermModifier mod = VTERM_MOD_NONE;
    if (event->modifiers() & Qt::ShiftModifier) {
        mod = (VTermModifier)(mod | VTERM_MOD_SHIFT);
    }
    if (event->modifiers() & Qt::ControlModifier) {
        mod = (VTermModifier)(mod | VTERM_MOD_CTRL);
    }
    if (event->modifiers() & Qt::AltModifier) {
        mod = (VTermModifier)(mod | VTERM_MOD_ALT);
    }

    int key = event->key();
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        vterm_keyboard_key(m_vterm, (VTermKey)(VTERM_KEY_FUNCTION(1 + key - Qt::Key_F1)), mod);
    } else {
        switch (key) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
            vterm_keyboard_key(m_vterm, VTERM_KEY_ENTER, mod);
            break;
        case Qt::Key_Backspace:
            vterm_keyboard_key(m_vterm, VTERM_KEY_BACKSPACE, mod);
            break;
        case Qt::Key_Tab:
            vterm_keyboard_key(m_vterm, VTERM_KEY_TAB, mod);
            break;
        case Qt::Key_Escape:
            vterm_keyboard_key(m_vterm, VTERM_KEY_ESCAPE, mod);
            break;
        case Qt::Key_Up:
            vterm_keyboard_key(m_vterm, VTERM_KEY_UP, mod);
            break;
        case Qt::Key_Down:
            vterm_keyboard_key(m_vterm, VTERM_KEY_DOWN, mod);
            break;
        case Qt::Key_Left:
            vterm_keyboard_key(m_vterm, VTERM_KEY_LEFT, mod);
            break;
        case Qt::Key_Right:
            vterm_keyboard_key(m_vterm, VTERM_KEY_RIGHT, mod);
            break;
        case Qt::Key_PageUp:
            if (event->modifiers() & Qt::ShiftModifier) {
                pageUp();
            } else {
                vterm_keyboard_key(m_vterm, VTERM_KEY_PAGEUP, mod);
            }
            break;
        case Qt::Key_PageDown:
            if (event->modifiers() & Qt::ShiftModifier) {
                pageDown();
            } else {
                vterm_keyboard_key(m_vterm, VTERM_KEY_PAGEDOWN, mod);
            }
            break;
        case Qt::Key_Home:
            if (event->modifiers() & Qt::ShiftModifier) {
                m_scrollBar->setValue(m_scrollBar->minimum());
            } else {
                vterm_keyboard_key(m_vterm, VTERM_KEY_HOME, mod);
            }
            break;
        case Qt::Key_End:
            if (event->modifiers() & Qt::ShiftModifier) {
                m_scrollBar->setValue(m_scrollBar->maximum());
            } else {
                vterm_keyboard_key(m_vterm, VTERM_KEY_END, mod);
            }
            break;
        case Qt::Key_Insert:
            vterm_keyboard_key(m_vterm, VTERM_KEY_INS, mod);
            break;
        case Qt::Key_Delete:
            vterm_keyboard_key(m_vterm, VTERM_KEY_DEL, mod);
            break;
        default:
            if (event->modifiers() & Qt::ControlModifier) {
                if (key == Qt::Key_Plus || key == Qt::Key_Equal) {
                    zoomIn();
                    return;
                } else if (key == Qt::Key_Minus) {
                    zoomOut();
                    return;
                } else if (key == Qt::Key_0) {
                    resetZoom();
                    return;
                }
            }

            if ((event->modifiers() & Qt::ControlModifier) &&
                (event->modifiers() & Qt::ShiftModifier)) {

                if (key == Qt::Key_C) {
                    copyToClipboard();
                    return;
                } else if (key == Qt::Key_V) {
                    pasteFromClipboard();
                    return;
                }
            }
            if ((mod & VTERM_MOD_CTRL) && key >= Qt::Key_A && key <= Qt::Key_Z) {
                if (key == Qt::Key_S) {
                    m_flowControlStopped = true;
                    update();
                } else if (key == Qt::Key_Q) {
                    m_flowControlStopped = false;
                    update();
                }
                int charCode = key - Qt::Key_A + 1;
                vterm_keyboard_unichar(m_vterm, charCode, VTERM_MOD_NONE);
            } else if (!event->text().isEmpty()) {
                for (const QChar &qc : event->text()) {
                    vterm_keyboard_unichar(m_vterm, qc.unicode(), mod);
                }
            }
            break;
        }
    }
}

bool KodoTerm::focusNextPrevChild(bool next) { return false; }

void KodoTerm::populateThemeMenu(QMenu *parentMenu, const QString &dirPath,
                                 TerminalTheme::ThemeFormat format) {
    QList<TerminalTheme::ThemeInfo> themes = TerminalTheme::builtInThemes();
    QList<TerminalTheme::ThemeInfo> filteredThemes;

    for (const auto &theme : themes) {
        if (theme.format == format) {
            filteredThemes.append(theme);
        }
    }

    auto addThemeAction = [this](QMenu *m, const TerminalTheme::ThemeInfo &info) {
        m->addAction(info.name, this, [this, info]() {
            if (info.format == TerminalTheme::ThemeFormat::Konsole) {
                setTheme(TerminalTheme::loadKonsoleTheme(info.path));
            } else {
                setTheme(TerminalTheme::loadWindowsTerminalTheme(info.path));
            }
        });
    };

    if (filteredThemes.size() < 26) {
        for (const auto &info : filteredThemes) {
            addThemeAction(parentMenu, info);
        }
    } else {
        QMap<QString, QMenu *> subMenus;
        for (const auto &info : filteredThemes) {
            QChar firstLetterChar = info.name.isEmpty() ? QChar('#') : info.name[0].toUpper();
            if (!firstLetterChar.isLetter()) {
                firstLetterChar = QChar('#');
            }

            QString firstLetter(firstLetterChar);
            if (!subMenus.contains(firstLetter)) {
                subMenus[firstLetter] = parentMenu->addMenu(firstLetter);
            }
            addThemeAction(subMenus[firstLetter], info);
        }
    }
}