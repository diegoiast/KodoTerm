// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#pragma once

#include <QColor>
#include <QFont>
#include <QMenu>
#include <QScrollBar>
#include <QSocketNotifier>
#include <QTimer>
#include <QWidget>
#include <deque>
#include <vector>
#include <vterm.h>

class PtyProcess;

struct TerminalTheme {
    QString name;
    QColor foreground;
    QColor background;
    QColor palette[16];

    enum class ThemeFormat { Konsole, WindowsTerminal };

    struct ThemeInfo {
        QString name;
        QString path;
        ThemeFormat format;
    };

    static TerminalTheme loadKonsoleTheme(const QString &path);
    static TerminalTheme loadWindowsTerminalTheme(const QString &path);
    static QList<ThemeInfo> builtInThemes();
};

class KodoTerm : public QWidget {
    Q_OBJECT

  public:
    explicit KodoTerm(QWidget *parent = nullptr);
    ~KodoTerm();

    void setTheme(const TerminalTheme &theme);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool focusNextPrevChild(bool next) override; // To capture Tab

  signals:
    void contextMenuRequested(QMenu *menu, const QPoint &pos);
    void cwdChanged(const QString &cwd);

  public slots:
    void onPtyReadyRead(const QByteArray &data);
    void onScrollValueChanged(int value);
    void scrollUp(int lines = 1);
    void scrollDown(int lines = 1);
    void pageUp();
    void pageDown();
    void copyToClipboard();
    void pasteFromClipboard();
    void selectAll();
    void clearScrollback();
    void resetTerminal();
    void openFileBrowser();

    void zoomIn();
    void zoomOut();
    void resetZoom();

    const QString &cwd() const { return m_cwd; }

    bool copyOnSelect() const { return m_copyOnSelect; }
    void setCopyOnSelect(bool enable) { m_copyOnSelect = enable; }

    bool pasteOnMiddleClick() const { return m_pasteOnMiddleClick; }
    void setPasteOnMiddleClick(bool enable) { m_pasteOnMiddleClick = enable; }
    bool mouseWheelZoom() const { return m_mouseWheelZoom; }
    void setMouseWheelZoom(bool enable) { m_mouseWheelZoom = enable; }
    bool visualBell() const { return m_visualBell; }
    void setVisualBell(bool enable) { m_visualBell = enable; }
    bool audibleBell() const { return m_audibleBell; }
    void setAudibleBell(bool enable) { m_audibleBell = enable; }

  private:
    void setupPty();
    void updateTerminalSize();
    void populateThemeMenu(QMenu *parentMenu, const QString &dirPath,
                           TerminalTheme::ThemeFormat format);
    void drawCell(QPainter &painter, int row, int col, const VTermScreenCell &cell, bool selected);
    QColor mapColor(const VTermColor &c, const VTermState *state) const;
    QString getTextRange(VTermPos start, VTermPos end);
    bool isSelected(int row, int col) const;
    VTermPos mouseToPos(const QPoint &pos) const;

    // VTerm callbacks
    static int onDamage(VTermRect rect, void *user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int onSetTermProp(VTermProp prop, VTermValue *val, void *user);
    static int onBell(void *user);
    static int onSbPushLine(int cols, const VTermScreenCell *cells, void *user);
    static int onSbPopLine(int cols, VTermScreenCell *cells, void *user);
    static int onOsc(int command, VTermStringFragment frag, void *user);

    int pushScrollback(int cols, const VTermScreenCell *cells);
    int popScrollback(int cols, VTermScreenCell *cells);

    struct SavedCell {
        uint32_t chars[VTERM_MAX_CHARS_PER_CELL];
        VTermScreenCellAttrs attrs;
        VTermColor fg, bg;
        int width;
    };
    using SavedLine = std::vector<SavedCell>;

    PtyProcess *m_pty = nullptr;
    VTerm *m_vterm = nullptr;
    VTermScreen *m_vtermScreen = nullptr;

    QSocketNotifier *m_notifier = nullptr;
    QFont m_font;
    QSize m_cellSize;
    int m_cursorRow = 0;
    int m_cursorCol = 0;
    bool m_cursorVisible = true;
    bool m_cursorBlink = false;
    int m_cursorShape = 1; // VTERM_PROP_CURSORSHAPE_BLOCK
    bool m_cursorBlinkState = true;
    bool m_altScreen = false;
    bool m_flowControlStopped = false;
    int m_mouseMode = 0; // VTERM_PROP_MOUSE_NONE
    QTimer *m_cursorBlinkTimer = nullptr;

    QScrollBar *m_scrollBar = nullptr;
    std::deque<SavedLine> m_scrollback;
    int m_maxScrollback = 1000;

    bool m_selecting = false;
    VTermPos m_selectionStart = {-1, -1};
    VTermPos m_selectionEnd = {-1, -1};
#ifdef Q_OS_WIN
    bool m_copyOnSelect = false;
#else
    bool m_copyOnSelect = true;
#endif
    bool m_pasteOnMiddleClick = true;
    bool m_mouseWheelZoom = true;
    bool m_visualBell = true;
    bool m_audibleBell = true;
    bool m_visualBellActive = false;
    QString m_cwd;
    QByteArray m_oscBuffer;
};
