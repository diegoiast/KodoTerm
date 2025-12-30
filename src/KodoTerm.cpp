// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "KodoTerm/KodoTerm.hpp"
#include "PtyProcess.h"

#include <QKeyEvent>
#include <QPainter>

#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vterm.h>

#include <QApplication>
#include <QClipboard>
#include <QMouseEvent>

static void output_callback(const char *s, size_t len, void *user) {
    int fd = *static_cast<int *>(user);
    if (fd >= 0) {
        ::write(fd, s, len);
    }
}

static void vterm_output_callback(const char *s, size_t len, void *user) {
    auto *pty = static_cast<PtyProcess *>(user);
    if (pty) {
        pty->write(QByteArray(s, (int)len));
    }
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
    m_scrollBar = new QScrollBar(Qt::Vertical, this);
    m_scrollBar->setRange(0, 0);
    connect(m_scrollBar, &QScrollBar::valueChanged, this, &KodoTerm::onScrollValueChanged);

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

    static VTermScreenCallbacks callbacks = {.damage = &KodoTerm::onDamage,
                                             .moverect = nullptr,
                                             .movecursor = &KodoTerm::onMoveCursor,
                                             .settermprop = nullptr,
                                             .bell = nullptr,
                                             .resize = nullptr,
                                             .sb_pushline = &KodoTerm::onSbPushLine,
                                             .sb_popline = &KodoTerm::onSbPopLine,
                                             .sb_clear = nullptr,
                                             .sb_pushline4 = nullptr};

    vterm_screen_set_callbacks(m_vtermScreen, &callbacks, this);
    vterm_screen_reset(m_vtermScreen, 1);

    setFocusPolicy(Qt::StrongFocus);
    setupPty();
}

KodoTerm::~KodoTerm() {
    if (m_vterm) {
        vterm_free(m_vterm);
    }
}

void KodoTerm::setupPty() {
    m_pty = PtyProcess::create(this);
    if (!m_pty) {
        qWarning() << "Failed to create PtyProcess backend";
        return;
    }
    connect(m_pty, &PtyProcess::readyRead, this, &KodoTerm::onPtyReadyRead);
    vterm_output_set_callback(m_vterm, vterm_output_callback, m_pty);

    QString program;
#ifdef Q_OS_WIN
    program = "powershell.exe"; // or cmd.exe
#else
    program = "/bin/bash";
#endif

    // Initial size
    QSize size(80, 25);
    if (!m_cellSize.isEmpty()) {
        size = QSize(width() / m_cellSize.width(), height() / m_cellSize.height());
        if (size.width() <= 0) {
            size.setWidth(80);
        }
        if (size.height() <= 0) {
            size.setHeight(25);
        }
    }

    m_pty->start(program, QStringList(), size);
}

void KodoTerm::onPtyReadyRead(const QByteArray &data) {
    if (!data.isEmpty()) {
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

int KodoTerm::pushScrollback(int cols, const VTermScreenCell *cells) {
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

    if ((int)m_scrollback.size() > m_maxScrollback) {
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

    m_scrollback.pop_back();
    m_scrollBar->setRange(0, (int)m_scrollback.size());
    return 1;
}

void KodoTerm::updateTerminalSize() {
    if (m_cellSize.isEmpty()) {
        return;
    }

    int rows = height() / m_cellSize.height();
    int cols = (width() - m_scrollBar->sizeHint().width()) / m_cellSize.width();
    if (rows <= 0) {
        rows = 1;
    }
    if (cols <= 0) {
        cols = 1;
    }
    m_scrollBar->setPageStep(rows);
    if (m_vterm) {
        vterm_set_size(m_vterm, rows, cols);
        if (m_vtermScreen) {
            vterm_screen_flush_damage(m_vtermScreen);
        }
    }

    if (m_pty) {
        m_pty->resize(
            QSize(cols, rows)); // Check if PtyProcess expects cols/rows or width/height pixels?
        // My interface says "resize(const QSize &size)".
        // PtyProcessUnix::resize uses it as height=rows, width=cols.
        // Let's stick to that convention: width=cols, height=rows.
    }
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

void KodoTerm::resizeEvent(QResizeEvent *event) {
    int sbWidth = m_scrollBar->sizeHint().width();
    m_scrollBar->setGeometry(width() - sbWidth, 0, sbWidth, height());
    updateTerminalSize();
    QWidget::resizeEvent(event);
}

void KodoTerm::wheelEvent(QWheelEvent *event) { m_scrollBar->event(event); }

void KodoTerm::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_selecting = true;
        m_selectionStart = mouseToPos(event->pos());
        m_selectionEnd = m_selectionStart;
        update();
    }
}

void KodoTerm::mouseMoveEvent(QMouseEvent *event) {
    if (m_selecting) {
        m_selectionEnd = mouseToPos(event->pos());
        update();
    }
}

void KodoTerm::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        m_selectionEnd = mouseToPos(event->pos());
        if (m_selectionStart.row == m_selectionEnd.row &&
            m_selectionStart.col == m_selectionEnd.col) {
            m_selectionStart = {-1, -1};
            m_selectionEnd = {-1, -1};
        } else if (m_copyOnSelect) {
            copyToClipboard();
        }
        update();
    }
}

VTermPos KodoTerm::mouseToPos(const QPoint &pos) const {
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
        int endCol = (r == end.row) ? end.col : 1000; // Arbitrary large numbe

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

void KodoTerm::drawCell(QPainter &painter, int row, int col, const VTermScreenCell &cell,
                        bool selected) {
    auto mapColor = [](const VTermColor &c, const VTermState *state) -> QColor {
        if (VTERM_COLOR_IS_RGB(&c)) {
            return QColor(c.rgb.red, c.rgb.green, c.rgb.blue);
        } else if (VTERM_COLOR_IS_INDEXED(&c)) {
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
    if (cell.attrs.reverse ^ selected) {
        std::swap(fg, bg);
    }

    QRect rect(col * m_cellSize.width(), row * m_cellSize.height(), m_cellSize.width(),
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
    painter.setFont(m_font);
    painter.fillRect(rect(), Qt::black);

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
            }
        } else {
            int vtermRow = absoluteRow - scrollbackLines;
            for (int col = 0; col < cols; ++col) {
                VTermScreenCell cell;
                vterm_screen_get_cell(m_vtermScreen, {vtermRow, col}, &cell);
                drawCell(painter, row, col, cell, isSelected(absoluteRow, col));
            }
        }
    }

    if (m_cursorVisible && currentScrollPos == scrollbackLines) {
        QRect cursorRect(m_cursorCol * m_cellSize.width(), m_cursorRow * m_cellSize.height(),
                         m_cellSize.width(), m_cellSize.height());
        painter.setCompositionMode(QPainter::CompositionMode_Difference);
        painter.fillRect(cursorRect, Qt::white);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
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
        default:
            if ((event->modifiers() & Qt::ControlModifier) &&
                (event->modifiers() & Qt::ShiftModifier)) {
                if (key == Qt::Key_C) {
                    copyToClipboard();
                    return;
                } else if (key == Qt::Key_V) {
                    QString text = QApplication::clipboard()->text();
                    if (!text.isEmpty()) {
                        m_pty->write(text.toUtf8());
                    }
                    return;
                }
            }

            if ((mod & VTERM_MOD_CTRL) && key >= Qt::Key_A && key <= Qt::Key_Z) {
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
