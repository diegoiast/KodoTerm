// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "KodoTerm/KodoTermRenderer.hpp"
#include <QFontMetrics>
#include <algorithm>
#include <cstring>

KodoTermRenderer::KodoTermRenderer() : m_cellSize(10, 20) { resetDirtyRect(); }

void KodoTermRenderer::resetDirtyRect() {
    m_dirtyRect.start_row = 10000;
    m_dirtyRect.start_col = 10000;
    m_dirtyRect.end_row = -1;
    m_dirtyRect.end_col = -1;
}

void KodoTermRenderer::noteDamage(const QRect &rect) {
    m_dirtyRect.start_row = std::min(m_dirtyRect.start_row, rect.top());
    m_dirtyRect.start_col = std::min(m_dirtyRect.start_col, rect.left());
    m_dirtyRect.end_row = std::max(m_dirtyRect.end_row, rect.bottom());
    m_dirtyRect.end_col = std::max(m_dirtyRect.end_col, rect.right());
    m_dirty = true;
}

void KodoTermRenderer::setDirty(bool dirty) {
    m_dirty = dirty;
    if (dirty) {
        m_dirtyRect.start_row = 0;
        m_dirtyRect.start_col = 0;
        m_dirtyRect.end_row = 10000;
        m_dirtyRect.end_col = 10000;
        for (auto &c : m_cellCache) {
            c.chars[0] = (uint32_t)-1;
        }
        for (size_t i = 0; i < m_selectedCache.size(); ++i) {
            m_selectedCache[i] = false;
        }
    }
}

static bool colorsEqual(const VTermColor &a, const VTermColor &b) {
    if (a.type != b.type) {
        return false;
    }
    if (a.type == VTERM_COLOR_RGB) {
        return a.rgb.red == b.rgb.red && a.rgb.green == b.rgb.green && a.rgb.blue == b.rgb.blue;
    }
    if (a.type == VTERM_COLOR_INDEXED) {
        return a.indexed.idx == b.indexed.idx;
    }
    return true;
}

static bool cellsEqual(const KodoTermSession::SavedCell &a, const KodoTermSession::SavedCell &b) {
    if (a.width != b.width) {
        return false;
    }
    if (memcmp(&a.attrs, &b.attrs, sizeof(VTermScreenCellAttrs)) != 0) {
        return false;
    }
    if (!colorsEqual(a.fg, b.fg) || !colorsEqual(a.bg, b.bg)) {
        return false;
    }
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL; ++i) {
        if (a.chars[i] != b.chars[i]) {
            return false;
        }
        if (a.chars[i] == 0) {
            break;
        }
    }
    return true;
}

void KodoTermRenderer::updateSize(const QSize &viewSize, qreal dpr, KodoTermSession *session,
                                  int sbWidth) {
    const KodoTermConfig &cfg = session->config();
    QFont f = cfg.font;
    f.setKerning(false);
    if (cfg.textAntialiasing) {
        f.setStyleStrategy((QFont::StyleStrategy)(QFont::PreferAntialias | QFont::PreferQuality));
        f.setHintingPreference(QFont::PreferFullHinting);
    } else {
        f.setStyleStrategy(QFont::NoAntialias);
    }

    QFontMetrics fm(f);
    m_cellSize = QSize(fm.horizontalAdvance('W'), fm.height());
    if (m_cellSize.width() <= 0 || m_cellSize.height() <= 0) {
        m_cellSize = QSize(10, 20);
    }

    m_rows = viewSize.height() / m_cellSize.height();
    m_cols = (viewSize.width() - sbWidth) / m_cellSize.width();
    if (m_rows <= 0) {
        m_rows = 1;
    }
    if (m_cols <= 0) {
        m_cols = 1;
    }

    session->resizeTerminal(m_rows, m_cols);

    m_backBuffer = QImage(m_cols * m_cellSize.width() * dpr, m_rows * m_cellSize.height() * dpr,
                          QImage::Format_RGB32);
    m_backBuffer.setDevicePixelRatio(dpr);

    m_cellCache.assign(m_rows * m_cols, KodoTermSession::SavedCell{});
    for (auto &c : m_cellCache) {
        c.chars[0] = (uint32_t)-1;
    }
    m_selectedCache.assign(m_rows * m_cols, false);

    setDirty();
}

void KodoTermRenderer::moveRect(const QRect &dest, const QRect &src, int scrollValue, int sbSize) {
    if (m_backBuffer.isNull() || m_cellCache.empty()) {
        return;
    }
    if (scrollValue != sbSize) {
        return;
    }

    int cw = m_cellSize.width();
    int ch = m_cellSize.height();
    qreal dpr = m_backBuffer.devicePixelRatio();

    QRect dPix(dest.x() * cw * dpr, dest.y() * ch * dpr, dest.width() * cw * dpr,
               dest.height() * ch * dpr);
    QRect sPix(src.x() * cw * dpr, src.y() * ch * dpr, src.width() * cw * dpr,
               src.height() * ch * dpr);

    QImage copy = m_backBuffer.copy(sPix);
    copy.setDevicePixelRatio(dpr);
    QPainter p(&m_backBuffer);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.drawImage(dPix.topLeft() / dpr, copy);
    p.end();

    int h = src.height();
    if (dest.top() < src.top()) {
        for (int r = 0; r < h; ++r) {
            int sr = src.top() + r, dr = dest.top() + r;
            if (sr >= 0 && sr < m_rows && dr >= 0 && dr < m_rows) {
                std::copy(m_cellCache.begin() + sr * m_cols,
                          m_cellCache.begin() + sr * m_cols + m_cols,
                          m_cellCache.begin() + dr * m_cols);
                std::copy(m_selectedCache.begin() + sr * m_cols,
                          m_selectedCache.begin() + sr * m_cols + m_cols,
                          m_selectedCache.begin() + dr * m_cols);
            }
        }
    } else {
        for (int r = h - 1; r >= 0; --r) {
            int sr = src.top() + r, dr = dest.top() + r;
            if (sr >= 0 && sr < m_rows && dr >= 0 && dr < m_rows) {
                std::copy(m_cellCache.begin() + sr * m_cols,
                          m_cellCache.begin() + sr * m_cols + m_cols,
                          m_cellCache.begin() + dr * m_cols);
                std::copy(m_selectedCache.begin() + sr * m_cols,
                          m_selectedCache.begin() + sr * m_cols + m_cols,
                          m_selectedCache.begin() + dr * m_cols);
            }
        }
    }

    noteDamage(dest);
    noteDamage(src);
}

void KodoTermRenderer::renderToBackbuffer(KodoTermSession *session, int scrollValue) {
    if (m_backBuffer.isNull() || m_cellCache.empty()) {
        return;
    }

    int rows = session->rows();
    int cols = session->cols();
    int sbSize = session->scrollbackSize();
    bool useCache = (scrollValue == sbSize) && (rows == m_rows) && (cols == m_cols);

    if (useCache && (m_dirtyRect.start_row > m_dirtyRect.end_row)) {
        m_dirty = false;
        return;
    }

    QPainter painter(&m_backBuffer);
    const KodoTermConfig &cfg = session->config();

    QFont f = cfg.font;
    f.setKerning(false);
    if (cfg.textAntialiasing) {
        f.setStyleStrategy((QFont::StyleStrategy)(QFont::PreferAntialias | QFont::PreferQuality));
        f.setHintingPreference(QFont::PreferFullHinting);
    } else {
        f.setStyleStrategy(QFont::NoAntialias);
    }

    painter.setFont(f);
    painter.setRenderHint(QPainter::TextAntialiasing, cfg.textAntialiasing);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QColor defBg = cfg.theme.background;

    int sR = 0, eR = rows, sC = 0, eC = cols;
    if (useCache) {
        sR = std::max(0, m_dirtyRect.start_row);
        eR = std::min(rows, m_dirtyRect.end_row + 1);
        sC = std::max(0, m_dirtyRect.start_col);
        eC = std::min(cols, m_dirtyRect.end_col + 1);
    } else {
        m_backBuffer.fill(defBg);
    }

    for (int r = sR; r < eR; ++r) {
        int absR = scrollValue + r;
        for (int c = sC; c < eC; ++c) {
            KodoTermSession::SavedCell cell;
            if (!session->getCell(absR, c, cell)) {
                memset(&cell, 0, sizeof(cell));
                cell.width = 1;
            }

            if (cell.width == 0) {
                continue;
            }

            bool sel = session->isSelected(absR, c);
            if (useCache && sel == m_selectedCache[r * m_cols + c] &&
                cellsEqual(cell, m_cellCache[r * m_cols + c])) {
                if (cell.width > 1) {
                    c += (cell.width - 1);
                }
                continue;
            }

            if (useCache) {
                m_cellCache[r * m_cols + c] = cell;
                m_selectedCache[r * m_cols + c] = sel;
            }

            QColor fg = session->mapColor(cell.fg);
            QColor bg = session->mapColor(cell.bg);

            if (cell.attrs.reverse ^ sel) {
                std::swap(fg, bg);
            }

            QRect rect(c * m_cellSize.width(), r * m_cellSize.height(),
                       cell.width * m_cellSize.width(), m_cellSize.height());
            painter.fillRect(rect, bg);

            if (cell.chars[0] != 0) {
                int n = 0;
                while (n < VTERM_MAX_CHARS_PER_CELL && cell.chars[n]) {
                    n++;
                }
                painter.setPen(fg);
                painter.drawText(rect, Qt::AlignCenter,
                                 QString::fromUcs4((const char32_t *)cell.chars, n));
            }

            if (cell.width > 1) {
                c += (cell.width - 1);
            }
        }
    }

    resetDirtyRect();
    m_dirty = false;
}

void KodoTermRenderer::paint(QPainter &painter, const QRect &targetRect, KodoTermSession *session,
                             int scrollValue, bool hasFocus, bool blinkState) {
    if (m_dirty || m_backBuffer.isNull()) {
        renderToBackbuffer(session, scrollValue);
    }

    QColor defBg = session->config().theme.background;
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(targetRect, defBg);

    if (!m_backBuffer.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(targetRect.topLeft(), m_backBuffer, targetRect);
    }

    if (hasFocus && session->cursorVisible() && scrollValue == session->scrollbackSize() &&
        (!session->cursorBlink() || blinkState)) {

        QRect r(session->cursorCol() * m_cellSize.width(),
                session->cursorRow() * m_cellSize.height(), m_cellSize.width(),
                m_cellSize.height());

        painter.setCompositionMode(QPainter::CompositionMode_Difference);
        switch (session->cursorShape()) {
        case 2:
            painter.fillRect(r.x(), r.y(), 2, r.height(), Qt::white);
            break;
        case 3:
            painter.fillRect(r.x(), r.y() + r.height() - 2, r.width(), 2, Qt::white);
            break;
        default:
            painter.fillRect(r, Qt::white);
            break;
        }
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
}