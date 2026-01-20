// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#pragma once

#include "KodoTermSession.hpp"
#include <QImage>
#include <QPainter>
#include <QSize>

class KodoTermRenderer {
  public:
    KodoTermRenderer();

    void updateSize(const QSize &viewSize, qreal dpr, KodoTermSession *session, int sbWidth = 0);
    void renderToBackbuffer(KodoTermSession *session, int scrollValue);
    void paint(QPainter &painter, const QRect &targetRect, KodoTermSession *session,
               int scrollValue, bool hasFocus, bool blinkState);

    void noteDamage(const QRect &rect);
    void moveRect(const QRect &dest, const QRect &src, int scrollValue, int sbSize);

    QSize cellSize() const { return m_cellSize; }
    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty = true);

  private:
    void resetDirtyRect();

    QSize m_cellSize;
    QImage m_backBuffer;
    int m_rows = 0;
    int m_cols = 0;
    bool m_dirty = true;

    struct DirtyRect {
        int start_row, start_col, end_row, end_col;
    } m_dirtyRect;

    std::vector<KodoTermSession::SavedCell> m_cellCache;
    std::vector<bool> m_selectedCache;
};
