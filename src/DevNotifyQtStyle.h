#pragma once

#include <QObject>
#include <libudev.h>

class DevNotifyQtStyle : public QObject
{
    Q_OBJECT
public:
    explicit DevNotifyQtStyle(QObject* parent = nullptr);

signals:
    void deviceConnectedOnCable(QString devpath, QString devnode);
    void deviceDisconnectedOnCable(QString devpath, QString devnode);

private:
    udev_monitor* udev_monitor_;
};
