// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#pragma once

#include <KodoTerm/KodoTermRenderer.hpp>
#include <KodoTerm/KodoTermSession.hpp>
#include <QImage>
#include <QQuickPaintedItem>
#include <QTimer>

class KodoQuickTerm : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString program READ program WRITE setProgram NOTIFY programChanged)
    Q_PROPERTY(QStringList arguments READ arguments WRITE setArguments NOTIFY argumentsChanged)
    Q_PROPERTY(QString workingDirectory READ workingDirectory WRITE setWorkingDirectory NOTIFY
                   workingDirectoryChanged)
    Q_PROPERTY(int scrollValue READ scrollValue WRITE setScrollValue NOTIFY scrollValueChanged)
    Q_PROPERTY(int scrollMax READ scrollMax NOTIFY scrollMaxChanged)

  public:
    explicit KodoQuickTerm(QQuickItem *parent = nullptr);
    ~KodoQuickTerm();

    void paint(QPainter *painter) override;

    void setTheme(const TerminalTheme &theme);
    void setConfig(const KodoTermConfig &config);
    KodoTermConfig getConfig() const { return m_session->config(); }

    void setProgram(const QString &program);
    QString program() const;
    void setArguments(const QStringList &arguments);
    QStringList arguments() const;
    void setWorkingDirectory(const QString &workingDirectory);
    QString workingDirectory() const;

    Q_INVOKABLE bool start(bool reset = true);
    Q_INVOKABLE void kill();

    int scrollValue() const { return m_scrollValue; }
    void setScrollValue(int value);
    int scrollMax() const { return m_session->scrollbackSize(); }

  signals:
    void programChanged();
    void argumentsChanged();
    void workingDirectoryChanged();
    void scrollValueChanged();
    void scrollMaxChanged();
    void cwdChanged(const QString &cwd);
    void finished(int exitCode, int exitStatus);
    void bell();

  protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

  private slots:
    void onContentChanged(const QRect &rect);
    void onScrollbackChanged();

  private:
    void updateTerminalSize();

    KodoTermSession *m_session;
    KodoTermRenderer m_renderer;

    int m_scrollValue = 0;

    QTimer *m_blinkTimer;
    bool m_blinkState = true;
};