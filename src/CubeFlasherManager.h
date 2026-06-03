#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QSettings>
#include <QSharedPointer>
#include <QTimer>
#include <QElapsedTimer>

class CubeFlasher;

class CubeFlasherManager : public QObject
{
    /**
     * @brief Этот класс создаёт и уничтожает отдельные объекты CubeFlasher, когда нужно прошить куб
     */

    Q_OBJECT
public:
    explicit CubeFlasherManager(QObject* parent = nullptr);

    QStringList connectedDevices() const;
    QString sambaPath() const;
    QString ubootFilePath() const;
    QString at91bootstrapFilePath() const;
    QString rootfsFilePath() const;
    QString imagesDirPath() const;

    bool usePmeccOptionEnabled() const;
    bool useManufacturingModeEnabled() const;

    void stopFlashingAllDevices();
    void suppressAutoFlashing();
    void releaseAutoFlashing();
    bool isAutoFlashingSuppressed() const;
    void suppressAutoFlashingForDevice(QString const& device);
    void releaseAutoFlashingForDevice(QString const& device);
    bool isAutoFlashingSuppressedForDevice(QString const& device) const;

    static QStringList defaultBootConfigCommands();

signals:
    void deviceFlashedCompleted(int cable_number);
    void deviceFlashedFailed(int cable_number);
    void deviceFlashingCouldNotStart(int cable_number);
    void deviceDisconnected(int cable_number);

    void deviceFlashingInfoChanged(int cable_number, int percentage, QString const& description);
    void connectedDevicesListChanged(QStringList const& devices_list);

public slots:
    void onDeviceConnected(int cable_number, QString const& device);
    void onDeviceDisconnected(int cable_number);

    void onImagesDirPathChanged(QString const& images_dir);

    void onFirmwarePathsChanged(
        QString const& rootfs_file_path, QString const& uboot_file_path, QString const& at91bootstrap_file_path);

    void onHardwareRevisionDetectionWaitingTimeoutChanged(std::chrono::milliseconds const& timeout);
    void onUsePmeccOptionChanged(bool option_state);
    void onUseManufacturingModeChanged(bool state);

    void startFlashing(int cable_number, QString const& device);
    void retryStartFlashing(int cable_number, QString const& device);

private:
    struct RetryState {
        QTimer* timer{nullptr};
        QElapsedTimer elapsed;
        QString device;
    };

    QSettings m_settings;

    QString m_uboot_file_path;
    QString m_at91bootstrap_path;
    QString m_rootfs_path;
    QString m_images_dir;

    QHash<int, QString> m_devices_to_cables;
    QSet<QSharedPointer<CubeFlasher>> m_flashers;
    QHash<int, RetryState> m_retry_states;
    QHash<QString, int> m_suppressed_devices;
    int m_global_auto_flashing_suppression_count{0};

    std::chrono::milliseconds m_hardware_revision_detection_waiting_timeout;

    bool m_use_pmecc_option_enabled{true};
    bool m_use_manufacturing_mode_enabled{false};

    void _scheduleRetry(int cable_number, QString const& device);
    void _cancelRetry(int cable_number);
    void _cancelAllRetries();
    void _stopFlashing(int cable_number);
    bool _isDeviceConnected(int cable_number) const;
    bool _isFlashing(int cable_number) const;

private slots:
    void _onFlasherCompleted(int cable_number);
    void _onFlasherFailed(int cable_number);
    void _onFlasherCouldNotStart(int cable_number);
};
