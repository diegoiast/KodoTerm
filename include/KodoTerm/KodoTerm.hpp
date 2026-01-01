// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#pragma once

#include <QColor>
#include <QFont>
#include <QMenu>
#include <QProcessEnvironment>
#include <QScrollBar>
#include <QSocketNotifier>
#include <QTimer>
#include <QWidget>
#include <deque>
#include <vector>
#include <vterm.h>

#include "KodoTermConfig.hpp"

class PtyProcess;

class KodoTerm : public QWidget {
    Q_OBJECT

  public:
    explicit KodoTerm(QWidget *parent = nullptr);
    ~KodoTerm();

    void setTheme(const TerminalTheme &theme);
    void setConfig(const KodoTermConfig &config);
    KodoTermConfig getConfig() const { return m_config; }

    void setProgram(const QString &program) { m_program = program; }
    QString program() const { return m_program; }
    void setArguments(const QStringList &arguments) { m_arguments = arguments; }
    QStringList arguments() const { return m_arguments; }
    void setWorkingDirectory(const QString &workingDirectory) {
        m_workingDirectory = workingDirectory;
    }
    QString workingDirectory() const { return m_workingDirectory; }
    void setProcessEnvironment(const QProcessEnvironment &environment) {
        m_environment = environment;
    }
    QProcessEnvironment processEnvironment() const { return m_environment; }
    bool start();

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
    void finished(int exitCode, int exitStatus);

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
    void kill();

    void zoomIn();
    void zoomOut();
    void resetZoom();

    bool isRoot() const;
    const QString &cwd() const { return m_cwd; }

    bool copyOnSelect() const { return m_config.copyOnSelect; }
    void setCopyOnSelect(bool enable) { m_config.copyOnSelect = enable; }

    bool pasteOnMiddleClick() const { return m_config.pasteOnMiddleClick; }
    void setPasteOnMiddleClick(bool enable) { m_config.pasteOnMiddleClick = enable; }
    bool mouseWheelZoom() const { return m_config.mouseWheelZoom; }
    void setMouseWheelZoom(bool enable) { m_config.mouseWheelZoom = enable; }
    bool visualBell() const { return m_config.visualBell; }
    void setVisualBell(bool enable) { m_config.visualBell = enable; }
    bool audibleBell() const { return m_config.audibleBell; }
    void setAudibleBell(bool enable) { m_config.audibleBell = enable; }

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

    bool m_selecting = false;
    VTermPos m_selectionStart = {-1, -1};
    VTermPos m_selectionEnd = {-1, -1};

    bool m_visualBellActive = false;
    QString m_cwd;
    QByteArray m_oscBuffer;

    QString m_program;
    QStringList m_arguments;
    QString m_workingDirectory;
    QProcessEnvironment m_environment = QProcessEnvironment::systemEnvironment();
    KodoTermConfig m_config;
};
