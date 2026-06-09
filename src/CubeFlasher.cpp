#include "CubeFlasher.h"

#include <chrono>

#include <QProcess>
#include <QTimer>
#include <QDir>
#include <QSettings>
#include <QDebug>
#include <QElapsedTimer>
#include <QMap>
#include <QRegularExpression>

#include "bootconfigparser.h"
#include "CubeImageInfo.h"
#include "CubeFlasherManager.h"
#include "flashingconstants.h"
#include "HardwareDetection.h"
#include "loggingCategories.h"

static constexpr auto kHardwareRevisionErrorTitle{"Hardware revision check error"};
static auto const kHardwareRevisionUndefinedMessageDescriptionMessageTemplate{
    QStringLiteral("Hardware revision is '%1'\nMake sure you have all the permissions for running sam-ba "
                   "or another app non block device.")};
static auto const kHardwareRevisionUndefinedMessageLogMessageTemplate{
    QStringLiteral("Ошибка во время проверки hw ревизии устройства. Не удалось прочесть значения с портов "
                   "либо hw ревизия не известна ('%1')")};


static QString
withPortPrefix(int cable_number, QString const& message)
{
    return QStringLiteral("Порт %1: %2").arg(QString::number(cable_number), message);
}

static QString
stageInfoMessage(QString const& stage, int cable_number)
{
    if (stage == kErasingStage) {
        return withPortPrefix(cable_number, QStringLiteral("Идет процесс стирания данных с устройства"));
    }
    if (stage == kWritingImageStage || stage == kWritingUbootStage || stage == kWritingBootstrapStage) {
        return withPortPrefix(cable_number, QStringLiteral("Идет процесс записи прошивки"));
    }
    if (stage == kVerifyingImageStage || stage == kVerifyingUbootStage || stage == kVerifyingBootstrapStage) {
        return withPortPrefix(cable_number, QStringLiteral("Идет процесс верификации прошивки"));
    }
    return withPortPrefix(cable_number, QStringLiteral("Идет процесс прошивки устройства"));
}

static int
stageInfoClass(QString const& stage)
{
    if (stage == kErasingStage) {
        return 1;
    }
    if (stage == kWritingImageStage || stage == kWritingUbootStage || stage == kWritingBootstrapStage) {
        return 2;
    }
    if (stage == kVerifyingImageStage || stage == kVerifyingUbootStage || stage == kVerifyingBootstrapStage) {
        return 3;
    }
    return 0;
}

CubeFlasher::CubeFlasher(int cable_number, QString const& devnode, CubeFlasherManager const* manager, QObject* parent)
    : QObject(parent), m_devnode(devnode), m_manager(manager), m_cable_number(cable_number)
{
    m_process.setProgram(m_manager->sambaPath());

    QString ptoolwd = QFileInfo(m_manager->sambaPath()).path();
    m_process.setWorkingDirectory(ptoolwd);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LD_LIBRARY_PATH", ptoolwd);
    m_process.setProcessEnvironment(env);

    m_hardware_detection = new HardwareDetection(&env, manager, m_devnode, this);
    connect(
        m_hardware_detection, &HardwareDetection::hardwareRevisionDetected, this,
        [this](HardwareDetection::Revision revision) {
            m_hardware_revision = revision;
            qCInfo(cat_cube_flasher).noquote()
                << QStringLiteral("Проверка HW ревизии устройства завершена. HW ревизия устройства '%1'")
                       .arg(HardwareDetection::revisionToString(revision));
        });
    connect(m_hardware_detection, &HardwareDetection::hardwareRevisionDetected, this, &CubeFlasher::startFlashing);
    connect(m_hardware_detection, &HardwareDetection::errorOccurred, this, [this](QString const& error_msg) {
        m_hw_detection_last_error = error_msg;
        qCWarning(cat_cube_flasher).noquote()
            << QStringLiteral("Ошибка во время проверки hw ревизии устройства:") << error_msg;
    });
    connect(
        &m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &CubeFlasher::_runNextCommand);
    connect(&m_process, &QProcess::readyReadStandardError, this, &CubeFlasher::_readyReadStandardError);
}

CubeFlasher::~CubeFlasher()
{
    m_is_process_terminated =
        true;  // деструктор QProcess может послать finished и мы не хотим, чтобы в этом случае отработала
    // runNextCommand
}

bool
CubeFlasher::_prepareCommands()
{
    qCDebug(cat_cube_flasher).noquote() << "Run _prepareCommands";
    if (m_manager->useManufacturingModeEnabled()) {
        qCDebug(cat_cube_flasher).noquote() << "m_manager->useManufacturingModeEnabled()) == true";
        return _prepareCommandInManufacturingMode();
    }

    using namespace flashingConstants;

    static constexpr auto kCannotChooseRootfsImageFileMessage{"Can't choose rootfs image file"};
    static constexpr auto kCannotFindImagesMessage{"Cannot find any image file in selected dir."};

    QDir images_dir(m_manager->imagesDirPath());

    auto const images_info = CubeImageInfo::scanDirectoryForImages(images_dir);
    auto const revision    = HardwareDetection::revisionToString(m_hardware_revision);

    if (images_info.isEmpty()) {
        _showAndLogErrorMessage(
            kCannotChooseRootfsImageFileMessage,
            kCannotFindImagesMessage,
            QStringLiteral("Не удалось найти rootfs файл имиджа во время подготовки к прошивке"));
        qCDebug(cat_cube_flasher).noquote() << images_dir.absolutePath();
        return false;
    }

    QString rootfs;

    if (not m_manager->usePmeccOptionEnabled()) {
        rootfs = images_info.first().filename();
    }

    for (auto const& image_info : qAsConst(images_info)) {
        if (revision.contains("cube_t", Qt::CaseInsensitive) and
            (image_info.cube().contains("cube_t", Qt::CaseInsensitive) ||
             image_info.cube().contains("cube-t", Qt::CaseInsensitive))) {
            rootfs = image_info.filename();
            break;
        } else if (
            revision.contains("cube_d", Qt::CaseInsensitive) and
            (image_info.cube().contains("cube_d", Qt::CaseInsensitive) ||
             image_info.cube().contains("cube-d", Qt::CaseInsensitive))) {
            rootfs = image_info.filename();
            break;
        }
    }
    if (rootfs.isEmpty() and !images_info.isEmpty()) {
        // In ptool_mini we do not block flashing on revision mismatch.
        rootfs = images_info.first().filename();
        qCWarning(cat_dialog).noquote()
            << QStringLiteral("HW ревизия '%1' не совпала с именем имиджа. Используем первый доступный образ: '%2'")
                   .arg(revision, rootfs);
    }

    if (rootfs.isEmpty()) {
        static constexpr auto kErrorMessage{
            "This tool is expecting three files in the image directory"
            "(provided %1): *.ubi file, at91bootstrap.bin and u-boot.bin. \n\nSome of"
            "them do not exist or hardware revision and selected image are not compatible."};

        _showAndLogErrorMessage(
            kCannotChooseRootfsImageFileMessage, kErrorMessage,
            QStringLiteral("Ошибка во время подготовки к прошивки к устройству. Путь к файлу rootfs имиджа пуст."));
        return false;
    }

    if (not _appendBootConfigCommands()) {
        return false;
    }
    auto const kPathDelimeter{"/"};
    auto const rootfs_file_path       = m_manager->imagesDirPath() + kPathDelimeter + rootfs;
    auto const uboot_file_path        = m_manager->imagesDirPath() + kPathDelimeter + kUBoot;
    auto const at91bootstap_file_path = m_manager->imagesDirPath() + kPathDelimeter + kAt91bootstrap;

    _appendFlashingCommands(uboot_file_path, at91bootstap_file_path, rootfs_file_path);

    return true;
}

bool
CubeFlasher::_prepareCommandInManufacturingMode()
{
    auto const device_hardware_revision = HardwareDetection::revisionToString(m_hardware_revision);

    auto const rootfs_file_path        = m_manager->rootfsFilePath();
    auto const uboot_file_path         = m_manager->ubootFilePath();
    auto const at91bootstrap_file_path = m_manager->at91bootstrapFilePath();
    auto const rootfs_file_name        = rootfs_file_path.mid(rootfs_file_path.lastIndexOf('/'));
    auto const case_sensitivity        = Qt::CaseInsensitive;

    if (m_manager->usePmeccOptionEnabled() and not device_hardware_revision.contains("cube", case_sensitivity)) {
        qCWarning(cat_dialog).noquote()
            << QStringLiteral("HW ревизия устройства '%1' не распознана, продолжаем прошивку принудительно")
                   .arg(device_hardware_revision);
    }

    if ((device_hardware_revision.contains("cube_t", case_sensitivity) and
             not rootfs_file_name.contains("cube_t", case_sensitivity) and
         not rootfs_file_name.contains("cube-t", case_sensitivity)) or
        (device_hardware_revision.contains("cube_d", case_sensitivity) and
             not rootfs_file_name.contains("cube_d", case_sensitivity) and
             not rootfs_file_name.contains("cube-d", case_sensitivity))) {
        qCWarning(cat_dialog).noquote()
            << QStringLiteral("Имидж '%1' не совпадает с HW ревизией '%2', продолжаем прошивку принудительно")
                   .arg(rootfs_file_name, device_hardware_revision);
        qCDebug(cat_cube_flasher).noquote() << rootfs_file_name;
        qCDebug(cat_cube_flasher).noquote() << rootfs_file_path;
    }

    if (not _appendBootConfigCommands()) {
        return false;
    }
    _appendFlashingCommands(uboot_file_path, at91bootstrap_file_path, rootfs_file_path);

    return true;
}

void
CubeFlasher::startFlashing()
{
    if (m_hardware_revision == HardwareDetection::Revision::kUndefined and m_manager->usePmeccOptionEnabled()) {
        m_hardware_detection->checkHWRevision();
        return;
    }

    if (not m_manager->usePmeccOptionEnabled()) {
        qCWarning(cat_cube_flasher).noquote() << QStringLiteral(
            "Опция 'usePMECC' отключена пользователем. Проверка соответствия HW ревизии устройства и выбранного "
            "имиджа выполнена не будет");
    }

    if (_prepareCommands()) {
        _runNextCommand();
    } else {
        static auto const error_message =
            QStringLiteral("Не удалось запустить прошивку устройства. Ошибка во время этапа подготовки к прошивке.");
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        QMetaObject::invokeMethod(
            this,
            [this]() {
                emit couldNotStart(m_cable_number);
                qCWarning(cat_cube_flasher).noquote() << error_message;
            },
            Qt::QueuedConnection);
#else
        QMetaObject::invokeMethod(this, "couldNotStart", Qt::QueuedConnection, Q_ARG(int, m_cable_number));
        qCWarning(cat_cube_flasher).noquote() << error_message;
#endif
    }
}

int
CubeFlasher::cableNumber() const
{
    return m_cable_number;
}

void
CubeFlasher::terminate()
{
    m_is_process_terminated = true;
    if (m_hardware_detection) {
        m_hardware_detection->terminate();
    }
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        qCDebug(cat_cube_flasher).noquote()
            << QStringLiteral(
                   "Попытка принудительно завершить процесс. Имя "
                   "приложения: '%1'. Устройство: '%2'. Номер "
                   "кабеля: '%3'")
                   .arg(m_process.program(), m_devnode, QString::number(m_cable_number));
        m_process.waitForFinished();
    }
}

QString
CubeFlasher::devnode() const
{
    return m_devnode;
}

HardwareDetection*
CubeFlasher::hardwareDetection()
{
    return m_hardware_detection;
}

void
CubeFlasher::_runNextCommand()
{
    if (m_is_process_terminated) {
        return;
    }
    if (m_next_command_index == m_commands.size()) {
        static auto const kDeviceFlashedMessage = QStringLiteral("Устройство успешно прошито");
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        QMetaObject::invokeMethod(
            this,
            [this]() {
                emit flashed(m_cable_number);
                qCInfo(cat_cube_flasher).noquote() << withPortPrefix(m_cable_number, kDeviceFlashedMessage);
            },
            Qt::QueuedConnection);
#else
        QMetaObject::invokeMethod(this, "flashed", Qt::QueuedConnection, Q_ARG(int, m_cable_number));
        qCInfo(cat_cube_flasher).noquote() << withPortPrefix(m_cable_number, kDeviceFlashedMessage);
#endif
        return;
    }
    if (m_next_command_index != 0) {
        if (not _handlePreviousOutput()) {
            return;
        }
    }

    Command const& command        = m_commands[m_next_command_index];
    QStringList arguments         = {"-p", "serial:" + m_devnode.split("/").last(), "-d", "sama5d2",
                                     "-a", _appletToString(command.applet),         "-c", command.applet_command};
    m_current_command_description = command.applet_display;
    m_stderr_output.clear();
    auto const stage_info_message = stageInfoMessage(m_current_command_description, m_cable_number);
    auto const stage_info_class   = stageInfoClass(m_current_command_description);

    if (!m_logged_stage_classes.contains(stage_info_class)) {
        qCInfo(cat_cube_flasher).noquote() << stage_info_message;
        m_last_stage_info_message = stage_info_message;
        m_logged_stage_classes.insert(stage_info_class);
    }

    arguments.prepend("--sam-ba");
    m_process.setArguments(arguments);

    qCDebug(cat_cube_flasher).noquote()
        << QStringLiteral("Запуск утилиты sam-ba с параметрами: %1").arg(m_process.arguments().join(','));

    m_process.start();
    if (m_next_command_index == 0) {
        auto const started = m_process.waitForStarted();
        if (not started) {
            auto const error_message =
                QStringLiteral("Не удалось запустить процесс прошивки устройства с номером кабеля '%1'. Ошибка: '%2'")
                    .arg(QString::number(m_cable_number), m_process.errorString());
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
            QMetaObject::invokeMethod(
                this,
                [this, error_message]() {
                    emit couldNotStart(m_cable_number);
                    qCWarning(cat_cube_flasher).noquote() << error_message;
                },
                Qt::QueuedConnection);
#else
            QMetaObject::invokeMethod(this, "couldNotStart", Qt::QueuedConnection, Q_ARG(int, m_cable_number));
            qCWarning(cat_cube_flasher).noquote() << error_message;
#endif
            // благодаря тому, что после испускания сигнала CubeFlasher уничтожается,
            // уничтожитя и process и можно не волноваться, что он всё-таки решит начаться
            return;
        }
    }
    ++m_next_command_index;
}

void
CubeFlasher::_readyReadStandardError()
{
    auto const stage_output = m_process.readAllStandardError();
    m_stderr_output.append(stage_output);

    static QRegularExpression const r("\\((\\d+)\\.\\d+%\\)");
    auto const match = r.match(stage_output);
    if (match.hasMatch()) {
        int const global_progress = _computeGlobalProgress(match.captured(1).toInt());
        if (global_progress != m_last_progress_emited) {
            emit processStageInfo(m_cable_number, global_progress, m_current_command_description);
            m_last_progress_emited = global_progress;
        }
    }
}

int
CubeFlasher::_computeGlobalProgress(int stage_progress) const
{
    // 7 nandflash stages in _appendFlashingCommands: erase, write×3, verify×3
    static const QMap<QString, int> stage_offsets = {
        {kErasingStage,            0},
        {kWritingImageStage,       1},
        {kVerifyingImageStage,     2},
        {kWritingUbootStage,       3},
        {kVerifyingUbootStage,     4},
        {kWritingBootstrapStage,   5},
        {kVerifyingBootstrapStage, 6},
    };
    static constexpr int kTotalStages = 7;

    auto const it = stage_offsets.constFind(m_current_command_description);
    if (it == stage_offsets.constEnd()) {
        return stage_progress;
    }
    return (it.value() * 100 + stage_progress) / kTotalStages;
}

bool
CubeFlasher::_handlePreviousOutput()
{
    if (m_stderr_output.contains("error", Qt::CaseInsensitive) or m_process.exitStatus() != QProcess::NormalExit) {
        auto const exit_code = m_process.exitCode();  // При ошибке код завершения может быть нулевым,
        // так что он бесполезен, но при проблеме вывести его не помешает
        qCWarning(cat_cube_flasher).noquote()
            << QStringLiteral(
                   "Ошибка во время работы приложения sam-ba. Код завершения работы "
                   "приложения '%1'. Вывод приложения (stdout) : '%2'")
                   .arg(QString::number(exit_code), QString(m_process.readAllStandardOutput()));
        qCWarning(cat_cube_flasher).noquote() << QStringLiteral("Ошибка на этапе: '%1'").arg(m_current_command_description);
        auto const error_message =
            QStringLiteral("Не удалось прошить устройство с номером кабеля '%1'.").arg(m_cable_number);
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        QMetaObject::invokeMethod(
            this,
            [this, error_message]() {
                emit flashingFailed(m_cable_number);
                qCWarning(cat_cube_flasher).noquote() << error_message;
            },
            Qt::QueuedConnection);

#else
        QMetaObject::invokeMethod(this, "flashingFailed", Qt::QueuedConnection, Q_ARG(int, m_cable_number));
        qCWarning(cat_cube_flasher) << error_message;
#endif
        return false;
    }

    return true;
}

bool
CubeFlasher::_appendBootConfigCommands()
{
    QDir images_dir(m_manager->imagesDirPath());
    QStringList boot_config_commands;
    {
        QFile boot_config_file(images_dir.filePath("bootconfig.txt"));
        if (boot_config_file.exists()) {
            if (!boot_config_file.open(QFile::ReadOnly)) {
                qCWarning(cat_cube_flasher).noquote()
                    << QStringLiteral("Ошибка во время обработки файла с командами для настройки boot '%1'")
                           .arg(boot_config_file.errorString());
                return false;
            }
            boot_config_commands = parseBootConfig(&boot_config_file);
        } else {
            boot_config_commands = m_manager->defaultBootConfigCommands();
        }
    }

    for (auto const& boot_config_command : qAsConst(boot_config_commands)) {
        m_commands.append({Bootconfig, boot_config_command, QStringLiteral("Boot config")});
    }
    return true;
}

void
CubeFlasher::_appendFlashingCommands(
    QString const& uboot_file_path, QString const& at91bootstrap_file_path, QString const& rootfs_file_path)
{
    m_commands.append(
        {{Nandflash, QString("erase"), kErasingStage},
         {Nandflash, QString("write:%1:0x180000").arg(rootfs_file_path), kWritingImageStage},
         {Nandflash, QString("verify:%1:0x180000").arg(rootfs_file_path), kVerifyingImageStage},
         {Nandflash, QString("write:%1:0x40000").arg(uboot_file_path), kWritingUbootStage},
         {Nandflash, QString("verify:%1:0x40000").arg(uboot_file_path), kVerifyingUbootStage},
         {Nandflash, QString("writeboot:%1").arg(at91bootstrap_file_path), kWritingBootstrapStage},
         {Nandflash, QString("verifyboot:%1").arg(at91bootstrap_file_path), kVerifyingBootstrapStage}});
}

void
CubeFlasher::_showAndLogErrorMessage(
    QString const& error_message_title, QString const& error_message_description, QString const& log_error_message)
{
    qCWarning(cat_cube_flasher).noquote() << log_error_message;
    auto error_message = error_message_description;
    if (not m_hw_detection_last_error.isEmpty()) {
        error_message += "\n\n" + m_hw_detection_last_error;
    }

    qCWarning(cat_dialog).noquote() << error_message_title << ":" << error_message;
}

QString
CubeFlasher::_appletToString(CubeFlasher::Applet applet)
{
    switch (applet) {
    case Nandflash:
        return m_manager->usePmeccOptionEnabled() ? "nandflash:2:8:0xc1807007" : "nandflash:2:8:0xc1807006";
    case Bootconfig:
        return "bootconfig";
    }
    Q_ASSERT(false);
    return {};
}
