// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "PtyProcess_win.h"
#include <QDebug>
#include <vector>

// Define necessary types if building on older SDKs or mingw that might lack them
// But assuming standard modern environment.

class PtyProcessWin::ReaderThread : public QThread {
  public:
    ReaderThread(HANDLE hPipe, PtyProcessWin *parent) : m_hPipe(hPipe), m_parent(parent) {}

    void run() override {
        char buffer[4096];
        DWORD bytesRead;
        while (m_running) {
            if (ReadFile(m_hPipe, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
                // Determine if we need to emit via meta-object system
                // Since we are in a thread, we should invoke method on parent
                QByteArray data(buffer, (int)bytesRead);
                QMetaObject::invokeMethod(m_parent, "onReadThreadData", Qt::QueuedConnection,
                                          Q_ARG(QByteArray, data));
            } else {
                break;
            }
        }
    }

    void stop() {
        m_running = false;
        // CancelSynchronousIo(threadHandle) or CloseHandle(m_hPipe) externally triggers exit
    }

  private:
    HANDLE m_hPipe;
    PtyProcessWin *m_parent;
    bool m_running = true;
};

PtyProcessWin::PtyProcessWin(QObject *parent) : PtyProcess(parent) {
    ZeroMemory(&m_pi, sizeof(PROCESS_INFORMATION));
}

PtyProcessWin::~PtyProcessWin() {
    kill();
    if (m_hPC != INVALID_HANDLE_VALUE) {
        ClosePseudoConsole(m_hPC);
        m_hPC = INVALID_HANDLE_VALUE;
    }
    if (m_hPipeIn != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hPipeIn);
    }
    if (m_hPipeOut != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hPipeOut);
    }
}

bool PtyProcessWin::start(const QString &program, const QStringList &arguments, const QSize &size) {
    HANDLE hPipePTYIn = INVALID_HANDLE_VALUE;
    HANDLE hPipePTYOut = INVALID_HANDLE_VALUE;

    // Create pipes
    if (!CreatePipe(&hPipePTYIn, &m_hPipeOut, NULL, 0)) {
        return false;
    }
    if (!CreatePipe(&m_hPipeIn, &hPipePTYOut, NULL, 0)) {
        return false;
    }

    // Create Pseudo Console
    COORD origin = {(SHORT)size.width(), (SHORT)size.height()};
    HRESULT hr = CreatePseudoConsole(origin, hPipePTYIn, hPipePTYOut, 0, &m_hPC);

    // Close the sides we don't need
    CloseHandle(hPipePTYIn);
    CloseHandle(hPipePTYOut);

    if (FAILED(hr)) {
        return false;
    }

    // Prepare Startup Info
    STARTUPINFOEX si;
    ZeroMemory(&si, sizeof(STARTUPINFOEX));
    si.StartupInfo.cb = sizeof(STARTUPINFOEX);

    SIZE_T bytesRequired = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &bytesRequired);
    si.lpAttributeList = (PPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, bytesRequired);
    if (!si.lpAttributeList) {
        return false;
    }

    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytesRequired)) {
        return false;
    }

    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   m_hPC, sizeof(HPCON), NULL, NULL)) {
        return false;
    }

    // Command Line
    QString cmd = program;
    for (const auto &arg : arguments) {
        cmd += " " + arg; // Simple quoting might be needed
    }

    // Create Process
    std::vector<wchar_t> cmdLine(cmd.length() + 1);
    cmd.toWCharArray(cmdLine.data());
    cmdLine[cmd.length()] = 0;

    BOOL success = CreateProcessW(NULL, cmdLine.data(), NULL, NULL, FALSE,
                                  EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &si.StartupInfo, &m_pi);

    // Cleanup attribute list
    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);

    if (!success) {
        return false;
    }

    // Start reader thread
    m_readerThread = new ReaderThread(m_hPipeIn, this);
    m_readerThread->start();

    return true;
}

void PtyProcessWin::write(const QByteArray &data) {
    if (m_hPipeOut != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten;
        WriteFile(m_hPipeOut, data.constData(), data.size(), &bytesWritten, NULL);
    }
}

void PtyProcessWin::resize(const QSize &size) {
    if (m_hPC != INVALID_HANDLE_VALUE) {
        COORD origin = {(SHORT)size.width(), (SHORT)size.height()};
        ResizePseudoConsole(m_hPC, origin);
    }
}

void PtyProcessWin::kill() {
    if (m_readerThread) {
        m_readerThread->stop();
        // Closing handle usually terminates blocking read
        CloseHandle(m_hPipeIn);
        m_hPipeIn = INVALID_HANDLE_VALUE;
        m_readerThread->wait();
        delete m_readerThread;
        m_readerThread = nullptr;
    }

    if (m_pi.hProcess) {
        TerminateProcess(m_pi.hProcess, 1);
        CloseHandle(m_pi.hProcess);
        CloseHandle(m_pi.hThread);
        m_pi.hProcess = NULL;
    }
}

void PtyProcessWin::onReadThreadData(const QByteArray &data) { emit readyRead(data); }
