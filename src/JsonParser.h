#pragma once

#include <QObject>
#include <QJsonValue>
#include <QSettings>
#include <QVector>

class JsonParser : public QObject
{
    Q_OBJECT
public:
    explicit JsonParser(QObject* parent = nullptr);

    struct Firmware {
        QString m_image_type;            // cube-image / cube-dev-image
        QString m_device_type;           // cube-t-c / cube-d
        QString m_firmware_description;  // 1.0.6-rc43 ...
        QString m_directory;
        QString m_uboot_file_path;
        QString m_at91bootstrap_file_path;
        QString m_rootfs_file_path;
    };

    QVector<Firmware> processConfigFile(QString const& config_file_path, bool show_message_boxes = true);
    QVector<Firmware> availableFirmware() const;
signals:
    void firmwareUpdated(QVector<Firmware> const&);

private:
    QSettings m_settings;
    QVector<Firmware> m_firmware;

    void printProcessedFields(JsonParser::Firmware const& firmware) const;

    Firmware _processRecord(QJsonObject const& object) const;
};
