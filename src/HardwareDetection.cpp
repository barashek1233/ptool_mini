#include "HardwareDetection.h"

#include <QDebug>
#include <QMetaEnum>
#include <QTimer>

#include "CubeFlasher.h"
#include "CubeFlasherManager.h"
#include "loggingCategories.h"

HardwareDetection::HardwareDetection(
    QProcessEnvironment const* env, CubeFlasherManager const* manager, QString const& device, QObject* parent)
    : QObject(parent),
      m_process(new QProcess(this)),
      m_cube_flasher_manager(manager),
      m_device(device),
      m_revision(HardwareDetection::Revision::kUndefined)
{
    //  sam-ba -p serial:dev -d sama5d2 -m read32:0xFC038008 -m read32:0xFC038048 -m
    //  read32:0xFC038088 -m read32:0xFC0380C8

    m_detection_timer.setSingleShot(true);
    connect(
        m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        &HardwareDetection::_processFinished);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &HardwareDetection::_readOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &HardwareDetection::_readError);
    connect(m_process, &QProcess::errorOccurred, this, &HardwareDetection::_handleProcessError);
    connect(&m_detection_timer, &QTimer::timeout, this, [this]() { _emit(HardwareDetection::Revision::kUnknown); });

    QStringList arguments{"-p", "serial:" + m_device.split('/').last(),
                          "-d", "sama5d2",
                          "-m", "read32:0xFC038008",
                          "-m", "read32:0xFC038048",
                          "-m", "read32:0xFC038088",
                          "-m", "read32:0xFC0380C8"};
    arguments.prepend("--sam-ba");
    m_process->setProgram(manager->sambaPath());
    m_process->setProcessEnvironment(*env);
    m_process->setArguments(arguments);
}

QString
HardwareDetection::hardwareRevision() const
{
    return revisionToString(m_revision);
}

void
HardwareDetection::_processFinished(int exit_code, QProcess::ExitStatus exit_status)
{
    if (m_is_terminated) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    qCInfo(cat_hardware_detection).noquote()
        << QStringView(u"Процесс '%1' завершился кодом '%2'.").arg(m_process->program(), QString::number(exit_code));
    qCInfo(cat_hardware_detection).noquote()
        << QStringLiteral("Полный результат выполнения команды определения HW ревизии. stderr:\n%1\nstdout:\n%2")
               .arg(m_stderr, m_stdout);

#else
    qCInfo(cat_hardware_detection).noquote()
        << QString("Процесс '%1' завершился кодом '%2'.").arg(m_process->program(), QString::number(exit_code));
    qCInfo(cat_hardware_detection).noquote()
        << QStringLiteral("Полный результат выполнения команды определения HW ревизии. stderr:\n%1\nstdout:\n%2")
               .arg(m_stderr, m_stdout);
#endif
    if (exit_status != QProcess::ExitStatus::NormalExit) {
        qCWarning(cat_hardware_detection).noquote()
            << QStringLiteral("Приложение аварийно завершилось. Стандартный вывод в поток stderr:") << m_stderr;
    }

    if (m_stderr.contains("permission", Qt::CaseInsensitive)) {
        qCWarning(cat_hardware_detection).noquote() << QStringLiteral("Ошибка доступа.") << m_stderr;

        if (m_detection_timer.isActive()) {
            m_detection_timer.stop();
        }

        if (m_is_hw_detection_restarted_after_permission_error) {
            emit errorOccurred(m_stderr);
            _emit(HardwareDetection::Revision::kUnknown);
        } else {
            using namespace std::chrono_literals;
            m_is_hw_detection_restarted_after_permission_error = true;
            static constexpr auto kRetryTimeout{1s};
            QTimer::singleShot(kRetryTimeout, this, [this]() { _retry(); });
        }
    }
}

void
HardwareDetection::_readOutput()
{
    m_stdout.append(QString(m_process->readAllStandardOutput()));
    _checkIsAllRegistersReceivedFromAnyStdStream();
}

void
HardwareDetection::_readError()
{
    m_stderr.append(QString(m_process->readAllStandardError()));
    _checkIsAllRegistersReceivedFromAnyStdStream();
}

void
HardwareDetection::_retry()
{
    if (m_is_terminated) {
        return;
    }
    qCInfo(cat_hardware_detection).noquote()
        << QStringLiteral("Повторная попытка определения hw ревизии устройства %1").arg(m_device);
    m_detection_timer.start(timeoutForFailedHardwareDetection());
    m_stderr.clear();
    m_stdout.clear();

    m_process->start();
}

void
HardwareDetection::_handleProcessError(QProcess::ProcessError error)
{
    if (m_is_terminated) {
        return;
    }
    QString error_message;
    switch (error) {
    case QProcess::ProcessError::FailedToStart: {
        error_message = QStringLiteral(
            "The 'sam-ba' process failed to start.\nEither the invoked program is missing or may you "
            "have insufficient permissions to invoke the program.\n\nPlease check path to 'sam-ba'.");
        break;
    }
    case QProcess::ProcessError::Crashed: {
        error_message = QStringLiteral("The 'sam-ba' process crashed some time after starting successfully");
        break;
    }
    case QProcess::ProcessError::ReadError: {
        error_message = QStringLiteral(
            "An error occurred when attempting to read from the 'sam-ba' process. For example, the process may not be "
            "running");
        break;
    }
    case QProcess::ProcessError::WriteError: {
        error_message = QStringLiteral(
            "An error occurred when attempting to write to the 'sam-ba' process. For example, the process may not be "
            "running, "
            "or it may have closed its input channel.");
        break;
    }
    case QProcess::ProcessError::Timedout:
    case QProcess::ProcessError::UnknownError: {
        break;
    }
    }

    emit errorOccurred(error_message);

    _emit(HardwareDetection::Revision::kUnknown);
}

QString
HardwareDetection::revisionToString(HardwareDetection::Revision revision)
{
    auto const revision_enum = QMetaEnum::fromType<HardwareDetection::Revision>();
    auto const revision_str  = QString{revision_enum.valueToKey(static_cast<int>(revision))};
    Q_ASSERT(!revision_str.isNull() && "Unknown revision value");
    if (revision_str.isNull()) {
        return {};
    }
    return revision_str.right(revision_str.length() - 1);
}

void
HardwareDetection::checkHWRevision()
{
    if (m_is_terminated) {
        return;
    }
    if (not m_process->isOpen()) {
        qCInfo(cat_hardware_detection).noquote()
            << QStringLiteral("Запущен процесс проверки HW ревизии устройства %1").arg(m_device);
        qCInfo(cat_hardware_detection).noquote()
            << QStringLiteral("Выполняем команду определения HW ревизии: %1 %2")
                   .arg(m_process->program(), m_process->arguments().join(QStringLiteral(" ")));
        m_process->start();

        if (not m_detection_timer.isActive()) {
            m_detection_timer.start(timeoutForFailedHardwareDetection());
        }
    }
}

void
HardwareDetection::terminate()
{
    m_is_terminated = true;
    m_detection_timer.stop();

    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished();
    }
}

void
HardwareDetection::setTimeoutForFailedHardwareDetection(std::chrono::milliseconds const& timeout_ms)
{
    m_hardware_detection_timeout = timeout_ms;
}

std::chrono::milliseconds
HardwareDetection::timeoutForFailedHardwareDetection() const
{
    return m_hardware_detection_timeout;
}

std::chrono::milliseconds
HardwareDetection::defaultTimeoutForHardwareDetection()
{
    using namespace std::chrono_literals;
    static constexpr auto kDefaultTimeoutForHardwareDetection{3s};
    return kDefaultTimeoutForHardwareDetection;
}

void
HardwareDetection::_checkIsAllRegistersReceivedFromAnyStdStream()
{
    if (_isAllRegistersDataReceived(m_stdout)) {
        _matchHWRevision(m_stdout);
        _emit(m_revision);
    } else if (_isAllRegistersDataReceived(m_stderr)) {
        _matchHWRevision(m_stderr);
        _emit(m_revision);
    }
}

bool
HardwareDetection::_isAllRegistersDataReceived(QString const& stdio) const
{
    return stdio.count("read32") == 4 && stdio.contains("Connection closed", Qt::CaseInsensitive);
}

void
HardwareDetection::_emit(Revision revision)
{
    if (m_is_terminated) {
        return;
    }
    std::call_once(
        m_emit_hardware_revision_ready_flag, [this, revision]() { emit hardwareRevisionDetected(revision); });
}

QMap<HardwareDetection::Revision, HardwareDetection::register_data>
HardwareDetection::_matchTable()
{
    // https://gitlab.aqsi.ru/pg-group/analysis/cube/-/issues/596#note_163968

    static QMap<Revision, HardwareDetection::register_data> kRegisterDataMap{
        {HardwareDetection::Revision::kCube_d_r16_Cam_Gemalto,
         std::make_tuple<range, range, range, range>(
             {"0xDBFD7F00", QString("0xDBFD7FFF")}, {"0xFFBFBD1E"}, {"0xFFFFE1FF"}, {"0xBC_F8_7F", true})},

        {HardwareDetection::Revision::kCube_d_r17,
         std::make_tuple<range, range, range, range>(
             {"0xDBFD7F00", QString("0xDBFD7FFF")}, {"0xFFBFFD1E"}, {"0xFFFFE5FF"}, {"0xBC_F8_7D", true})},

        {HardwareDetection::Revision::kCube_d_r17_Gemalto,
         std::make_tuple<range, range, range, range>(
             {"0xDBFD7F00", QString("0xDBFD7FFF")}, {"0xFFBFBD1E"}, {"0xFFFFE1FF"}, {"0xBC_F8_7D", true})},

        {HardwareDetection::Revision::kCube_d_r17_Quectel,
         std::make_tuple<range, range, range, range>(
             {"0xDBFD7F00", QString("0xDBFD7FFF")}, {"0xFFBFBD1E"}, {"0xFFFFE5FF"}, {"0xBC_F8_7D", true})},

        {HardwareDetection::Revision::kCube_T_b_r8_Lite,
         std::make_tuple<range, range, range, range>(
             {"0x_7FF7F__", true}, {"0xFFFF___A", true}, {"0xFFFFE_FF", true}, {"0xBC_F8___", true})},

        {HardwareDetection::Revision::kCube_T_b_r7_Gemalto,
         std::make_tuple<range, range, range, range>(
             {"0xD7FF7F00", QString("0xD7FF7FFF")}, {"0xFFFFBD1A"}, {"0xFFFFE9FF"}, {"0xBC_F8_7D", true})},

        {HardwareDetection::Revision::kCube_T_b_r8_Quectel,
         std::make_tuple<range, range, range, range>(
             {"0xD7FF7F00", QString("0xD7FF7FFF")}, {"0xFFFFBD1A"}, {"0xFFFFEDFF"}, {"0xBC_F8_7D", true})},

        {HardwareDetection::Revision::kCube_T_b_r8_Cam_Quectel,
         std::make_tuple<range, range, range, range>(
             {"0xD7FF7F00", QString("0xD7FF7FFF")}, {"0xFFFFBD1A"}, {"0xFFFFEDFF"}, {"0xBC_F8_7D", true})}

    };

    return kRegisterDataMap;
}

void
HardwareDetection::_matchHWRevision(QString const& standard_output)
{
    if (m_is_hardware_revision_detected) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    auto const output_rows = standard_output.split('\n', Qt::SkipEmptyParts);
#else
    auto const output_rows = standard_output.split('\n', QString::SkipEmptyParts);
#endif
    QStringList values;
    for (auto const& row : qAsConst(output_rows)) {
        if (not row.contains("read32")) {
            continue;
        }
        auto const a = row.split('=');
        Q_ASSERT(a.size() == 2);
        values.push_back(a.at(1).trimmed());
    }

    if (values.size() != 4) {
        qCWarning(cat_hardware_detection).noquote()
            << QStringLiteral("Неожиданное количество регистров в выводе sam-ba: %1. Ожидалось 4.")
                   .arg(values.size());
        m_is_hardware_revision_detected = true;
        m_revision                      = HardwareDetection::Revision::kUnknown;
        return;
    }

    auto const match_map = HardwareDetection::_matchTable();

    for (auto it = match_map.begin(); it != match_map.end(); ++it) {
        if (std::get<0>(it.value().register_values).inRange(values.at(0)) &&
            std::get<1>(it.value().register_values).inRange(values.at(1)) &&
            std::get<2>(it.value().register_values).inRange(values.at(2)) &&
            std::get<3>(it.value().register_values).inRange(values.at(3))) {
            m_is_hardware_revision_detected = true;
            m_revision                      = it.key();
            return;
        }
    }

    qCWarning(cat_hardware_detection).noquote()
        << QStringLiteral("Ревизия устройства не известна. Вывод приложения sam-ba:") << standard_output;

    m_is_hardware_revision_detected = true;
    m_revision                      = HardwareDetection::Revision::kUnknown;
}
