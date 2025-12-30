// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "PtyProcess_unix.h"

#include <QDebug>
#include <QCoreApplication>
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>

PtyProcessUnix::PtyProcessUnix(QObject *parent) : PtyProcess(parent) {
}

PtyProcessUnix::~PtyProcessUnix() {
    kill();
    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
}

bool PtyProcessUnix::start(const QString &program, const QStringList &arguments, const QSize &size) {
    struct winsize ws;
    ws.ws_row = (unsigned short)size.height();
    ws.ws_col = (unsigned short)size.width();
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    pid_t pid = forkpty(&m_masterFd, nullptr, nullptr, &ws);

    if (pid == -1) {
        qWarning() << "Failed to forkpty";
        return false;
    }

    if (pid == 0) {
        // Child
        setenv("TERM", "xterm-256color", 1);
        
        // Prepare args
        // If program is /bin/bash, args might be empty
        // execl expects path, arg0, ..., nullptr
        // For simplicity, we just exec bash like before if program is bash
        // But let's support generic execvp
        
        // Convert args to char* array
        std::vector<char*> args;
        QByteArray progBytes = program.toLocal8Bit();
        args.push_back(progBytes.data());
        
        // Helper to keep storage alive
        std::vector<QByteArray> storage;
        storage.reserve(arguments.size());
        
        for (const auto &arg : arguments) {
            storage.push_back(arg.toLocal8Bit());
            args.push_back(storage.back().data());
        }
        args.push_back(nullptr);
        
        execvp(program.toLocal8Bit().constData(), args.data());
        
        // If execvp returns, it failed
        _exit(1);
    } else {
        // Parent
        m_pid = pid;
        
        m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &PtyProcessUnix::onReadyRead);
        
        return true;
    }
}

void PtyProcessUnix::write(const QByteArray &data) {
    if (m_masterFd >= 0) {
        ::write(m_masterFd, data.constData(), data.size());
    }
}

void PtyProcessUnix::resize(const QSize &size) {
    if (m_masterFd >= 0) {
        struct winsize ws;
        ws.ws_row = (unsigned short)size.height();
        ws.ws_col = (unsigned short)size.width();
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
        ioctl(m_masterFd, TIOCSWINSZ, &ws);
    }
}

void PtyProcessUnix::kill() {
    if (m_pid > 0) {
        ::kill(m_pid, SIGTERM);
        // Wait? usually waitpid via signal handler, but for now just cleanup
        m_pid = -1;
    }
}

void PtyProcessUnix::onReadyRead() {
    char buffer[4096];
    ssize_t len = ::read(m_masterFd, buffer, sizeof(buffer));

    if (len > 0) {
        emit readyRead(QByteArray(buffer, (int)len));
    } else if (len < 0 && errno != EAGAIN) {
        m_notifier->setEnabled(false);
        emit finished(-1, -1); // Error
    } else if (len == 0) {
        m_notifier->setEnabled(false);
        emit finished(0, 0); // EOF
    }
}
