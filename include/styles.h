#pragma once
#include <QtCore/QString>
#include <QtCore/QFile>
#include <QtCore/QTextStream>

class styles {
public:
    static QString get(QString appPath, QString filename) {
        QFile file(appPath + "/" + filename);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();
        QTextStream in(&file);
        QString newAppPath = appPath;
        newAppPath.replace('\\', '/');
        QString result;
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line[0] == '$') {
                QString tmp;
                tmp.append(line.mid(1));
                tmp.replace("url(", "url(" + newAppPath + "/");
                result += tmp;
            }
            else
                result += line;
        }
        return result;
    }
};
