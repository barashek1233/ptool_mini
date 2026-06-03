#include "Mapping.h"
#include <QDebug>

#include "loggingCategories.h"

static int
nextFreeCableIndex(QMap<QString, int> const& mapping)
{
    QSet<int> used;
    used.reserve(mapping.size());
    for (auto it = mapping.begin(); it != mapping.end(); ++it) {
        used.insert(it.value());
    }
    int i = 0;
    while (used.contains(i)) {
        ++i;
    }
    return i;
}

void
Mapping::on_deviceConnectedOnCable(QString path, QString devnode)
{
    qCInfo(cat_mapping).noquote() << QStringLiteral("Устройство подключено ") << path << devnode;

    if (not m_mapping.contains(path)) {
        int const new_cable_index = nextFreeCableIndex(m_mapping);
        m_mapping.insert(path, new_cable_index);
    }
    int const cable_index = m_mapping.value(path);
    m_devnodes[cable_index] = devnode;
    emit cubeConnected(cable_index, devnode);
}

void
Mapping::on_deviceDisconnectedOnCable(QString path)
{
    qCInfo(cat_mapping).noquote() << QStringLiteral("Устройство отключено ") << path;
    if (!m_mapping.contains(path)) {
        return;
    }
    qCDebug(cat_mapping).noquote() << "emitting cubeDisconnected" << m_mapping[path] << "(" << path << ")";
    m_devnodes.remove(m_mapping[path]);
    emit cubeDisconnected(m_mapping[path]);
    // m_mapping намеренно не очищается: при повторном подключении того же порта
    // устройство получит тот же номер кабеля (стабильная нумерация).
}
