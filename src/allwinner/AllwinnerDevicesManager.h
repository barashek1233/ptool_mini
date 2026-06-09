#pragma once

#include <cube_aw_flash.h>
#include <QSet>
#include <QMutex>
#include <QThreadPool>
#include <QHash>

class AllwinnerDevicesManager : public QObject
{
    Q_OBJECT
public:
    AllwinnerDevicesManager();
    ~AllwinnerDevicesManager();

    bool setImageFilepath(QString const& image_filepath);

    void startPolling();

    void onDeviceConnected(caf_device* libc_device, QString const& device);
    void onDeviceDisconnected(caf_device* libc_device, QString const& device);

    void onDeviceFlashingStarted(QString const& device);
    void onDeviceFlashingResumed(QString const& device);
    void onDeviceFlashingCompleted(QString const& device);
    void onDeviceFlashingFailed(QString const& device);
    void onDeviceFlashingProgressChanged(QString const& device, int progress);
    void onDeviceManualPowerCycleRequested(QString const& device);
    void onDeviceFelJumperRemovalRequested(QString const& device);
    void onDeviceLibraryLog(QString const& device, caf_loglevel log_level, QString const& message);

signals:
    void deviceConnected(QString const& device);
    void deviceDisconnected(QString const& device);

    void deviceFlashingStarted(QString const& device);
    void deviceFlashingResumed(QString const& device);
    void deviceFlashingCompleted(QString const& device);
    void deviceFlashingFailed(QString const& device);
    void deviceFlashingProgressChanged(QString const& device, int progress);
    void deviceManualPowerCycleRequested(QString const& device);
    void deviceFelJumperRemovalRequested(QString const& device);

    void logMessage(QString const& message);

private:
    struct AllwinnerLibrary {
        ~AllwinnerLibrary();
        bool isValid() const;
        bool init(AllwinnerDevicesManager* manager);

    private:

        bool m_valid{false};
    };

    AllwinnerLibrary m_library;
    QThreadPool m_pool;
    QSet<caf_device*> m_connected_devices;
    QMutex m_connected_devices_mx;
    bool m_image_loaded{false};

    QHash<QString, int> m_device_to_cable;
    QHash<QString, QSet<int>> m_logged_stage_classes;
    QSet<QString> m_waiting_for_resume_devices;
    int m_next_cable_number{1};

    int _ensureCableNumber(QString const& device);
    int _cableNumber(QString const& device) const;
    static int _stageInfoClass(QString const& stage_message);
    static QString _withPortPrefix(int cable_number, QString const& message);
    void _logStageIfNeeded(QString const& device, QString const& stage_message);
};
