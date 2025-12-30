// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#pragma once

#include <QObject>
#include <QSize>

class PtyProcess : public QObject {
    Q_OBJECT

  public:
    explicit PtyProcess(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~PtyProcess() = default;

    virtual bool start(const QString &program, const QStringList &arguments, const QSize &size) = 0;
    virtual void write(const QByteArray &data) = 0;
    virtual void resize(const QSize &size) = 0;
    virtual void kill() = 0;

    // Factory method
    static PtyProcess *create(QObject *parent = nullptr);

  signals:
    void readyRead(const QByteArray &data);
    void finished(int exitCode, int exitStatus);
};
