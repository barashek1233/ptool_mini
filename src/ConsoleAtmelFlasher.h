#pragma once

#include <QObject>
#include <QAtomicInteger>
#include <QSet>

class CubeFlasherManager;

/**
 * @brief Класс для консольного режима прошивки Atmel устройств.
 * Завершает работу приложения после успешной прошивки первого устройства,
 * либо если прошивка началась, но завершилась неудачей.
 */
class ConsoleAtmelFlasher : public QObject
{
    Q_OBJECT

public:
    explicit ConsoleAtmelFlasher(CubeFlasherManager& manager, QObject* parent = nullptr);
    ~ConsoleAtmelFlasher() override = default;

signals:
    void flashingFinished(int exit_code);

private slots:
    void onDeviceFlashedCompleted(int cable_number);
    void onDeviceFlashedFailed(int cable_number);
    void onDeviceFlashingCouldNotStart(int cable_number);
    void onDeviceFlashingInfoChanged(int cable_number, int percentage, QString const& description);
    void onDeviceDisconnected(int cable_number);

private:
    CubeFlasherManager& m_manager;
    QAtomicInteger<int> m_flash_completed{0};
    QSet<int> m_flashing_started;  // Номера кабелей, где прошивка началась
};
