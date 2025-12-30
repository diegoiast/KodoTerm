// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "KodoTerm/KodoTerm.hpp"
#include "PtyProcess.h"

#include <vterm.h>

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
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

TerminalTheme TerminalTheme::loadKonsoleTheme(const QString &path) {

    TerminalTheme theme;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return theme;
    }

    theme.name = QFileInfo(path).baseName();

    auto parseColor = [](const QString &s) -> QColor {
        QStringList parts = s.split(',');
        if (parts.size() >= 3) {
            return QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
        }
        return QColor();
    };

    QMap<QString, QMap<QString, QString>> sections;
    QString currentSection;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(';')) {
            continue;
        }
        if (line.startsWith('[') && line.endsWith(']')) {
            currentSection = line.mid(1, line.length() - 2);
        } else if (!currentSection.isEmpty()) {
            int eq = line.indexOf('=');
            if (eq != -1) {
                QString key = line.left(eq).trimmed();
                QString value = line.mid(eq + 1).trimmed();
                sections[currentSection][key] = value;
            }
        }
    }

    if (sections.contains("General") && sections["General"].contains("Description")) {
        theme.name = sections["General"]["Description"];
    }

    theme.foreground = parseColor(sections["Foreground"]["Color"]);
    if (!theme.foreground.isValid()) {
        theme.foreground = Qt::white;
    }

    theme.background = parseColor(sections["Background"]["Color"]);
    if (!theme.background.isValid()) {
        theme.background = Qt::black;
    }

    for (int i = 0; i < 16; ++i) {
        QString section = QString("Color%1%2").arg(i % 8).arg(i >= 8 ? "Intense" : "");
        theme.palette[i] = parseColor(sections[section]["Color"]);
        if (!theme.palette[i].isValid()) {
            theme.palette[i] = (i < 8) ? Qt::black : Qt::white;
        }
    }

    return theme;
}

TerminalTheme TerminalTheme::loadWindowsTerminalTheme(const QString &path) {
    TerminalTheme theme;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return theme;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();

    theme.name = obj.value("name").toString();
    theme.foreground = QColor(obj.value("foreground").toString());
    theme.background = QColor(obj.value("background").toString());

    QStringList keys = {"black",       "red",          "green",       "yellow",
                        "blue",        "purple",       "cyan",        "white",
                        "brightBlack", "brightRed",    "brightGreen", "brightYellow",
                        "brightBlue",  "brightPurple", "brightCyan",  "brightWhite"};

    for (int i = 0; i < 16; ++i) {
        theme.palette[i] = QColor(obj.value(keys[i]).toString());
    }

    return theme;
}

QList<TerminalTheme::ThemeInfo> TerminalTheme::builtInThemes() {
    Q_INIT_RESOURCE(themes);
    QList<ThemeInfo> themes;
    QDirIterator it(":/themes", QStringList() << "*.colorscheme" << "*.json", QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ThemeInfo info;
        info.path = it.filePath();
        if (info.path.endsWith(".colorscheme")) {
            info.format = ThemeFormat::Konsole;
            QSettings settings(info.path, QSettings::IniFormat);
            info.name = settings.value("General/Description", it.fileName()).toString();
        } else {
            info.format = ThemeFormat::WindowsTerminal;
            QFile file(info.path);
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                info.name = doc.object().value("name").toString();
            }
            if (info.name.isEmpty()) {
                info.name = it.fileName();
            }
        }
        themes.append(info);
    }
    std::sort(themes.begin(), themes.end(), [](const ThemeInfo &a, const ThemeInfo &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return themes;
}

void KodoTerm::setTheme(const TerminalTheme &theme) {
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
                if (path.isEmpty()) {
                    path = qurl.path();
                }
                // Handle potential trailing characters from malformed sequences
                if (path.endsWith(';') || path.endsWith('.')) {
                    path.chop(1);
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
    QFontMetrics fm(m_font);
    m_cellSize = QSize(fm.horizontalAdvance('W'), fm.height());
    if (m_cellSize.width() <= 0 || m_cellSize.height() <= 0) {
        m_cellSize = QSize(10, 20);
    }

    if (m_cellSize.isEmpty()) {
        return;
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
    m_scrollBar->setPageStep(rows);
    if (m_vterm) {
        vterm_set_size(m_vterm, rows, cols);
        if (m_vtermScreen) {
            vterm_screen_flush_damage(m_vtermScreen);
        }
    }
    if (m_pty) {
        // Check if PtyProcess expects cols/rows or width/height pixels?
        m_pty->resize(QSize(cols, rows));
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
    default:
        break;
    }
    widget->update();
    return 1;
}

int KodoTerm::onBell(void *user) {
    auto *widget = static_cast<KodoTerm *>(user);
    if (widget->m_audibleBell) {
        QApplication::beep();
    }
    if (widget->m_visualBell) {
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
    if (m_mouseWheelZoom && (event->modifiers() & Qt::ControlModifier)) {
        if (event->angleDelta().y() > 0) {
            zoomIn();
        } else if (event->angleDelta().y() < 0) {
            zoomOut();
        }
        return;
    }
    m_scrollBar->event(event);
}

void KodoTerm::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_selecting = true;
        m_selectionStart = mouseToPos(event->pos());
        m_selectionEnd = m_selectionStart;
        update();
    } else if (event->button() == Qt::MiddleButton && m_pasteOnMiddleClick) {
        pasteFromClipboard();
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

void KodoTerm::contextMenuEvent(QContextMenuEvent *event) {
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
    populateThemeMenu(konsoleMenu, ":/themes/konsole", TerminalTheme::ThemeFormat::Konsole);
    populateThemeMenu(wtMenu, ":/themes/windowsterminal",
                      TerminalTheme::ThemeFormat::WindowsTerminal);
    emit contextMenuRequested(menu, event->globalPos());
    menu->exec(event->globalPos());
    delete menu;
}

void KodoTerm::zoomIn() {
    qreal size = m_font.pointSizeF();
    if (size <= 0) {
        size = m_font.pointSize();
    }
    m_font.setPointSizeF(size + 1.0);
    updateTerminalSize();
    update();
}

void KodoTerm::zoomOut() {
    qreal size = m_font.pointSizeF();
    if (size <= 0) {
        size = m_font.pointSize();
    }
    if (size > 1.0) {
        m_font.setPointSizeF(size - 1.0);
        updateTerminalSize();
        update();
    }
}

void KodoTerm::resetZoom() {
    m_font.setPointSize(10);
    updateTerminalSize();
    update();
}

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
    QList<TerminalTheme::ThemeInfo> themes;
    QStringList filters;
    if (format == TerminalTheme::ThemeFormat::Konsole) {
        filters << "*.colorscheme";
    } else {
        filters << "*.json";
    }

    QDirIterator it(dirPath, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        TerminalTheme::ThemeInfo info;
        info.path = it.filePath();
        info.format = format;
        if (format == TerminalTheme::ThemeFormat::Konsole) {
            QFile file(info.path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                while (!in.atEnd()) {
                    QString line = in.readLine().trimmed();
                    if (line.startsWith("Description=", Qt::CaseInsensitive)) {
                        info.name = line.mid(12).trimmed();
                        break;
                    }
                }
            }
            if (info.name.isEmpty()) {
                info.name = QFileInfo(info.path).baseName();
            }
        } else {
            QFile file(info.path);
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                info.name = doc.object().value("name").toString();
            }
            if (info.name.isEmpty()) {
                info.name = QFileInfo(info.path).baseName();
            }
        }
        themes.append(info);
    }

    std::sort(themes.begin(), themes.end(), [](const auto &a, const auto &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    auto addThemeAction = [this](QMenu *m, const TerminalTheme::ThemeInfo &info) {
        m->addAction(info.name, this, [this, info]() {
            if (info.format == TerminalTheme::ThemeFormat::Konsole) {
                setTheme(TerminalTheme::loadKonsoleTheme(info.path));
            } else {
                setTheme(TerminalTheme::loadWindowsTerminalTheme(info.path));
            }
        });
    };

    if (themes.size() < 26) {
        for (const auto &info : themes) {
            addThemeAction(parentMenu, info);
        }
    } else {
        QMap<QString, QMenu *> subMenus;
        for (const auto &info : themes) {
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
