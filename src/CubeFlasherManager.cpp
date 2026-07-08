#include "CubeFlasherManager.h"

#include "CubeFlasher.h"
#include "loggingCategories.h"
#include "flashingconstants.h"

CubeFlasherManager::CubeFlasherManager(QObject* parent) : QObject{parent}
{
    using namespace flashingConstants;

    static constexpr auto kInvalidTimeoutValue{-1};

    m_images_dir = m_settings.value(kImagesDirSettingsKey).toString();

    auto const setting_value =
        m_settings.value(kTimeoutForHardwareDetectionSettingsKey, kInvalidTimeoutValue).toLongLong();

    m_hardware_revision_detection_waiting_timeout = setting_value == kInvalidTimeoutValue
                                                        ? HardwareDetection::defaultTimeoutForHardwareDetection()
                                                        : std::chrono::milliseconds(setting_value);
}

void
CubeFlasherManager::stopFlashingAllDevices()
{
    _cancelAllRetries();
    qCInfo(cat_cube_flasher).noquote() << QStringLiteral("Остановка прошивки всех устройств");
    while (not m_flashers.isEmpty()) {
        auto i = m_flashers.begin();
        (*i)->terminate();
        m_flashers.erase(i);
    }
}

void
CubeFlasherManager::suppressAutoFlashing()
{
    ++m_global_auto_flashing_suppression_count;
    qCInfo(cat_cube_flasher).noquote() << QStringLiteral("Автопрошивка временно отключена");
    stopFlashingAllDevices();
}

void
CubeFlasherManager::releaseAutoFlashing()
{
    if (m_global_auto_flashing_suppression_count <= 0) {
        return;
    }

    --m_global_auto_flashing_suppression_count;
    if (m_global_auto_flashing_suppression_count == 0) {
        qCInfo(cat_cube_flasher).noquote() << QStringLiteral("Автопрошивка снова включена");
    }
}

bool
CubeFlasherManager::isAutoFlashingSuppressed() const
{
    return m_global_auto_flashing_suppression_count > 0;
}

void
CubeFlasherManager::suppressAutoFlashingForDevice(QString const& device)
{
    if (device.isEmpty()) {
        return;
    }

    auto& counter = m_suppressed_devices[device];
    ++counter;
    qCInfo(cat_cube_flasher).noquote()
        << QStringLiteral("Автопрошивка временно отключена для устройства '%1'").arg(device);

    for (auto it = m_devices_to_cables.cbegin(); it != m_devices_to_cables.cend(); ++it) {
        if (it.value() == device) {
            _cancelRetry(it.key());
            _stopFlashing(it.key());
            break;
        }
    }
}

void
CubeFlasherManager::releaseAutoFlashingForDevice(QString const& device)
{
    auto it = m_suppressed_devices.find(device);
    if (it == m_suppressed_devices.end()) {
        return;
    }

    --it.value();
    if (it.value() <= 0) {
        m_suppressed_devices.erase(it);
        qCInfo(cat_cube_flasher).noquote()
            << QStringLiteral("Автопрошивка снова включена для устройства '%1'").arg(device);
    }
}

bool
CubeFlasherManager::isAutoFlashingSuppressedForDevice(QString const& device) const
{
    if (isAutoFlashingSuppressed()) {
        return true;
    }
    auto const it = m_suppressed_devices.find(device);
    return it != m_suppressed_devices.end() && it.value() > 0;
}

QStringList
CubeFlasherManager::defaultBootConfigCommands()
{
    static QStringList const kDefaultBootConfigCommands{
        "writecfg:bscr:0",
        "writecfg:fuse:qspi0_disabled,qspi1_disabled,spi0_disabled,spi1_disabled,nfc_ioset2,sdmmc0_disabled,sdmmc1,"
        "uart1_ioset1,jtag_ioset3,ext_mem_boot"};
    return kDefaultBootConfigCommands;
}

QStringList
CubeFlasherManager::connectedDevices() const
{
    return m_devices_to_cables.values();
}

QString
CubeFlasherManager::sambaPath() const
{
    // sam-ba интегрирована в ptool, запускается командой `ptool --sam-ba [параметры sam-ba]`
    return QCoreApplication::applicationFilePath();
}

QString
CubeFlasherManager::ubootFilePath() const
{
    return m_uboot_file_path;
}

QString
CubeFlasherManager::at91bootstrapFilePath() const
{
    return m_at91bootstrap_path;
}

QString
CubeFlasherManager::rootfsFilePath() const
{
    qCDebug(cat_cube_flasher).noquote() << m_rootfs_path;
    return m_rootfs_path;
}

QString
CubeFlasherManager::imagesDirPath() const
{
    return m_images_dir;
}

bool
CubeFlasherManager::usePmeccOptionEnabled() const
{
    return m_use_pmecc_option_enabled;
}

bool
CubeFlasherManager::useManufacturingModeEnabled() const
{
    return m_use_manufacturing_mode_enabled;
}

void
CubeFlasherManager::onDeviceConnected(int cable_number, QString const& device)
{
    qCInfo(cat_cube_flasher).noquote() << QStringLiteral(
                                              "Подключено новое устройство. Номер кабеля: '%1', имя устройства: '%2'")
                                              .arg(QString::number(cable_number), device);
    m_devices_to_cables[cable_number] = device;
    emit connectedDevicesListChanged(connectedDevices());
    _cancelRetry(cable_number);
    if (isAutoFlashingSuppressedForDevice(device)) {
        qCInfo(cat_cube_flasher).noquote()
            << QStringLiteral("Автопрошивка пропущена для устройства '%1': выполняется сервисная операция").arg(device);
        return;
    }
    startFlashing(cable_number, device);
}

void
CubeFlasherManager::onDeviceDisconnected(int cable_number)
{
    qCInfo(cat_cube_flasher).noquote()
        << QStringLiteral("Устройство с номером кабеля '%1' было отключено").arg(QString::number(cable_number));

    _stopFlashing(cable_number);
    _cancelRetry(cable_number);
    m_devices_to_cables.remove(cable_number);
    emit connectedDevicesListChanged(connectedDevices());
    emit deviceDisconnected(cable_number);
}

void
CubeFlasherManager::onImagesDirPathChanged(QString const& images_dir)
{
    m_images_dir = images_dir;
    m_settings.setValue(flashingConstants::kImagesDirSettingsKey, images_dir);
}

void
CubeFlasherManager::onFirmwarePathsChanged(
    QString const& rootfs_file_path, QString const& uboot_file_path, QString const& at91bootstrap_file_path)
{
    m_rootfs_path        = rootfs_file_path;
    m_uboot_file_path    = uboot_file_path;
    m_at91bootstrap_path = at91bootstrap_file_path;
}

void
CubeFlasherManager::onHardwareRevisionDetectionWaitingTimeoutChanged(std::chrono::milliseconds const& timeout)
{
    m_hardware_revision_detection_waiting_timeout = timeout;
    m_settings.setValue(
        kTimeoutForHardwareDetectionSettingsKey,
        static_cast<long long>(m_hardware_revision_detection_waiting_timeout.count()));
}

void
CubeFlasherManager::onUsePmeccOptionChanged(bool option_state)
{
    m_use_pmecc_option_enabled = option_state;
}

void
CubeFlasherManager::onUseManufacturingModeChanged(bool state)
{
    m_use_manufacturing_mode_enabled = state;
}

void
CubeFlasherManager::startFlashing(int cable_number, QString const& device)
{
    if (isAutoFlashingSuppressedForDevice(device)) {
        qCInfo(cat_cube_flasher).noquote()
            << QStringLiteral("Запуск прошивки пропущен для устройства '%1': выполняется сервисная операция").arg(device);
        return;
    }
    if (_isFlashing(cable_number)) {
        qCDebug(cat_cube_flasher).noquote()
            << QStringLiteral("Прошивка устройства с номером кабеля '%1' уже в процессе").arg(QString::number(cable_number));
        return;
    }
    qCInfo(cat_cube_flasher).noquote()
        << QStringLiteral("Запущен процесс прошивки устройства. Номер кабеля: %1. Имя устройства: '%2'")
               .arg(QString::number(cable_number), device);

    // No Qt parent: flasher lifetime is managed solely by the shared_ptr in m_flashers.
    // Passing `this` as parent would cause double-delete when m_flashers is destroyed
    // before QObject::~QObject() runs its children cleanup.
    auto const flasher = new CubeFlasher(cable_number, device, this, nullptr);

    connect(flasher, &CubeFlasher::flashed, this, &CubeFlasherManager::_onFlasherCompleted);
    connect(flasher, &CubeFlasher::flashingFailed, this, &CubeFlasherManager::_onFlasherFailed);
    connect(flasher, &CubeFlasher::couldNotStart, this, &CubeFlasherManager::_onFlasherCouldNotStart);
    connect(flasher, &CubeFlasher::processStageInfo, this, &CubeFlasherManager::deviceFlashingInfoChanged);

    QSharedPointer<CubeFlasher> shared(flasher);
    QWeakPointer<CubeFlasher> weak(shared);

    auto const l_processFlashingFinished = [this, weak]() {
        m_flashers.remove(weak);
    };

    connect(flasher, &CubeFlasher::couldNotStart, this, l_processFlashingFinished);
    connect(flasher, &CubeFlasher::flashed, this, l_processFlashingFinished);
    connect(flasher, &CubeFlasher::flashingFailed, this, l_processFlashingFinished);

    flasher->hardwareDetection()->setTimeoutForFailedHardwareDetection(m_hardware_revision_detection_waiting_timeout);
    flasher->startFlashing();
    m_flashers.insert(shared);
    qCDebug(cat_cube_flasher).noquote() << QStringLiteral("Текущее количество прошиваемых устройств: ")
                                        << m_flashers.size();
}

void
CubeFlasherManager::retryStartFlashing(int cable_number, QString const& device)
{
    qCDebug(cat_cube_flasher).noquote()
        << QStringLiteral("Повторная попытка прошивки устройства. Номер кабеля: %1. Имя устройства: '%2'")
               .arg(QString::number(cable_number), device);
    onDeviceConnected(cable_number, device);
}

void
CubeFlasherManager::_scheduleRetry(int cable_number, QString const& device)
{
    static constexpr auto kRetryInterval = std::chrono::seconds(5);
    static constexpr auto kRetryTotalTimeout = std::chrono::minutes(2);
    auto const retry_interval_ms =
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(kRetryInterval).count());
    auto const retry_total_timeout_ms =
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(kRetryTotalTimeout).count());

    if (not _isDeviceConnected(cable_number)) {
        return;
    }
    if (isAutoFlashingSuppressedForDevice(device)) {
        qCInfo(cat_cube_flasher).noquote()
            << QStringLiteral("Повтор прошивки пропущен для устройства '%1': выполняется сервисная операция").arg(device);
        return;
    }

    auto& state = m_retry_states[cable_number];
    state.device = device;
    if (state.timer == nullptr) {
        state.timer = new QTimer(this);
        state.timer->setSingleShot(true);
        state.elapsed.start();
        connect(state.timer, &QTimer::timeout, this, [this, cable_number]() {
            auto it = m_retry_states.find(cable_number);
            if (it == m_retry_states.end()) {
                return;
            }
            auto& retry_state = it.value();

            if (not _isDeviceConnected(cable_number)) {
                _cancelRetry(cable_number);
                return;
            }

            if (retry_state.elapsed.hasExpired(retry_total_timeout_ms)) {
                qCWarning(cat_cube_flasher).noquote()
                    << QStringLiteral("Повторы прошивки для кабеля '%1' отменены: истёк лимит 2 мин.").arg(QString::number(cable_number));
                _cancelRetry(cable_number);
                return;
            }

            if (_isFlashing(cable_number)) {
                retry_state.timer->start(retry_interval_ms);
                return;
            }

            if (isAutoFlashingSuppressedForDevice(retry_state.device)) {
                qCInfo(cat_cube_flasher).noquote()
                    << QStringLiteral("Повторный запуск прошивки пропущен для устройства '%1': выполняется сервисная операция")
                           .arg(retry_state.device);
                retry_state.timer->start(retry_interval_ms);
                return;
            }

            qCDebug(cat_cube_flasher).noquote()
                << QStringLiteral("Повторный запуск прошивки. Кабель: %1, устройство: %2")
                       .arg(QString::number(cable_number), retry_state.device);
            // Намеренно не вызываем _cancelRetry здесь: это стёрло бы retry_state.elapsed
            // и 2-минутный лимит на повторы никогда бы не срабатывал (сбрасывался бы на
            // каждой попытке). Состояние очищается только при реальном успехе/отключении/
            // переподключении устройства (см. _onFlasherCompleted, onDeviceDisconnected,
            // onDeviceConnected).
            auto const device = retry_state.device;
            startFlashing(cable_number, device);
        });
    }

    emit deviceFlashingInfoChanged(cable_number, 0, QStringLiteral("Retrying flashing..."));
    state.timer->start(retry_interval_ms);
}

void
CubeFlasherManager::_cancelRetry(int cable_number)
{
    auto it = m_retry_states.find(cable_number);
    if (it == m_retry_states.end()) {
        return;
    }
    if (it.value().timer) {
        it.value().timer->stop();
        it.value().timer->deleteLater();
    }
    m_retry_states.erase(it);
}

void
CubeFlasherManager::_cancelAllRetries()
{
    auto keys = m_retry_states.keys();
    for (auto const& cable : keys) {
        _cancelRetry(cable);
    }
}

void
CubeFlasherManager::_stopFlashing(int cable_number)
{
    for (auto i = m_flashers.begin(); i != m_flashers.end(); i++) {
        if ((*i)->cableNumber() == cable_number) {
            (*i)->terminate();
            m_flashers.erase(i);
            break;
        }
    }
}

bool
CubeFlasherManager::_isDeviceConnected(int cable_number) const
{
    return m_devices_to_cables.contains(cable_number);
}

bool
CubeFlasherManager::_isFlashing(int cable_number) const
{
    for (auto const& flasher : m_flashers) {
        if (flasher && flasher->cableNumber() == cable_number) {
            return true;
        }
    }
    return false;
}

void
CubeFlasherManager::_onFlasherCompleted(int cable_number)
{
    _cancelRetry(cable_number);
    emit deviceFlashedCompleted(cable_number);
}

void
CubeFlasherManager::_onFlasherFailed(int cable_number)
{
    auto const device = m_devices_to_cables.value(cable_number);
    qCWarning(cat_dialog).noquote()
        << QStringLiteral("Прошивка устройства '%1' завершилась с ошибкой. Повторная попытка через несколько секунд.").arg(device);
    qCWarning(cat_dialog).noquote()
        << QStringLiteral("Снимите перемычку на устройстве: '%1'").arg(device);
    if (not device.isEmpty() && !isAutoFlashingSuppressedForDevice(device)) {
        _scheduleRetry(cable_number, device);
    }
    emit deviceFlashedFailed(cable_number);
}

void
CubeFlasherManager::_onFlasherCouldNotStart(int cable_number)
{
    auto const device = m_devices_to_cables.value(cable_number);
    qCWarning(cat_dialog).noquote()
        << QStringLiteral("Не удалось запустить прошивку устройства '%1'. Снимите перемычку и повторите подключение.")
               .arg(device);
    if (not device.isEmpty() && !isAutoFlashingSuppressedForDevice(device)) {
        _scheduleRetry(cable_number, device);
    }
    emit deviceFlashingCouldNotStart(cable_number);
}
