#pragma once

#include <QAbstractTableModel>
#include <QDateTime>

class AllwinnerDevicesManager;

class AllwinnerDataModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum class DeviceState
    {
        kConnected,
        kDisconnected,
        kFlashingStarted,
        kFlashingPaused,
        kFlashingInProgress,
        kFlashingCompleted,
        kFlashingFailed
    };
    Q_ENUM(DeviceState)

    explicit AllwinnerDataModel(AllwinnerDevicesManager& devices_manager, QObject* parent = nullptr);

    int rowCount(QModelIndex const&) const override;
    int columnCount(QModelIndex const&) const override;

    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;

    void processDeviceConnected(QString const&);
    void processDeviceDisconnected(QString const&);
    void processDeviceFlashingStarted(QString const& device);
    void processDeviceFlashingResumed(QString const& device);
    void processDeviceFlashingFailed(QString const& device);
    void processDeviceFlashingCompleted(QString const& device);
    void processDeviceFlashingProgressChanged(QString const& device, int progress);
    void processDeviceManualPowerCycleRequested(QString const& device);
    void processDeviceFelJumperRemovalRequested(QString const& device);

private:
    static constexpr int ColumnsPerRow = 7;
    
    struct DeviceInfo {
        QString device;
        DeviceState state;
        QDateTime started_at{QDateTime::currentDateTime()};

        size_t progress{0};
        size_t elapsed{0};

        QString m_message{};
    };

    QVector<DeviceInfo> m_devices;

    void _updateModel();
    QString _labelFromDeviceInfo(DeviceInfo const& info) const;

    void _showElapsedTime(QString const& device);
    void _updateDeviceState(QString const& device, DeviceState new_state);
    void _pauseFlashing(QString const& device, QString const& message);
    QVector<DeviceInfo>::Iterator _getInfoAboutDevice(QString const& device);
    QString _elapsedTime(size_t elapsed) const;
};
