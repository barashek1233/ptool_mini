#include "ConsoleAllwinnerFlasher.h"
#include <QDebug>
#include <QTimer>

ConsoleAllwinnerFlasher::ConsoleAllwinnerFlasher(
    AllwinnerDevicesManager& devices_manager,
    const QString& image_path,
    QObject* parent
)
    : QObject(parent)
    , m_devices_manager(devices_manager)
{
    connectSignals();

    initFlashing(image_path);
}

ConsoleAllwinnerFlasher::~ConsoleAllwinnerFlasher() = default;

void ConsoleAllwinnerFlasher::connectSignals()
{
    QObject::connect(&m_devices_manager, &AllwinnerDevicesManager::deviceConnected,
                     this, &ConsoleAllwinnerFlasher::onDeviceConnected);
    QObject::connect(&m_devices_manager, &AllwinnerDevicesManager::deviceDisconnected,
                     this, &ConsoleAllwinnerFlasher::onDeviceDisconnected);
    QObject::connect(&m_devices_manager, &AllwinnerDevicesManager::deviceFlashingStarted,
                     this, &ConsoleAllwinnerFlasher::onDeviceFlashingStarted);
    QObject::connect(&m_devices_manager, &AllwinnerDevicesManager::deviceFlashingResumed,
                     this, &ConsoleAllwinnerFlasher::onDeviceFlashingResumed);
    QObject::connect(&m_devices_manager, &AllwinnerDevicesManager::deviceFlashingCompleted,
                     this, &ConsoleAllwinnerFlasher::onDeviceFlashingCompleted);
    QObject::connect(&m_devices_manager, &AllwinnerDevicesManager::deviceFlashingFailed,
                     this, &ConsoleAllwinnerFlasher::onDeviceFlashingFailed);
    QObject::connect(&m_devices_manager, &AllwinnerDevicesManager::deviceFlashingProgressChanged,
                     this, &ConsoleAllwinnerFlasher::onDeviceFlashingProgressChanged);
    QObject::connect(&m_devices_manager, &AllwinnerDevicesManager::deviceManualPowerCycleRequested,
                     this, &ConsoleAllwinnerFlasher::onDeviceManualPowerCycleRequested);
    QObject::connect(&m_devices_manager, &AllwinnerDevicesManager::deviceFelJumperRemovalRequested,
                     this, &ConsoleAllwinnerFlasher::onDeviceFelJumperRemovalRequested);
}

void ConsoleAllwinnerFlasher::initFlashing(const QString& image_path)
{
    qInfo() << "Инициализация прошивки с файлом:" << image_path;
    if (!m_devices_manager.setImageFilepath(image_path)) {
        // Defer emit so the caller has time to connect signals after construction
        QTimer::singleShot(0, this, [this] { emit flashingFinished(1); });
        return;
    }

    qDebug() << "Polling запущен. Ожидаем подключения устройства.";
}

void ConsoleAllwinnerFlasher::onDeviceConnected(const QString& device) {
    qInfo() << "Устройство подключено:" << device;
}

void ConsoleAllwinnerFlasher::onDeviceDisconnected(const QString& device) {
    qDebug() << "Устройство отключено:" << device;
    // Если устройство отключилось до начала прошивки - не завершаем программу, ждём следующее
    if (m_flashing_started.contains(device)) {
        qWarning() << "Устройство отключилось во время прошивки";
        // Прошивка началась, но устройство отключили - завершаем с ошибкой
        if (m_flash_completed.fetchAndAddAcquire(1) == 0) {
            qCritical() << "Устройство отключили во время прошивки. Завершение работы с ошибкой.";
            emit flashingFinished(1);
        }
    } else {
        qDebug() << "Устройство отключили до начала прошивки. Ожидаем следующее устройство.";
    }
}

void ConsoleAllwinnerFlasher::onDeviceFlashingStarted(const QString& device) {
    qDebug() << "Прошивка начата для устройства:" << device;
    m_flashing_started.insert(device);
}

void ConsoleAllwinnerFlasher::onDeviceFlashingResumed(const QString& device) {
    qDebug() << "Прошивка возобновлена для:" << device;
    m_flashing_started.insert(device);
}

void ConsoleAllwinnerFlasher::onDeviceFlashingCompleted(const QString& device) {
    qDebug() << "Прошивка завершена успешно для:" << device;

    // Проверяем, что это первое успешное завершение
    if (m_flash_completed.fetchAndAddAcquire(1) == 0) {
        qDebug() << "Первое устройство успешно прошито. Завершение работы.";
        emit flashingFinished(0);  // Сигнал на завершение приложения
    }
}

void ConsoleAllwinnerFlasher::onDeviceFlashingFailed(const QString& device) {
    // Завершаем только если прошивка началась
    if (m_flashing_started.contains(device)) {
        qWarning() << "Прошивка началась, но завершилась ошибкой для устройства:" << device;

        // Проверяем, что это первое завершение (с ошибкой)
        if (m_flash_completed.fetchAndAddAcquire(1) == 0) {
            qCritical() << "Прошивка не удалась после начала записи. Завершение работы с ошибкой.";
            emit flashingFinished(1);
        }
    } else {
        qDebug() << "Прошивка не началась для устройства:" << device << ". Ожидаем следующее устройство.";
    }
}

void ConsoleAllwinnerFlasher::onDeviceFlashingProgressChanged(const QString& device, int progress) {
    qDebug() << "Прогресс прошивки" << device << ":" << progress << "%";
}

void ConsoleAllwinnerFlasher::onDeviceManualPowerCycleRequested(const QString& device) {
    qWarning() << "Требуется ручной перезапуск питания:" << device;
}

void ConsoleAllwinnerFlasher::onDeviceFelJumperRemovalRequested(const QString& device) {
    qWarning() << "Требуется снять FEL-перемычку:" << device;
}

