// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "KodoTerm/KodoTerm.hpp"

#include <QPainter>
#include <QKeyEvent>

#include <vterm.h>
#include <pty.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <cerrno>

static void output_callback(const char *s, size_t len, void *user) {
    int fd = *static_cast<int*>(user);
    if (fd >= 0) {
        ::write(fd, s, len);
    }
}

int KodoTerm::onDamage(VTermRect rect, void *user) {
    auto *widget = static_cast<KodoTerm*>(user);
    int w = widget->m_cellSize.width();
    int h = widget->m_cellSize.height();
    widget->update(rect.start_col * w, rect.start_row * h, 
                   (rect.end_col - rect.start_col) * w, 
                   (rect.end_row - rect.start_row) * h);
    return 1;
}

int KodoTerm::onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user) {
    auto *widget = static_cast<KodoTerm*>(user);
    widget->m_cursorRow = pos.row;
    widget->m_cursorCol = pos.col;
    widget->m_cursorVisible = visible;
    widget->update(); 
    return 1;
}

int KodoTerm::onPushLine(int cols, const VTermScreenCell *cells, void *user) {
    return 1; 
}

int KodoTerm::onPopLine(int cols, VTermScreenCell *cells, void *user) {
    return 1;
}

KodoTerm::KodoTerm(QWidget *parent) : QWidget(parent) {
    m_font = QFont("Monospace", 10);
    m_font.setStyleHint(QFont::Monospace);
    
    QFontMetrics fm(m_font);
    m_cellSize = QSize(fm.horizontalAdvance('W'), fm.height());

    if (m_cellSize.width() <= 0 || m_cellSize.height() <= 0) {
        m_cellSize = QSize(10, 20);
    }

    setFocusPolicy(Qt::StrongFocus);
    
    m_vterm = vterm_new(25, 80);
    if (!m_vterm) return;
    vterm_set_utf8(m_vterm, 1);
    
    m_vtermScreen = vterm_obtain_screen(m_vterm);
    if (!m_vtermScreen) return;
    vterm_screen_enable_altscreen(m_vtermScreen, 1);
    
    static VTermScreenCallbacks callbacks = {
        .damage = &KodoTerm::onDamage,
        .moverect = nullptr,
        .movecursor = &KodoTerm::onMoveCursor,
        .settermprop = nullptr,
        .bell = nullptr,
        .resize = nullptr,
        .sb_pushline = nullptr,
        .sb_popline = nullptr,
        .sb_clear = nullptr,
        .sb_pushline4 = nullptr
    };
    
    vterm_screen_set_callbacks(m_vtermScreen, &callbacks, this);
    vterm_screen_reset(m_vtermScreen, 1);
    
    setupPty();
}

KodoTerm::~KodoTerm() {
    if (m_vterm) vterm_free(m_vterm);
    if (m_masterFd >= 0) ::close(m_masterFd);
}

void KodoTerm::setupPty() {
    struct winsize ws;
    ws.ws_row = 25;
    ws.ws_col = 80;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    
    pid_t pid = forkpty(&m_masterFd, nullptr, nullptr, &ws);
    
    if (pid == -1) return;
    
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        execl("/bin/bash", "/bin/bash", nullptr);
        _exit(1);
    } else {
        m_childPid = pid;
        vterm_output_set_callback(m_vterm, output_callback, &m_masterFd);
        m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &KodoTerm::onPtyReadyRead);
    }
}

void KodoTerm::onPtyReadyRead() {
    char buffer[4096];
    ssize_t len = ::read(m_masterFd, buffer, sizeof(buffer));
    
    if (len > 0) {
        vterm_input_write(m_vterm, buffer, len);
        vterm_screen_flush_damage(m_vtermScreen);
    } else if (len < 0 && errno != EAGAIN) {
        m_notifier->setEnabled(false);
    } else if (len == 0) {
        m_notifier->setEnabled(false);
    }
}

void KodoTerm::updateTerminalSize() {
    if (m_cellSize.isEmpty()) return;
    
    int rows = height() / m_cellSize.height();
    int cols = width() / m_cellSize.width();
    
    if (rows <= 0) rows = 1;
    if (cols <= 0) cols = 1;
    
    if (m_vterm) {
        vterm_set_size(m_vterm, rows, cols);
        if (m_vtermScreen)
            vterm_screen_flush_damage(m_vtermScreen);
    }
    
    if (m_masterFd >= 0) {
        struct winsize ws;
        ws.ws_row = (unsigned short)rows;
        ws.ws_col = (unsigned short)cols;
        ws.ws_xpixel = (unsigned short)width();
        ws.ws_ypixel = (unsigned short)height();
        ioctl(m_masterFd, TIOCSWINSZ, &ws);
    }
}

void KodoTerm::resizeEvent(QResizeEvent *event) {
    updateTerminalSize();
    QWidget::resizeEvent(event);
}

void KodoTerm::drawCell(QPainter &painter, int row, int col, const VTermScreenCell &cell) {
    auto mapColor = [](const VTermColor &c, const VTermState *state) -> QColor {
        if (VTERM_COLOR_IS_RGB(&c)) {
            return QColor(c.rgb.red, c.rgb.green, c.rgb.blue);
        }
        else if (VTERM_COLOR_IS_INDEXED(&c)) {
            VTermColor rgb = c;
            vterm_state_convert_color_to_rgb(state, &rgb);
            return QColor(rgb.rgb.red, rgb.rgb.green, rgb.rgb.blue);
        }
        return Qt::white;
    };
    
    VTermState *state = vterm_obtain_state(m_vterm);
    QColor fg = Qt::white;
    QColor bg = Qt::black;

    if (!VTERM_COLOR_IS_DEFAULT_FG(&cell.fg)) {
        fg = mapColor(cell.fg, state);
    }

    if (!VTERM_COLOR_IS_DEFAULT_BG(&cell.bg)) {
        bg = mapColor(cell.bg, state);
    }
    
    if (cell.attrs.reverse) {
        std::swap(fg, bg);
    }

    QRect rect(col * m_cellSize.width(), row * m_cellSize.height(), 
               m_cellSize.width(), m_cellSize.height());
               
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
    painter.setFont(m_font);
    painter.fillRect(rect(), Qt::black);
    
    int rows, cols;
    vterm_get_size(m_vterm, &rows, &cols);
    
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            VTermScreenCell cell;
            vterm_screen_get_cell(m_vtermScreen, {row, col}, &cell);
            drawCell(painter, row, col, cell);
        }
    }
    
    if (m_cursorVisible) {
        QRect cursorRect(m_cursorCol * m_cellSize.width(), m_cursorRow * m_cellSize.height(),
                         m_cellSize.width(), m_cellSize.height());
        painter.setCompositionMode(QPainter::CompositionMode_Difference);
        painter.fillRect(cursorRect, Qt::white);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
}

void KodoTerm::keyPressEvent(QKeyEvent *event) {
    VTermModifier mod = VTERM_MOD_NONE;
    if (event->modifiers() & Qt::ShiftModifier) mod = (VTermModifier)(mod | VTERM_MOD_SHIFT);
    if (event->modifiers() & Qt::ControlModifier) mod = (VTermModifier)(mod | VTERM_MOD_CTRL);
    if (event->modifiers() & Qt::AltModifier) mod = (VTermModifier)(mod | VTERM_MOD_ALT);

    int key = event->key();
    
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        vterm_keyboard_key(m_vterm, (VTermKey)(VTERM_KEY_FUNCTION(1 + key - Qt::Key_F1)), mod);
    } else {
        switch (key) {
        case Qt::Key_Enter: 
        case Qt::Key_Return: vterm_keyboard_key(m_vterm, VTERM_KEY_ENTER, mod); break;
        case Qt::Key_Backspace: vterm_keyboard_key(m_vterm, VTERM_KEY_BACKSPACE, mod); break;
        case Qt::Key_Tab: vterm_keyboard_key(m_vterm, VTERM_KEY_TAB, mod); break;
        case Qt::Key_Escape: vterm_keyboard_key(m_vterm, VTERM_KEY_ESCAPE, mod); break;
        case Qt::Key_Up: vterm_keyboard_key(m_vterm, VTERM_KEY_UP, mod); break;
        case Qt::Key_Down: vterm_keyboard_key(m_vterm, VTERM_KEY_DOWN, mod); break;
        case Qt::Key_Left: vterm_keyboard_key(m_vterm, VTERM_KEY_LEFT, mod); break;
        case Qt::Key_Right: vterm_keyboard_key(m_vterm, VTERM_KEY_RIGHT, mod); break;
        default:
            if ((mod & VTERM_MOD_CTRL) && key >= Qt::Key_A && key <= Qt::Key_Z) {
                int charCode = key - Qt::Key_A + 1;
                vterm_keyboard_unichar(m_vterm, charCode, VTERM_MOD_NONE);
            }
            else if (!event->text().isEmpty()) {
                for(const QChar &qc : event->text()) {
                    vterm_keyboard_unichar(m_vterm, qc.unicode(), mod);
                }
            }
            break;
        }
    }
}

bool KodoTerm::focusNextPrevChild(bool next) {
    return false;
}