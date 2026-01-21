// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "KodoTerm/KodoTermSession.hpp"
#include "PtyProcess.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cstring>

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

KodoTermSession::KodoTermSession(QObject *parent) : QObject(parent) {
    m_environment = QProcessEnvironment::systemEnvironment();
    if (!m_environment.contains("TERM")) {
        m_environment.insert("TERM", "xterm-256color");
    }
    if (!m_environment.contains("COLORTERM")) {
        m_environment.insert("COLORTERM", "truecolor");
    }

    memset(&m_lastVTermFg, 0, sizeof(VTermColor));
    memset(&m_lastVTermBg, 0, sizeof(VTermColor));
    for (int i = 0; i < 256; ++i) {
        m_paletteCacheValid[i] = false;
    }

    m_vterm = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vterm, 1);

    m_vtermScreen = vterm_obtain_screen(m_vterm);
    vterm_screen_enable_altscreen(m_vtermScreen, 1);

    static VTermScreenCallbacks callbacks = {
        .damage = &KodoTermSession::onDamage,
        .moverect = &KodoTermSession::onMoveRect,
        .movecursor = &KodoTermSession::onMoveCursor,
        .settermprop = &KodoTermSession::onSetTermProp,
        .bell = &KodoTermSession::onBell,
        .resize = nullptr,
        .sb_pushline = &KodoTermSession::onSbPushLine,
        .sb_popline = &KodoTermSession::onSbPopLine,
    };
    vterm_screen_set_callbacks(m_vtermScreen, &callbacks, this);
    vterm_screen_reset(m_vtermScreen, 1);

    VTermState *state = vterm_obtain_state(m_vterm);
    static VTermStateFallbacks fallbacks = {.control = nullptr,
                                            .csi = nullptr,
                                            .osc = &KodoTermSession::onOsc,
                                            .dcs = nullptr,
                                            .apc = nullptr,
                                            .pm = nullptr,
                                            .sos = nullptr};
    vterm_state_set_unrecognised_fallbacks(state, &fallbacks, this);
}

KodoTermSession::~KodoTermSession() {
    if (m_pty) {
        m_pty->kill();
    }
    if (m_vterm) {
        vterm_free(m_vterm);
    }
}

void KodoTermSession::setConfig(const KodoTermConfig &config) {
    m_config = config;
    setTheme(m_config.theme);
}

void KodoTermSession::setTheme(const TerminalTheme &theme) {
    m_config.theme = theme;
    if (!m_vterm) {
        return;
    }
    VTermState *state = vterm_obtain_state(m_vterm);
    VTermColor fg = toVTermColor(theme.foreground), bg = toVTermColor(theme.background);
    vterm_state_set_default_colors(state, &fg, &bg);
    for (int i = 0; i < 16; ++i) {
        VTermColor c = toVTermColor(theme.palette[i]);
        vterm_state_set_palette_color(state, i, &c);
    }
    for (int i = 0; i < 256; ++i) {
        m_paletteCacheValid[i] = false;
    }
    emit contentChanged(QRect(0, 0, m_cols, m_rows));
}

bool KodoTermSession::start(bool reset) {
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
        QDir logDir(m_config.logDirectory);
        if (!logDir.exists()) {
            logDir.mkpath(".");
        }
        QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
        m_logFile.setFileName(logDir.filePath(QString("kodoterm_%1.log").arg(ts)));
        if (m_logFile.open(QIODevice::WriteOnly)) {
            QString h = QString("-- KodoTerm Session Log --\nProgram: %1\nLOG_START_MARKER\n")
                            .arg(m_program);
            m_logFile.write(h.toUtf8());
            m_logFile.flush();
        }
    }

    bool ok = m_pty->start(QSize(m_cols, m_rows));
    if (ok && !m_pendingLogReplay.isEmpty()) {
        QTimer::singleShot(100, this, &KodoTermSession::processLogReplay);
    }
    return ok;
}

void KodoTermSession::kill() {
    if (m_pty) {
        m_pty->kill();
    }
}

bool KodoTermSession::isRunning() const { return m_pty != nullptr; }

QString KodoTermSession::foregroundProcessName() const {
    return m_pty ? m_pty->foregroundProcessName() : QString();
}

bool KodoTermSession::isRoot() const { return m_pty && m_pty->isRoot(); }

void KodoTermSession::setupPty() {
    if (m_pty) {
        return;
    }
    m_pty = PtyProcess::create(this);
    if (!m_pty) {
        return;
    }

    connect(m_pty, &PtyProcess::readyRead, this, &KodoTermSession::onPtyReadyRead);
    connect(m_pty, &PtyProcess::finished, this, &KodoTermSession::finished);

    vterm_output_set_callback(m_vterm, vterm_output_callback, m_pty);
}

void KodoTermSession::onPtyReadyRead(const QByteArray &data) {
    if (!data.isEmpty()) {
        if (m_logFile.isOpen()) {
            m_logFile.write(data);
            m_logFile.flush();
        }
        vterm_input_write(m_vterm, data.constData(), data.size());
        flushTerminal();
    }
}

void KodoTermSession::flushTerminal() {
    if (m_vtermScreen) {
        vterm_screen_flush_damage(m_vtermScreen);
    }
}

void KodoTermSession::resizeTerminal(int rows, int cols) {
    if (rows <= 0) {
        rows = 1;
    }
    if (cols <= 0) {
        cols = 1;
    }
    if (m_rows == rows && m_cols == cols) {
        return;
    }

    m_rows = rows;
    m_cols = cols;

    if (m_vterm) {
        vterm_set_size(m_vterm, rows, cols);
        vterm_screen_flush_damage(m_vtermScreen);
    }
    if (m_pty) {
        m_pty->resize(QSize(cols, rows));
    }
}

void KodoTermSession::resetTerminal() {
    vterm_screen_reset(m_vtermScreen, 1);
    m_altScreen = false;
    m_mouseMode = 0;
    m_cursorVisible = true;
    m_cursorBlink = false;
    m_cursorShape = 1;
    clearScrollback();
    emit contentChanged(QRect(0, 0, m_cols, m_rows));
}

void KodoTermSession::clearScrollback() {
    m_scrollback.clear();
    emit scrollbackChanged();
}

void KodoTermSession::setRestoreLog(const QString &path) { m_pendingLogReplay = path; }

QString KodoTermSession::logPath() const { return m_logFile.fileName(); }

void KodoTermSession::processLogReplay() {
    if (m_pendingLogReplay.isEmpty() && !m_replayFile) {
        return;
    }

    if (!m_replayFile) {
        m_restoring = true;
        m_suppressSignals = true;
        resetTerminal();
        m_replayFile = new QFile(m_pendingLogReplay);
        m_pendingLogReplay.clear();
        if (!m_replayFile->open(QIODevice::ReadOnly)) {
            delete m_replayFile;
            m_replayFile = nullptr;
            m_restoring = false;
            m_suppressSignals = false;
            return;
        }
        // Skip header if present
        QByteArray header;
        while (!m_replayFile->atEnd()) {
            char c;
            if (m_replayFile->getChar(&c)) {
                header.append(c);
                if (c == '\n' && header.endsWith("LOG_START_MARKER\n")) {
                    break;
                }
                if (header.size() > 1024) {
                    break;
                }
            } else {
                break;
            }
        }
    }

    if (m_replayFile) {
        QByteArray chunk = m_replayFile->read(65536);
        if (!chunk.isEmpty()) {
            vterm_input_write(m_vterm, chunk.constData(), chunk.size());
            flushTerminal();
            QTimer::singleShot(0, this, &KodoTermSession::processLogReplay);
        } else {
            m_replayFile->close();
            delete m_replayFile;
            m_replayFile = nullptr;
            m_restoring = false;
            m_suppressSignals = false;
            emit scrollbackChanged();
            emit contentChanged(QRect(0, 0, m_cols, m_rows));
        }
    }
}

void KodoTermSession::sendKey(int key, Qt::KeyboardModifiers modifiers, const QString &text) {
    if (!m_vterm) {
        return;
    }

    VTermModifier m = VTERM_MOD_NONE;
    if (modifiers & Qt::ShiftModifier) {
        m = (VTermModifier)(m | VTERM_MOD_SHIFT);
    }
    if (modifiers & Qt::ControlModifier) {
        m = (VTermModifier)(m | VTERM_MOD_CTRL);
    }
    if (modifiers & Qt::AltModifier) {
        m = (VTermModifier)(m | VTERM_MOD_ALT);
    }

    bool handled = true;
    switch (key) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
        vterm_keyboard_key(m_vterm, VTERM_KEY_ENTER, m);
        break;
    case Qt::Key_Backspace:
        vterm_keyboard_key(m_vterm, VTERM_KEY_BACKSPACE, m);
        break;
    case Qt::Key_Tab:
        vterm_keyboard_key(m_vterm, VTERM_KEY_TAB, m);
        break;
    case Qt::Key_Escape:
        vterm_keyboard_key(m_vterm, VTERM_KEY_ESCAPE, m);
        break;
    case Qt::Key_Up:
        vterm_keyboard_key(m_vterm, VTERM_KEY_UP, m);
        break;
    case Qt::Key_Down:
        vterm_keyboard_key(m_vterm, VTERM_KEY_DOWN, m);
        break;
    case Qt::Key_Left:
        vterm_keyboard_key(m_vterm, VTERM_KEY_LEFT, m);
        break;
    case Qt::Key_Right:
        vterm_keyboard_key(m_vterm, VTERM_KEY_RIGHT, m);
        break;
    case Qt::Key_PageUp:
        vterm_keyboard_key(m_vterm, VTERM_KEY_PAGEUP, m);
        break;
    case Qt::Key_PageDown:
        vterm_keyboard_key(m_vterm, VTERM_KEY_PAGEDOWN, m);
        break;
    case Qt::Key_Home:
        vterm_keyboard_key(m_vterm, VTERM_KEY_HOME, m);
        break;
    case Qt::Key_End:
        vterm_keyboard_key(m_vterm, VTERM_KEY_END, m);
        break;
    case Qt::Key_Insert:
        vterm_keyboard_key(m_vterm, VTERM_KEY_INS, m);
        break;
    case Qt::Key_Delete:
        vterm_keyboard_key(m_vterm, VTERM_KEY_DEL, m);
        break;
    default:
        if ((m & VTERM_MOD_CTRL) && key >= Qt::Key_A && key <= Qt::Key_Z) {
            vterm_keyboard_unichar(m_vterm, key - Qt::Key_A + 1, VTERM_MOD_NONE);
        } else if (!text.isEmpty()) {
            for (const QChar &qc : text) {
                vterm_keyboard_unichar(m_vterm, qc.unicode(), m);
            }
        } else {
            handled = false;
        }
        break;
    }
    if (handled) {
        flushTerminal();
    }
}

void KodoTermSession::sendMouse(int button, int row, int col, Qt::KeyboardModifiers modifiers,
                                bool pressed) {
    if (!m_vterm || m_mouseMode == 0) {
        return;
    }

    VTermModifier m = VTERM_MOD_NONE;
    if (modifiers & Qt::ShiftModifier) {
        m = (VTermModifier)(m | VTERM_MOD_SHIFT);
    }
    if (modifiers & Qt::ControlModifier) {
        m = (VTermModifier)(m | VTERM_MOD_CTRL);
    }
    if (modifiers & Qt::AltModifier) {
        m = (VTermModifier)(m | VTERM_MOD_ALT);
    }

    if (button >= 1 && button <= 3) {
        vterm_mouse_move(m_vterm, row, col, m);
        vterm_mouse_button(m_vterm, button, pressed, m);
        flushTerminal();
    }
}

void KodoTermSession::sendMouseMove(int row, int col, Qt::KeyboardModifiers modifiers) {
    if (!m_vterm || m_mouseMode == 0) {
        return;
    }

    VTermModifier m = VTERM_MOD_NONE;
    if (modifiers & Qt::ShiftModifier) {
        m = (VTermModifier)(m | VTERM_MOD_SHIFT);
    }
    if (modifiers & Qt::ControlModifier) {
        m = (VTermModifier)(m | VTERM_MOD_CTRL);
    }
    if (modifiers & Qt::AltModifier) {
        m = (VTermModifier)(m | VTERM_MOD_ALT);
    }

    vterm_mouse_move(m_vterm, row, col, m);
    flushTerminal();
}

void KodoTermSession::sendText(const QString &text) {
    if (m_pty) {
        m_pty->write(text.toUtf8());
    }
}

bool KodoTermSession::getCell(int row, int col, SavedCell &cell) const {
    if (!m_vtermScreen) {
        return false;
    }
    int sb = (int)m_scrollback.size();
    if (row < sb) {
        if (row < 0) {
            return false;
        }
        const SavedLine &l = m_scrollback[row];
        if (col < (int)l.size()) {
            cell = l[col];
            return true;
        }
        memset(&cell, 0, sizeof(cell));
        cell.width = 1;
        return true;
    } else {
        int r = row - sb;
        if (r >= 0 && r < m_rows) {
            VTermScreenCell vcell;
            vterm_screen_get_cell(m_vtermScreen, {r, col}, &vcell);
            memcpy(cell.chars, vcell.chars, sizeof(cell.chars));
            cell.attrs = vcell.attrs;
            cell.fg = vcell.fg;
            cell.bg = vcell.bg;
            cell.width = vcell.width;
            return true;
        }
    }
    return false;
}

QColor KodoTermSession::mapColor(const VTermColor &c) const {
    if (VTERM_COLOR_IS_DEFAULT_FG(&c)) {
        return m_config.theme.foreground;
    }
    if (VTERM_COLOR_IS_DEFAULT_BG(&c)) {
        return m_config.theme.background;
    }

    if (VTERM_COLOR_IS_RGB(&c)) {
        if (c.rgb.red == m_lastVTermFg.rgb.red && c.rgb.green == m_lastVTermFg.rgb.green &&
            c.rgb.blue == m_lastVTermFg.rgb.blue) {
            return m_lastFg;
        }
        if (c.rgb.red == m_lastVTermBg.rgb.red && c.rgb.green == m_lastVTermBg.rgb.green &&
            c.rgb.blue == m_lastVTermBg.rgb.blue) {
            return m_lastBg;
        }
        QColor col(c.rgb.red, c.rgb.green, c.rgb.blue);
        m_lastVTermBg = m_lastVTermFg;
        m_lastBg = m_lastFg;
        m_lastVTermFg = c;
        m_lastFg = col;
        return col;
    } else if (VTERM_COLOR_IS_INDEXED(&c)) {
        uint8_t i = c.indexed.idx;
        if (!m_paletteCacheValid[i]) {
            VTermColor rgb = c;
            vterm_state_convert_color_to_rgb(vterm_obtain_state(m_vterm), &rgb);
            m_paletteCache[i] = QColor(rgb.rgb.red, rgb.rgb.green, rgb.rgb.blue);
            m_paletteCacheValid[i] = true;
        }
        return m_paletteCache[i];
    }
    return Qt::white;
}

// VTerm Callbacks

int KodoTermSession::onDamage(VTermRect r, void *u) {
    auto *s = static_cast<KodoTermSession *>(u);
    if (s->m_suppressSignals) {
        return 1;
    }
    emit s->contentChanged(
        QRect(r.start_col, r.start_row, r.end_col - r.start_col, r.end_row - r.start_row));
    return 1;
}

int KodoTermSession::onMoveRect(VTermRect d, VTermRect s, void *u) {
    auto *self = static_cast<KodoTermSession *>(u);
    if (self->m_suppressSignals) {
        return 1;
    }
    emit self->rectMoved(
        QRect(d.start_col, d.start_row, d.end_col - d.start_col, d.end_row - d.start_row),
        QRect(s.start_col, s.start_row, s.end_col - s.start_col, s.end_row - s.start_row));
    return 1;
}

int KodoTermSession::onMoveCursor(VTermPos p, VTermPos op, int v, void *u) {
    auto *s = static_cast<KodoTermSession *>(u);
    s->m_cursorRow = p.row;
    s->m_cursorCol = p.col;
    s->m_cursorVisible = v;
    if (s->m_suppressSignals) {
        return 1;
    }
    emit s->cursorMoved(p.row, p.col);
    return 1;
}

int KodoTermSession::onSetTermProp(VTermProp p, VTermValue *v, void *u) {
    auto *s = static_cast<KodoTermSession *>(u);
    switch (p) {
    case VTERM_PROP_CURSORVISIBLE:
        s->m_cursorVisible = v->boolean;
        if (!s->m_suppressSignals) {
            emit s->cursorVisibilityChanged(v->boolean);
        }
        break;
    case VTERM_PROP_CURSORBLINK:
        s->m_cursorBlink = v->boolean;
        break;
    case VTERM_PROP_CURSORSHAPE:
        s->m_cursorShape = v->number;
        break;
    case VTERM_PROP_ALTSCREEN:
        s->m_altScreen = v->boolean;
        break;
    case VTERM_PROP_TITLE:
        if (!s->m_suppressSignals) {
            emit s->titleChanged(QString::fromUtf8(v->string.str, (int)v->string.len));
        }
        break;
    case VTERM_PROP_MOUSE:
        s->m_mouseMode = v->number;
        break;
    default:
        break;
    }
    if (!s->m_suppressSignals) {
        emit s->propChanged(p, v);
    }
    return 1;
}

int KodoTermSession::onBell(void *u) {
    auto *s = static_cast<KodoTermSession *>(u);
    if (s->m_suppressSignals) {
        return 1;
    }
    emit s->bell();
    return 1;
}

int KodoTermSession::onSbPushLine(int cols, const VTermScreenCell *cells, void *user) {
    return static_cast<KodoTermSession *>(user)->pushScrollback(cols, cells);
}

int KodoTermSession::onSbPopLine(int cols, VTermScreenCell *cells, void *user) {
    return static_cast<KodoTermSession *>(user)->popScrollback(cols, cells);
}

int KodoTermSession::pushScrollback(int cols, const VTermScreenCell *cells) {
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
    if (!m_suppressSignals) {
        emit scrollbackChanged();
    }
    return 1;
}

int KodoTermSession::popScrollback(int cols, VTermScreenCell *cells) {
    if (m_scrollback.empty()) {
        return 0;
    }
    const SavedLine &line = m_scrollback.back();
    int n = std::min(cols, (int)line.size());
    for (int i = 0; i < n; ++i) {
        memcpy(cells[i].chars, line[i].chars, sizeof(cells[i].chars));
        cells[i].attrs = line[i].attrs;
        cells[i].fg = line[i].fg;
        cells[i].bg = line[i].bg;
        cells[i].width = line[i].width;
    }
    for (int i = n; i < cols; ++i) {
        memset(&cells[i], 0, sizeof(VTermScreenCell));
        cells[i].width = 1;
    }
    m_scrollback.pop_back();
    if (!m_suppressSignals) {
        emit scrollbackChanged();
    }
    return 1;
}

int KodoTermSession::onOsc(int command, VTermStringFragment frag, void *user) {
    auto *s = static_cast<KodoTermSession *>(user);
    if (frag.initial) {
        s->m_oscBuffer.clear();
    }
    s->m_oscBuffer.append(frag.str, frag.len);
    if (frag.final && command == 7) {
        QString str = QString::fromUtf8(s->m_oscBuffer).trimmed();
        // Simple file:// parsing
        if (str.startsWith("file://")) {
            QUrl u(str);
            if (u.isLocalFile()) {
                s->m_cwd = u.toLocalFile();
            } else {
                s->m_cwd = u.path();
            }
            if (!s->m_suppressSignals) {
                emit s->cwdChanged(s->m_cwd);
            }
        } else if (!str.isEmpty() && str.startsWith("/")) {
            s->m_cwd = str;
            if (!s->m_suppressSignals) {
                emit s->cwdChanged(s->m_cwd);
            }
        }
    }
    return 1;
}

void KodoTermSession::setSelection(const QPoint &start, const QPoint &end) {
    m_selectionStart = start;
    m_selectionEnd = end;
    emit contentChanged(QRect(0, 0, m_cols, m_rows));
}

void KodoTermSession::clearSelection() {
    m_selectionStart = QPoint(-1, -1);
    m_selectionEnd = QPoint(-1, -1);
    emit contentChanged(QRect(0, 0, m_cols, m_rows));
}

void KodoTermSession::selectAll() {
    m_selectionStart = QPoint(0, 0);
    m_selectionEnd = QPoint(m_cols - 1, (int)m_scrollback.size() + m_rows - 1);
    emit contentChanged(QRect(0, 0, m_cols, m_rows));
}

bool KodoTermSession::isSelected(int row, int col) const {
    if (m_selectionStart.x() == -1 || m_selectionEnd.x() == -1) {
        return false;
    }

    QPoint s = m_selectionStart, e = m_selectionEnd;
    if (s.y() > e.y() || (s.y() == e.y() && s.x() > e.x())) {
        std::swap(s, e);
    }

    if (row < s.y() || row > e.y()) {
        return false;
    }
    if (row == s.y() && row == e.y()) {
        return col >= s.x() && col <= e.x();
    }
    if (row == s.y()) {
        return col >= s.x();
    }
    if (row == e.y()) {
        return col <= e.x();
    }
    return true;
}

QString KodoTermSession::selectedText() const {
    if (m_selectionStart.x() == -1 || m_selectionEnd.x() == -1) {
        return "";
    }
    QPoint s = m_selectionStart, e = m_selectionEnd;
    if (s.y() > e.y() || (s.y() == e.y() && s.x() > e.x())) {
        std::swap(s, e);
    }

    QString t;
    for (int r = s.y(); r <= e.y(); ++r) {
        int startCol = (r == s.y()) ? s.x() : 0;
        int endCol = (r == e.y()) ? e.x() : m_cols - 1;

        SavedCell cell;
        for (int c = startCol; c <= endCol; ++c) {
            if (getCell(r, c, cell)) {
                int n = 0;
                while (n < VTERM_MAX_CHARS_PER_CELL && cell.chars[n]) {
                    t.append(QChar::fromUcs4(cell.chars[n]));
                    n++;
                }
                if (cell.width > 1) {
                    c += (cell.width - 1);
                }
            }
        }
        if (r < e.y()) {
            t.append('\n');
        }
    }
    return t;
}