#pragma once

#include <QFile>
#include <QStringList>

/**
 * @name parseBootConfig @brief парсит bootconfig.txt
 * Формат файла bootconfig.txt:
 * Файл предполагается в UTF-8
   строчки, начинающиеся с #, игнорируются
   остальные строчки приписываются к команде sam-ba -p <port> "-b" "sama5d2-xplained" "-a" "bootconfig" "-c"  по одной и
 выполняются
 */

QStringList parseBootConfig(QFile* file);
