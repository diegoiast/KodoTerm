// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#pragma once

#include "KodoTermRenderer.hpp"
#include "KodoTermSession.hpp"
#include <QScrollBar>
#include <QWidget>

class KodoTerm : public QWidget {
    Q_OBJECT

  public:
    explicit KodoTerm(QWidget *parent = nullptr);
    ~KodoTerm();

    static void
    populateThemeMenu(QMenu *parentMenu, const QString &title, TerminalTheme::ThemeFormat format,
                      const std::function<void(const TerminalTheme::ThemeInfo &)> &callback);

    void setTheme(const TerminalTheme &theme) { m_session->setTheme(theme); }
    void setConfig(const KodoTermConfig &config) { m_session->setConfig(config); }
    KodoTermConfig getConfig() const { return m_session->config(); }

    void setProgram(const QString &program) { m_session->setProgram(program); }
    QString program() const { return m_session->program(); }
    void setArguments(const QStringList &arguments) { m_session->setArguments(arguments); }
    QStringList arguments() const { return m_session->arguments(); }
    void setWorkingDirectory(const QString &workingDirectory) {
        m_session->setWorkingDirectory(workingDirectory);
    }
    QString workingDirectory() const { return m_session->workingDirectory(); }
    void setProcessEnvironment(const QProcessEnvironment &environment) {
        m_session->setProcessEnvironment(environment);
    }
    QProcessEnvironment processEnvironment() const { return m_session->processEnvironment(); }

    bool start(bool reset = true);
    void kill() { m_session->kill(); }

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool focusNextPrevChild(bool next) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

  signals:
    void contextMenuRequested(QMenu *menu, const QPoint &pos);
    void cwdChanged(const QString &cwd);
    void finished(int exitCode, int exitStatus);

  public slots:
    void copyToClipboard();
    void pasteFromClipboard();
    void selectAll();
    void clearScrollback();
    void resetTerminal();
    void openFileBrowser();

    void zoomIn();
    void zoomOut();
    void resetZoom();

    QString logPath() const { return m_session->logPath(); }
    void setRestoreLog(const QString &path) { m_session->setRestoreLog(path); }

    QString foregroundProcessName() const { return m_session->foregroundProcessName(); }
    bool isRoot() const { return m_session->isRoot(); }
    QString cwd() const { return m_session->workingDirectory(); }

  private slots:
    void onContentChanged(const QRect &rect);
    void onScrollbackChanged();
    void onScrollValueChanged(int value);

  private:
    void updateTerminalSize();

    KodoTermSession *m_session;
    QScrollBar *m_scrollBar;
    KodoTermRenderer m_renderer;

    QTimer *m_cursorBlinkTimer;
    bool m_cursorBlinkState = true;
};