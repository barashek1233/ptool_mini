#ifndef CONSOLE_ALLWINNER_FLASHER_H
#define CONSOLE_ALLWINNER_FLASHER_H

#include <QObject>
#include <QAtomicInt>
#include <QSet>
#include "AllwinnerDevicesManager.h"

class ConsoleAllwinnerFlasher : public QObject
{
    Q_OBJECT

public:
    explicit ConsoleAllwinnerFlasher(
        AllwinnerDevicesManager& devices_manager,
        const QString& image_path,
        QObject* parent = nullptr
    );

    ~ConsoleAllwinnerFlasher();

signals:
    // Сигнал для завершения приложения после прошивки одного устройства
    void flashingFinished(int exitCode);

private:
    AllwinnerDevicesManager& m_devices_manager;
    QAtomicInt m_flash_completed{0};  // Флаг завершения прошивки
    QSet<QString> m_flashing_started;  // Устройства, где прошивка началась

    void initFlashing(const QString& image_path);
    void connectSignals();

private slots:
    void onDeviceConnected(const QString& device);
    void onDeviceDisconnected(const QString& device);
    void onDeviceFlashingStarted(const QString& device);
    void onDeviceFlashingResumed(const QString& device);
    void onDeviceFlashingCompleted(const QString& device);
    void onDeviceFlashingFailed(const QString& device);
    void onDeviceFlashingProgressChanged(const QString& device, int progress);
    void onDeviceManualPowerCycleRequested(const QString& device);
    void onDeviceFelJumperRemovalRequested(const QString& device);
};

#endif // CONSOLE_ALLWINNER_FLASHER_H
