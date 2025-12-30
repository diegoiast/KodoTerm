// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#pragma once

#include <QWidget>
#include <QFont>
#include <QSocketNotifier>
#include <vterm.h>

class PtyProcess;

class KodoTerm : public QWidget {
    Q_OBJECT

public:
    explicit KodoTerm(QWidget *parent = nullptr);
    ~KodoTerm();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool focusNextPrevChild(bool next) override; // To capture Tab

private slots:
    void onPtyReadyRead(const QByteArray &data);

private:
    void setupPty();
    void updateTerminalSize();
    void drawCell(QPainter &painter, int row, int col, const VTermScreenCell &cell);

    // VTerm callbacks
    static int onDamage(VTermRect rect, void *user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
    //static int onPushLine(int cols, const VTermScreenCell *cells, void *user);
    //static int onPopLine(int cols, VTermScreenCell *cells, void *user);

    PtyProcess *m_pty = nullptr;
    
    VTerm *m_vterm = nullptr;
    VTermScreen *m_vtermScreen = nullptr;
    
    QSocketNotifier *m_notifier = nullptr;
    
    QFont m_font;
    QSize m_cellSize;
    int m_cursorRow = 0;
    int m_cursorCol = 0;
    bool m_cursorVisible = true;
};
