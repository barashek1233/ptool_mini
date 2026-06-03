#pragma once

#include <chrono>

#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QProcess>

#include "HardwareDetection.h"

static QString const kErasingStage            = "Erasing";
static QString const kWritingImageStage       = "Writing image";
static QString const kVerifyingImageStage     = "Verifying image";
static QString const kWritingUbootStage       = "Writing u-boot";
static QString const kVerifyingUbootStage     = "Verifying u-boot";
static QString const kWritingBootstrapStage   = "Writing bootstrap";
static QString const kVerifyingBootstrapStage = "Verifying bootstrap";

class CubeFlasherManager;
class CubeFlasher : public QObject
{
    /**
     * @brief CubeFlasher прошивает куб, путь к которому передаётся в конструкторе. Прошивка начинается
     * при вызове startFlashing, об окончании прошивки говорит один из сигналов. В случае вызова terminate
     * сигнал не излучается. Если во время работы вызывается деструктор, то самба киляется (поведение QProcess)
     *
     * @example CubeFlasher *flasher = new CubeFlasher(17, "/dev/ttyACM2", this)
     *
     */
    Q_OBJECT
public:
    CubeFlasher(int cable_number, QString const& devnode, CubeFlasherManager const* manager, QObject* parent = nullptr);
    ~CubeFlasher();
    int cableNumber() const;
    QString devnode() const;

    void startFlashing();
    void terminate();

    HardwareDetection* hardwareDetection();
signals:
    void flashed(int);
    void flashingFailed(int);
    void couldNotStart(int);
    void processStageInfo(int cable, int percentage, QString const& desc);

private slots:
    void _runNextCommand();
    void _readyReadStandardError();

private:
    enum Applet {
        Nandflash,
        Bootconfig
    };

    struct Command {
        Applet applet;
        QString applet_command;
        QString applet_display;
    };

    QProcess m_process;

    QString m_devnode;
    QString m_stderr_output;
    QString m_current_command_description;
    QString m_last_stage_info_message;
    QSet<int> m_logged_stage_classes;
    QString m_hw_detection_last_error;

    QList<Command> m_commands;

    CubeFlasherManager const* m_manager{nullptr};

    HardwareDetection* m_hardware_detection;
    HardwareDetection::Revision m_hardware_revision{HardwareDetection::Revision::kUndefined};

    int m_cable_number;
    int m_last_progress_emited{-1};
    int m_next_command_index{0};

    bool m_is_process_terminated{false};  // переменная нужна на всякий случай, если функцию terminate вызовут после
    // завершения предыдущего процесса, но до начала следующего

    bool _handlePreviousOutput();
    bool _prepareCommands();
    bool _prepareCommandInManufacturingMode();
    bool _appendBootConfigCommands();
    void _appendFlashingCommands(
        QString const& uboot_file_path, QString const& at91bootstrap_file_path, QString const& rootfs_file_path);
    void _showAndLogErrorMessage(
        QString const& error_message_title, QString const& error_message_description, QString const& log_error_message);
    QString _appletToString(Applet applet);
};
