// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "AppConfig.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStandardPaths>

QStringList AppConfig::availableShells() {
    QStringList shells;
#ifdef Q_OS_WIN
    QStringList paths = QString::fromUtf8(qgetenv("PATH")).split(';');
    auto check = [&](const QString &name, const QString &exe) {
        if (!QStandardPaths::findExecutable(exe, paths).isEmpty()) {
            shells.append(name);
        }
    };
    
    // Check for git bash explicitly in common locations if not in PATH
    QString gitBash = "C:\\Program Files\\Git\\bin\\bash.exe";
    if (QFile::exists(gitBash)) {
        shells.append("Git Bash");
    }

    check("Command Prompt", "cmd.exe");
    check("PowerShell", "powershell.exe");

#else
    QFile file("/etc/shells");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (!line.isEmpty() && !line.startsWith('#')) {
                shells.append(line);
            }
        }
    }
    // Fallback if /etc/shells is empty or unreadable
    if (shells.isEmpty()) {
        shells << "/bin/bash" << "/bin/sh";
    }
#endif
    return shells;
}

AppConfig::ShellInfo AppConfig::getShellInfo(const QString &shellName) {
    ShellInfo info;
    info.name = shellName;
    
#ifdef Q_OS_WIN
    if (shellName == "Git Bash") {
        info.path = "C:\\Program Files\\Git\\bin\\bash.exe";
    } else if (shellName == "Command Prompt") {
        info.path = "cmd.exe";
    } else if (shellName == "PowerShell") {
        info.path = "powershell.exe";
    }
#else
    info.path = shellName;
#endif
    return info;
}
