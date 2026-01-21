// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#pragma once

#include "KodoTermConfig.hpp"
#include <QColor>
#include <QFile>
#include <QObject>
#include <QPoint>
#include <QProcessEnvironment>
#include <QSize>
#include <QStringList>
#include <deque>
#include <vector>
#include <vterm.h>

class PtyProcess;
class QTimer;

class KodoTermSession : public QObject {
    Q_OBJECT

  public:
    explicit KodoTermSession(QObject *parent = nullptr);
    ~KodoTermSession();

    // Configuration
    void setConfig(const KodoTermConfig &config);
    KodoTermConfig config() const { return m_config; }
    void setTheme(const TerminalTheme &theme);

    // Process Management
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

    bool start(bool reset = true);
    void kill();
    bool isRunning() const;
    QString foregroundProcessName() const;
    bool isRoot() const;

    // Interaction
    void sendKey(int key, Qt::KeyboardModifiers modifiers, const QString &text = QString());
    void sendMouse(int button, int row, int col, Qt::KeyboardModifiers modifiers, bool pressed);
    void sendMouseMove(int row, int col, Qt::KeyboardModifiers modifiers);
    void sendText(const QString &text);
    void resizeTerminal(int rows, int cols);

    // State Access
    struct SavedCell {
        uint32_t chars[VTERM_MAX_CHARS_PER_CELL];
        VTermScreenCellAttrs attrs;
        VTermColor fg, bg;
        int width;
    };

    // Helper to get a cell at a visual position (handling scrollback)
    // returns true if valid
    bool getCell(int row, int col, SavedCell &cell) const;

    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

    int cursorRow() const { return m_cursorRow; }
    int cursorCol() const { return m_cursorCol; }
    bool cursorVisible() const { return m_cursorVisible; }
    bool cursorBlink() const { return m_cursorBlink; }
    int cursorShape() const { return m_cursorShape; }
    int mouseMode() const { return m_mouseMode; }

    int scrollbackSize() const { return (int)m_scrollback.size(); }
    void clearScrollback();
    void resetTerminal();

    // Logging & Restoration
    void setLoggingEnabled(bool enabled);
    bool isLoggingEnabled() const;
    void setLogDirectory(const QString &dir);
    QString logPath() const;
    void setRestoreLog(const QString &path);
    void processLogReplay();

    // Selection
    void setSelection(const QPoint &start, const QPoint &end);
    void selectAll();
    void clearSelection();
    QPoint selectionStart() const { return m_selectionStart; }
    QPoint selectionEnd() const { return m_selectionEnd; }
    QString selectedText() const;
    bool isSelected(int row, int col) const;

    // Colors
    QColor mapColor(const VTermColor &c) const;

  signals:
    void contentChanged(const QRect &rect); // Rect in character cells
    void rectMoved(const QRect &dest, const QRect &src);
    void cursorMoved(int row, int col);
    void cursorVisibilityChanged(bool visible);
    void scrollbackChanged(); // Size changed
    void bell();
    void titleChanged(const QString &title);
    void finished(int exitCode, int exitStatus);
    void cwdChanged(const QString &cwd);
    void propChanged(VTermProp prop, const VTermValue *val);

  private slots:
    void onPtyReadyRead(const QByteArray &data);

  private:
    void setupPty();
    int pushScrollback(int cols, const VTermScreenCell *cells);
    int popScrollback(int cols, VTermScreenCell *cells);
    void flushTerminal();

    // VTerm callbacks
    static int onDamage(VTermRect rect, void *user);
    static int onMoveRect(VTermRect dest, VTermRect src, void *user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int onSetTermProp(VTermProp prop, VTermValue *val, void *user);
    static int onBell(void *user);
    static int onSbPushLine(int cols, const VTermScreenCell *cells, void *user);
    static int onSbPopLine(int cols, VTermScreenCell *cells, void *user);
    static int onOsc(int command, VTermStringFragment frag, void *user);

    PtyProcess *m_pty = nullptr;
    VTerm *m_vterm = nullptr;
    VTermScreen *m_vtermScreen = nullptr;

    QString m_program;
    QStringList m_arguments;
    QString m_workingDirectory;
    QProcessEnvironment m_environment;
    KodoTermConfig m_config;

    int m_rows = 24;
    int m_cols = 80;

    using SavedLine = std::vector<SavedCell>;
    std::deque<SavedLine> m_scrollback;

    int m_cursorRow = 0;
    int m_cursorCol = 0;
    bool m_cursorVisible = true;
    bool m_cursorBlink = false;
    int m_cursorShape = 1;
    bool m_altScreen = false;
    int m_mouseMode = 0;

    QPoint m_selectionStart = {-1, -1};
    QPoint m_selectionEnd = {-1, -1};

    mutable QColor m_paletteCache[256];
    mutable bool m_paletteCacheValid[256];
    mutable VTermColor m_lastVTermFg, m_lastVTermBg;
    mutable QColor m_lastFg, m_lastBg;

    QByteArray m_oscBuffer;
    QString m_cwd;

    QFile m_logFile;
    QString m_pendingLogReplay;
    QFile *m_replayFile = nullptr;
    bool m_restoring = false;
    bool m_suppressSignals = false;
};
