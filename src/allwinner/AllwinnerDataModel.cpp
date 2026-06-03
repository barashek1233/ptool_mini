#include "AllwinnerDataModel.h"

#include <QMetaEnum>
#include <QDebug>

#include "AllwinnerDevicesManager.h"

static constexpr auto kErrorNotExistingDataMessage{"There is no information about device"};

AllwinnerDataModel::AllwinnerDataModel(AllwinnerDevicesManager& devices_manager, QObject* parent) :
      QAbstractTableModel(parent)
{
    connect(
        &devices_manager, &AllwinnerDevicesManager::deviceConnected, this,
        &AllwinnerDataModel::processDeviceConnected);
    connect(
        &devices_manager, &AllwinnerDevicesManager::deviceDisconnected, this,
        &AllwinnerDataModel::processDeviceDisconnected);
    connect(
        &devices_manager, &AllwinnerDevicesManager::deviceFlashingStarted, this,
        &AllwinnerDataModel::processDeviceFlashingStarted);
    connect(
        &devices_manager, &AllwinnerDevicesManager::deviceFlashingResumed, this,
        &AllwinnerDataModel::processDeviceFlashingResumed);
    connect(
        &devices_manager, &AllwinnerDevicesManager::deviceFlashingCompleted, this,
        &AllwinnerDataModel::processDeviceFlashingCompleted);
    connect(
        &devices_manager, &AllwinnerDevicesManager::deviceFlashingFailed, this,
        &AllwinnerDataModel::processDeviceFlashingFailed);
    connect(
        &devices_manager, &AllwinnerDevicesManager::deviceManualPowerCycleRequested, this,
        &AllwinnerDataModel::processDeviceManualPowerCycleRequested);
    connect(
        &devices_manager, &AllwinnerDevicesManager::deviceFelJumperRemovalRequested, this,
        &AllwinnerDataModel::processDeviceFelJumperRemovalRequested);
    connect(
        &devices_manager, &AllwinnerDevicesManager::deviceFlashingProgressChanged, this,
        &AllwinnerDataModel::processDeviceFlashingProgressChanged);
}

int
AllwinnerDataModel::rowCount(QModelIndex const&) const
{
    return (m_devices.count() + ColumnsPerRow - 1) / ColumnsPerRow;
}

int
AllwinnerDataModel::columnCount(QModelIndex const&) const
{
    return ColumnsPerRow;
}

QVariant
AllwinnerDataModel::data(QModelIndex const& index, int role) const
{
    auto const col = index.column();
    auto const row = index.row();
    auto const device_index = (row * ColumnsPerRow) + col;
    if (device_index >= m_devices.count()) {
        return {}; /// Пустая ячейка
    }
    auto const& device = m_devices[device_index];
    switch (role) {
    case Qt::DisplayRole: {
        return _labelFromDeviceInfo(device);
    }
    case Qt::TextAlignmentRole: {
        return Qt::AlignCenter;
    }
    }
    return {};
}

void
AllwinnerDataModel::processDeviceConnected(QString const& device)
{
    auto const iter = _getInfoAboutDevice(device);
    if (iter == m_devices.end()) {
        m_devices.push_back({device, DeviceState::kConnected});
    } else {
        iter->progress   = 0;
        iter->elapsed    = 0;
        iter->started_at = QDateTime::currentDateTime();
        iter->state      = DeviceState::kConnected;
    }
    _updateModel();
}

void
AllwinnerDataModel::processDeviceDisconnected(QString const& device)
{
    _updateDeviceState(device, DeviceState::kDisconnected);
}

void
AllwinnerDataModel::processDeviceFlashingStarted(QString const& device)
{
    _updateDeviceState(device, DeviceState::kFlashingStarted);
}

void
AllwinnerDataModel::processDeviceFlashingResumed(QString const& device) {
    _updateDeviceState(device, DeviceState::kFlashingInProgress);
}

void
AllwinnerDataModel::processDeviceFlashingCompleted(QString const& device)
{
    _updateDeviceState(device, DeviceState::kFlashingCompleted);
    _showElapsedTime(device);
}

void
AllwinnerDataModel::processDeviceManualPowerCycleRequested(QString const& device) {
    _pauseFlashing(device, "POWER CYCLE DEVICE");
}

void
AllwinnerDataModel::processDeviceFelJumperRemovalRequested(QString const& device) {
    _pauseFlashing(device, "REMOVE JUMPER");
}

void
AllwinnerDataModel::_pauseFlashing(QString const& device, QString const& message) {
    auto const device_iter = _getInfoAboutDevice(device);
    if (device_iter == m_devices.end()) {
        qWarning() << QStringLiteral("There is no information about") << device;
        return;
    }

    device_iter->state = DeviceState::kFlashingPaused;
    device_iter->m_message = message;

    _updateModel();
}

void
AllwinnerDataModel::processDeviceFlashingProgressChanged(QString const& device, int progress)
{
    if (auto device_iter = _getInfoAboutDevice(device); device_iter != m_devices.end()) {
        if (progress > static_cast<int>(device_iter->progress)) {
            // Библиотека allwinner-flasher-helper (swupdate) может прислать прогресс меньше текущего, из-за этого мы
            // проверяем это значение и показываем пользователю только значения больше
            // Обсуждение:
            // https://gitlab.aqsi.ru/aqc_embedded/tools/liballwinner-flashing-helper/-/merge_requests/1#note_278249
            device_iter->progress = progress;
        }
        device_iter->state = DeviceState::kFlashingInProgress;
        _updateModel();
    } else {
        qWarning() << kErrorNotExistingDataMessage << device;
    }
}

void
AllwinnerDataModel::processDeviceFlashingFailed(QString const& device)
{
    _updateDeviceState(device, DeviceState::kFlashingFailed);
    _showElapsedTime(device);
}

void
AllwinnerDataModel::_updateDeviceState(QString const& device, DeviceState new_state)
{
    auto const device_iter = _getInfoAboutDevice(device);
    if (device_iter == m_devices.end()) {
        qWarning() << QStringLiteral("There is no information about") << device;
        return;
    }

    device_iter->state = new_state;
    _updateModel();
}

QVector<AllwinnerDataModel::DeviceInfo>::Iterator
AllwinnerDataModel::_getInfoAboutDevice(QString const& device)
{
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it->device == device) {
            return it;
        }
    }
    return m_devices.end();
}

QString
AllwinnerDataModel::_elapsedTime(size_t elapsed) const
{
    auto const hours = elapsed / 3600;
    elapsed -= hours * 3600;
    auto const mins = elapsed / 60;
    elapsed -= mins * 60;

    return QStringLiteral("%1:%2:%3")
        .arg(
            QString::number(hours).rightJustified(2, '0'), QString::number(mins).rightJustified(2, '0'),
            QString::number(elapsed).rightJustified(2, '0'));
}

void
AllwinnerDataModel::_updateModel()
{
    beginResetModel();
    endResetModel();
}

void
AllwinnerDataModel::_showElapsedTime(QString const& device)
{
    if (auto const device_iter = _getInfoAboutDevice(device); device_iter != m_devices.end()) {
        device_iter->elapsed = QDateTime::currentSecsSinceEpoch() - device_iter->started_at.toSecsSinceEpoch();
        _updateModel();
    }
}

QString
AllwinnerDataModel::_labelFromDeviceInfo(DeviceInfo const& info) const
{
    auto const device_state = QString(QMetaEnum::fromType<DeviceState>().valueToKey(static_cast<int>(info.state)));
    auto label              = QStringLiteral("Device: ") + info.device + QStringLiteral("\nState: ") +
                 device_state.right(device_state.length() - 1);

    if (info.state == DeviceState::kFlashingInProgress) {
        label += QStringLiteral("\nProgress: %1 %").arg(info.progress);
    } else if (info.state == DeviceState::kFlashingCompleted or info.state == DeviceState::kFlashingFailed) {
        label += QStringLiteral("\nElapsed time: ") + _elapsedTime(info.elapsed);
    } else if (info.state == DeviceState::kFlashingPaused) {
        label += QStringLiteral("\n") + info.m_message;
    }
    return label;
}
