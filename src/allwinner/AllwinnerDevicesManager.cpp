#include "AllwinnerDevicesManager.h"
#include "LogMessageHandlers.h"
#include "ConfigManager.h"
#include "loggingCategories.h"

#include <cube_aw_flash.h>

#include <QDebug>
#include <QDateTime>
#include <QSocketNotifier>
#include <QAbstractEventDispatcher>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrent>
#include <fcntl.h>
#include <unistd.h>

namespace {
class ScopedFdRedirect
{
public:
    explicit ScopedFdRedirect(int target_fd)
        : m_target_fd(target_fd)
    {
        m_saved_fd = ::dup(m_target_fd);
        if (m_saved_fd < 0) {
            return;
        }

        int null_fd = ::open("/dev/null", O_WRONLY);
        if (null_fd < 0) {
            ::close(m_saved_fd);
            m_saved_fd = -1;
            return;
        }

        if (::dup2(null_fd, m_target_fd) < 0) {
            ::close(m_saved_fd);
            m_saved_fd = -1;
        }
        ::close(null_fd);
    }

    ~ScopedFdRedirect()
    {
        if (m_saved_fd >= 0) {
            ::dup2(m_saved_fd, m_target_fd);
            ::close(m_saved_fd);
        }
    }

private:
    int m_target_fd{-1};
    int m_saved_fd{-1};
};
} // namespace

void
onDeviceProgress(caf_device* device, int progress, void* user_data)
{
    auto *manager = static_cast<AllwinnerDevicesManager*>(user_data);
    auto const device_name = caf_device_sysname(device);

    manager->onDeviceFlashingProgressChanged(device_name, progress);
}

void
onDeviceLog(caf_device* device, caf_loglevel log_level, char const* message, void* user_data)
{
    char const* chip           = caf_device_chip(device);
    char const* device_sysname = caf_device_sysname(device);

    if (chip == NULL) {
        chip = "-";
    }

    auto* manager = static_cast<AllwinnerDevicesManager*>(user_data);
    if (manager && message != nullptr) {
        manager->onDeviceLibraryLog(device_sysname, log_level, QString::fromUtf8(message));
    }

    // Игнорируем предупреждение об отсутствии таблицы разделов — это нормальная ситуация
    QString msg_str = QString::fromUtf8(message);
    if (msg_str.contains("does not contain a recogniz") && msg_str.contains("partition table")) {
        return;  // Не логируем это сообщение
    }

    // Логируем только ошибки и критические сообщения от библиотеки
    switch (log_level) {
    case caf_loglevel::kCafLoglevelError: {
        qWarning() << chip << device_sysname << message;
        break;
    }
    case caf_loglevel::kCafLoglevelWarn: {
        qWarning() << chip << device_sysname << message;
        break;
    }
    default:
        // Debug и Info сообщения от библиотеки не логируем в консоль
        break;
    }
}

void
onDeviceEvent(caf_device* device, caf_event_type type, void* user_data)
{
    auto *manager = static_cast<AllwinnerDevicesManager*>(user_data);
    auto const device_name = caf_device_sysname(device);

    static auto const l_cafEventToString = [](caf_event_type type) -> QString {
        switch (type) {
        case caf_event_type::kCafDeviceArrived: {
            return QStringLiteral("Device connected");
        }
        case caf_event_type::kCafDeviceLeft: {
            return QStringLiteral("Device disconnected");
        }
        case caf_event_type::kCafDeviceFlashingFailed: {
            return QStringLiteral("Device flashing failed");
        }
        case caf_event_type::kCafDeviceFlashingStarted: {
            return QStringLiteral("Device flashing started");
        }
        case caf_event_type::kCafDeviceFlashingCompleted: {
            return QStringLiteral("Device flasing completed");
        }
        case caf_event_type::kCafDeviceFlashingResumed: {
            return QStringLiteral("Flashing resumed");
        }
        case caf_event_type::kCafDeviceInitFailed: {
            return QStringLiteral("Device failed to initialize");
        }
        case caf_event_type::kCafDeviceManualPowerCycleRequested: {
            return QStringLiteral("Manual power cycle requested");
        }
        case caf_event_type::kCafDeviceFelJumperRemovalRequested: {
            return QStringLiteral("FEL jumper removal requested");
        }
        }
        return "Unknown event";
    };

    switch (type) {
    case caf_event_type::kCafDeviceArrived: {
        caf_device_ref(device);
        manager->onDeviceConnected(device, device_name);
        break;
    }
    case caf_event_type::kCafDeviceFlashingStarted: {
        manager->onDeviceFlashingStarted(device_name);
        break;
    }
    case caf_event_type::kCafDeviceFlashingResumed: {
        manager->onDeviceFlashingResumed(device_name);
        break;
    }
    case caf_event_type::kCafDeviceInitFailed:
    case caf_event_type::kCafDeviceFlashingFailed: {
        manager->onDeviceFlashingFailed(device_name);
        break;
    }
    case caf_event_type::kCafDeviceFlashingCompleted: {
        manager->onDeviceFlashingCompleted(device_name);
        break;
    }
    case caf_event_type::kCafDeviceLeft: {
        manager->onDeviceDisconnected(device, device_name);
        caf_device_unref(device);
        break;
    }
    case caf_event_type::kCafDeviceManualPowerCycleRequested:
        manager->onDeviceManualPowerCycleRequested(device_name);
        break;
    case caf_event_type::kCafDeviceFelJumperRemovalRequested:
        manager->onDeviceFelJumperRemovalRequested(device_name);
        break;
    }
}

static int
parseCableNumber(QString const& device)
{
    // Try to parse the last numeric segment from sysfs-like names: "3-1.4.2" -> 2
    auto const match = QRegularExpression(QStringLiteral("(\\d+)(?!.*\\d)")).match(device);
    if (match.hasMatch()) {
        bool ok = false;
        auto const parsed = match.captured(1).toInt(&ok);
        if (ok) {
            return parsed;
        }
    }
    return -1;
}

static QString
detectStageMessage(QString const& raw_message)
{
    auto const msg = raw_message.toLower();

    if (msg.contains("erase")) {
        return QStringLiteral("Идет процесс стирания данных с устройства");
    }
    if (msg.contains("verify")) {
        return QStringLiteral("Идет процесс верификации прошивки");
    }
    if (msg.contains("write") || msg.contains("flash") || msg.contains("install")) {
        return QStringLiteral("Идет процесс записи прошивки");
    }
    return {};
}

bool
AllwinnerDevicesManager::setImageFilepath(QString const& image_filepath)
{
    if (!m_library.init(this))
        return false;

    int r;
    {
        // caf_load_image() prints noisy CAF: lines directly to stdio.
        ScopedFdRedirect mute_stdout(STDOUT_FILENO);
        ScopedFdRedirect mute_stderr(STDERR_FILENO);
        r = caf_load_image(image_filepath.toStdString().c_str());
    }
    if (r) {
        qCritical() << "Failed to load image " << image_filepath;
        return false;
    }

    // Start flashing all currenly connected devices
    QMutexLocker lock(&m_connected_devices_mx);
    m_image_loaded = true;
    for (auto device : m_connected_devices)
        QtConcurrent::run(&m_pool, caf_device_install_image, device);
    m_connected_devices.clear();
    return true;
}

void
AllwinnerDevicesManager::startPolling()
{
    if (!m_image_loaded)
        return;

    if (!m_library.init(this))
        return;

    if (caf_enable_receiving())
        qFatal("Error in caf_enable_receiving()");

    // Handle library events in the main loop
    auto n = new QSocketNotifier(caf_get_fd(), QSocketNotifier::Read, this);
    QObject::connect(n, &QSocketNotifier::activated, [](int socket) {
        caf_handle_events();
    });
    QAbstractEventDispatcher::instance()->registerSocketNotifier(n);
}

AllwinnerDevicesManager::~AllwinnerDevicesManager()
{
    // Members are destroyed after this body runs. Background threads (caf_device_install_image)
    // call callbacks that access m_waiting_for_resume_devices, m_logged_stage_classes, etc.
    // Wait for all threads to finish before those members are destroyed.
    m_pool.waitForDone();
}

AllwinnerDevicesManager::AllwinnerDevicesManager() : QObject()
{
    static auto const kMaxConcurrentlyFlashedDevices = 64;

    m_pool.setExpiryTimeout(0);
    m_pool.setMaxThreadCount(kMaxConcurrentlyFlashedDevices);
}

void
AllwinnerDevicesManager::onDeviceConnected(caf_device* libc_device, QString const& device)
{
    auto const cable = _ensureCableNumber(device);
    m_pre_started_devices.insert(device);
    qCInfo(cat_cube_flasher).noquote()
        << QStringLiteral("Подключено новое устройство. Номер кабеля: '%1', имя устройства: '%2'")
               .arg(QString::number(cable), device);

    emit deviceConnected(device);

    QMutexLocker lock(&m_connected_devices_mx);
    if (m_image_loaded) {
        lock.unlock();
        QtConcurrent::run(&m_pool, caf_device_install_image, libc_device);
    } else {
        m_connected_devices.insert(libc_device);
    }
}

void
AllwinnerDevicesManager::onDeviceDisconnected(caf_device *libc_device, QString const& device)
{
    auto const cable = _ensureCableNumber(device);
    qCInfo(cat_cube_flasher).noquote()
        << QStringLiteral("Устройство с номером кабеля '%1' было отключено").arg(QString::number(cable));

    emit deviceDisconnected(device);

    QMutexLocker lock(&m_connected_devices_mx);
    m_connected_devices.remove(libc_device);
    m_logged_stage_classes.remove(device);
    m_pre_started_devices.remove(device);
    m_waiting_for_resume_devices.remove(device);
}

void
AllwinnerDevicesManager::onDeviceFlashingStarted(QString const& device)
{
    auto const cable = _ensureCableNumber(device);
    m_logged_stage_classes.remove(device);
    m_pre_started_devices.remove(device);
    m_waiting_for_resume_devices.insert(device);
    qCInfo(cat_cube_flasher).noquote()
        << QStringLiteral("Запущен процесс прошивки устройства. Номер кабеля: %1. Имя устройства: '%2'")
               .arg(QString::number(cable), device);
    emit deviceFlashingStarted(device);
}

void
AllwinnerDevicesManager::onDeviceFlashingResumed(QString const& device)
{
    m_waiting_for_resume_devices.remove(device);
    _logStageIfNeeded(device, QStringLiteral("Идет процесс стирания данных с устройства"));
    emit deviceFlashingResumed(device);
}

void
AllwinnerDevicesManager::onDeviceFlashingCompleted(QString const& device)
{
    m_waiting_for_resume_devices.remove(device);
    _logStageIfNeeded(device, QStringLiteral("Идет процесс верификации прошивки"));
    emit deviceFlashingCompleted(device);
}

void
AllwinnerDevicesManager::onDeviceFlashingFailed(QString const& device)
{
    m_waiting_for_resume_devices.remove(device);
    auto const cable = _cableNumber(device);
    qCWarning(cat_dialog).noquote()
        << QStringLiteral("Не удалось начать прошивку устройства '%1'. Снимите перемычку.").arg(device);
    qCWarning(cat_cube_flasher).noquote()
        << QStringLiteral("Не удалось прошить устройство с номером кабеля '%1'.").arg(QString::number(cable));
    emit deviceFlashingFailed(device);
}

void
AllwinnerDevicesManager::onDeviceFlashingProgressChanged(QString const& device, int progress)
{
    if (m_waiting_for_resume_devices.contains(device)) {
        emit deviceFlashingProgressChanged(device, progress);
        return;
    }

    if (progress > 0) {
        _logStageIfNeeded(device, QStringLiteral("Идет процесс записи прошивки"));
    }
    if (progress >= 95) {
        _logStageIfNeeded(device, QStringLiteral("Идет процесс верификации прошивки"));
    }
    emit deviceFlashingProgressChanged(device, progress);
}

void AllwinnerDevicesManager::onDeviceManualPowerCycleRequested(QString const& device) {
    qCWarning(cat_dialog).noquote()
        << QStringLiteral("Требуется ручной перезапуск питания устройства: '%1'").arg(device);
    emit deviceManualPowerCycleRequested(device);
}

void AllwinnerDevicesManager::onDeviceFelJumperRemovalRequested(QString const& device) {
    qCWarning(cat_dialog).noquote()
        << QStringLiteral("Снимите FEL-перемычку на устройстве: '%1'").arg(device);
    emit deviceFelJumperRemovalRequested(device);
}

AllwinnerDevicesManager::AllwinnerLibrary::~AllwinnerLibrary()
{
    if (isValid()) {
        caf_destroy();
    }
}

bool
AllwinnerDevicesManager::AllwinnerLibrary::isValid() const
{
    return m_valid;
}

bool
AllwinnerDevicesManager::AllwinnerLibrary::init(AllwinnerDevicesManager* manager)
{
    if (m_valid)
        return true;

    assert(manager);

    m_valid = (caf_init(kCafAllowManualPowerCycling) == 0);
    if (m_valid) {
        qInfo() << "Allwinner flashing library" << caf_get_version_string() << "has been successfully initialized";

        caf_set_user_data(manager);
        auto const log_level = ConfigManager::instance()->logLevel();
        caf_set_loglevel(log_level == "debug" ? caf_loglevel::kCafLoglevelDebug : caf_loglevel::kCafLoglevelInfo);
        caf_set_log_callback(onDeviceLog);
        caf_set_event_callback(onDeviceEvent);
        caf_set_progress_callback(onDeviceProgress);

    } else {
        qWarning() << "Failed to initialize Allwinner flashing library" << caf_get_version_string();
    }
    return m_valid;
}

void
AllwinnerDevicesManager::onDeviceLibraryLog(QString const& device, caf_loglevel, QString const& message)
{
    if (m_pre_started_devices.contains(device) || m_waiting_for_resume_devices.contains(device)) {
        return;
    }
    auto const stage_message = detectStageMessage(message);
    if (!stage_message.isEmpty()) {
        _logStageIfNeeded(device, stage_message);
    }
}

int
AllwinnerDevicesManager::_ensureCableNumber(QString const& device)
{
    auto it = m_device_to_cable.find(device);
    if (it != m_device_to_cable.end()) {
        return it.value();
    }

    auto parsed = parseCableNumber(device);
    if (parsed <= 0) {
        parsed = m_next_cable_number++;
    } else if (parsed >= m_next_cable_number) {
        m_next_cable_number = parsed + 1;
    }

    m_device_to_cable.insert(device, parsed);
    return parsed;
}

int
AllwinnerDevicesManager::cableNumber(QString const& device) const
{
    return _cableNumber(device);
}

int
AllwinnerDevicesManager::_cableNumber(QString const& device) const
{
    auto it = m_device_to_cable.find(device);
    if (it == m_device_to_cable.end()) {
        return 0;
    }
    return it.value();
}

int
AllwinnerDevicesManager::_stageInfoClass(QString const& stage_message)
{
    if (stage_message == QStringLiteral("Идет процесс стирания данных с устройства")) {
        return 1;
    }
    if (stage_message == QStringLiteral("Идет процесс записи прошивки")) {
        return 2;
    }
    if (stage_message == QStringLiteral("Идет процесс верификации прошивки")) {
        return 3;
    }
    return 0;
}

QString
AllwinnerDevicesManager::_withPortPrefix(int cable_number, QString const& message)
{
    return QStringLiteral("Порт %1: %2").arg(QString::number(cable_number), message);
}

void
AllwinnerDevicesManager::_logStageIfNeeded(QString const& device, QString const& stage_message)
{
    auto const cable = _ensureCableNumber(device);
    auto const stage_class = _stageInfoClass(stage_message);
    if (stage_class <= 0) {
        qCInfo(cat_cube_flasher).noquote() << _withPortPrefix(cable, stage_message);
        return;
    }

    auto& logged = m_logged_stage_classes[device];
    if (logged.contains(stage_class)) {
        return;
    }
    logged.insert(stage_class);
    qCInfo(cat_cube_flasher).noquote() << _withPortPrefix(cable, stage_message);
}
