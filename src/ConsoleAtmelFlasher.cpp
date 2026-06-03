#include "ConsoleAtmelFlasher.h"
#include "CubeFlasherManager.h"
#include <QDebug>

ConsoleAtmelFlasher::ConsoleAtmelFlasher(CubeFlasherManager& manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager)
{
    connect(
        &m_manager, &CubeFlasherManager::deviceFlashedCompleted, this,
        &ConsoleAtmelFlasher::onDeviceFlashedCompleted);
    connect(
        &m_manager, &CubeFlasherManager::deviceFlashedFailed, this,
        &ConsoleAtmelFlasher::onDeviceFlashedFailed);
    connect(
        &m_manager, &CubeFlasherManager::deviceFlashingCouldNotStart, this,
        &ConsoleAtmelFlasher::onDeviceFlashingCouldNotStart);
    connect(
        &m_manager, &CubeFlasherManager::deviceFlashingInfoChanged, this,
        &ConsoleAtmelFlasher::onDeviceFlashingInfoChanged);
    connect(
        &m_manager, &CubeFlasherManager::deviceDisconnected, this,
        &ConsoleAtmelFlasher::onDeviceDisconnected);
}

void ConsoleAtmelFlasher::onDeviceFlashedCompleted(int cable_number)
{
    qDebug() << "Устройство успешно прошито на кабеле:" << cable_number;

    // Проверяем, что это первое успешное завершение
    if (m_flash_completed.fetchAndAddAcquire(1) == 0) {
        qDebug() << "Первое устройство успешно прошито. Завершение работы.";
        emit flashingFinished(0);
    }
}

void ConsoleAtmelFlasher::onDeviceFlashedFailed(int cable_number)
{
    // Завершаем только если прошивка началась (перемычку сняли)
    if (m_flashing_started.contains(cable_number)) {
        qWarning() << "Прошивка началась, но завершилась ошибкой на кабеле:" << cable_number;

        // Проверяем, что это первое завершение (с ошибкой)
        if (m_flash_completed.fetchAndAddAcquire(1) == 0) {
            qCritical() << "Прошивка не удалась после начала записи. Завершение работы с ошибкой.";
            emit flashingFinished(1);
        }
    } else {
        qDebug() << "Прошивка не началась на кабеле:" << cable_number << "(перемычку не сняли). Ожидаем следующее устройство.";
    }
}

void ConsoleAtmelFlasher::onDeviceFlashingCouldNotStart(int cable_number)
{
    // Прошивка не смогла начаться (перемычку не сняли) - НЕ завершаем программу
    qDebug() << "Прошивка не смогла начаться на кабеле:" << cable_number 
             << "(перемычку не сняли). Ожидаем следующее устройство.";
}

void ConsoleAtmelFlasher::onDeviceFlashingInfoChanged(int cable_number, int percentage, QString const& description)
{
    qDebug() << "Прогресс прошивки кабель" << cable_number << ":" << percentage << "%";

    // Прошивка началась, если мы получили информацию о этапе стирания/записи/верификации
    // Это означает что перемычку сняли и sam-ba выполняет команды
    if (description.contains("стирания", Qt::CaseInsensitive) ||
        description.contains("записи", Qt::CaseInsensitive) ||
        description.contains("верификации", Qt::CaseInsensitive) ||
        description.contains("erasing", Qt::CaseInsensitive) ||
        description.contains("writing", Qt::CaseInsensitive) ||
        description.contains("verifying", Qt::CaseInsensitive)) {
        m_flashing_started.insert(cable_number);
    }
}

void ConsoleAtmelFlasher::onDeviceDisconnected(int cable_number)
{
    // Если устройство отключили до начала прошивки - не завершаем программу
    if (m_flashing_started.contains(cable_number)) {
        qWarning() << "Устройство отключили во время прошивки на кабеле:" << cable_number;
        // Прошивка началась, но устройство отключили - завершаем с ошибкой
        if (m_flash_completed.fetchAndAddAcquire(1) == 0) {
            qCritical() << "Устройство отключили во время прошивки. Завершение работы с ошибкой.";
            emit flashingFinished(1);
        }
    } else {
        qDebug() << "Устройство отключили до начала прошивки на кабеле:" << cable_number 
                 << ". Ожидаем следующее устройство.";
    }
}
