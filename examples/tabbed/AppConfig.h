// SPDX-License-Identifier: MIT
// Author: Diego Iastrubni <diegoiast@gmail.com>

#pragma once

#include <QString>
#include <QStringList>

class AppConfig {
public:
    struct ShellInfo {
        QString name;
        QString path;
    };

    static QStringList availableShells();
    static ShellInfo getShellInfo(const QString &shellName);
};
