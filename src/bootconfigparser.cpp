#include "bootconfigparser.h"
#include <QTextStream>

QStringList
parseBootConfig(QFile* file)
{
    QString line;
    QStringList res;
    QTextStream stream(file);
    stream.setCodec("UTF-8");
    while (!stream.atEnd()) {
        line = stream.readLine();
        if (line.trimmed().startsWith('#')) {
            continue;
        }
        res.append(line);
    }
    return res;
}
