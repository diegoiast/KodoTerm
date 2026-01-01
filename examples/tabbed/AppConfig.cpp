// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#include "AppConfig.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>

static QSettings getSettings() {
    return QSettings("Diego Iastrubni", "KodoTermTabbed");
}

QList<AppConfig::ShellInfo> AppConfig::detectedShells() {
    QList<ShellInfo> shells;
    auto add = [&](const QString &name, const QString &path) {
        shells.append({name, path});
    };

#ifdef Q_OS_WIN
    QStringList paths = QString::fromUtf8(qgetenv("PATH")).split(';');
    auto check = [&](const QString &name, const QString &exe) {
        QString p = QStandardPaths::findExecutable(exe, paths);
        if (!p.isEmpty()) {
            add(name, p);
        }
    };
    
    // Check for git bash explicitly in common locations if not in PATH
    QString gitBash = "C:\\Program Files\\Git\\bin\\bash.exe";
    if (QFile::exists(gitBash)) {
        add("Git Bash", gitBash);
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
                add(line, line);
            }
        }
    }
    // Fallback if /etc/shells is empty or unreadable
    if (shells.isEmpty()) {
        add("Bash", "/bin/bash");
        add("Sh", "/bin/sh");
    }
#endif
    return shells;
}

QList<AppConfig::ShellInfo> AppConfig::loadShells() {
    QSettings s = getSettings();
    QList<ShellInfo> shells;
    int size = s.beginReadArray("Shells");
    if (size > 0) {
        for (int i = 0; i < size; ++i) {
            s.setArrayIndex(i);
            ShellInfo info;
            info.name = s.value("name").toString();
            info.path = s.value("path").toString();
            shells.append(info);
        }
        s.endArray();
    } else {
        // First run or empty, fallback to detected
        shells = detectedShells();
        saveShells(shells); // Save them for next time
    }
    return shells;
}

void AppConfig::saveShells(const QList<ShellInfo> &shells) {
    QSettings s = getSettings();
    s.beginWriteArray("Shells");
    for (int i = 0; i < shells.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue("name", shells[i].name);
        s.setValue("path", shells[i].path);
    }
    s.endArray();
}

QString AppConfig::defaultShell() {
    QSettings s = getSettings();
    QString def = s.value("DefaultShell").toString();
    if (def.isEmpty()) {
        QList<ShellInfo> shells = loadShells();
        if (!shells.isEmpty()) {
            def = shells.first().name;
        }
    }
    return def;
}

void AppConfig::setDefaultShell(const QString &name) {
    QSettings s = getSettings();
    s.setValue("DefaultShell", name);
}

AppConfig::ShellInfo AppConfig::getShellInfo(const QString &shellName) {
    QList<ShellInfo> shells = loadShells();
    for (const auto &info : shells) {
        if (info.name == shellName) {
            return info;
        }
    }
    return {shellName, shellName}; // Fallback
}