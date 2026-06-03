#pragma once

#include <QString>
#include <QObject>
#include <QMap>

class Mapping : public QObject
{
    Q_OBJECT
public:
    Mapping() = default;

signals:
    void cubeConnected(int, QString devnode);
    void cubeDisconnected(int);

public slots:
    void on_deviceConnectedOnCable(QString path, QString devnode);
    void on_deviceDisconnectedOnCable(QString path);

private:
    QMap<QString, int> m_mapping;
    QMap<int, QString> m_devnodes;
};
