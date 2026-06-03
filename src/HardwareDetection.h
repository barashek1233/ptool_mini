#pragma once

#include <mutex>

#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QDebug>
#include <QTimer>

class QProcessEnvironment;
class CubeFlasherManager;

static auto const kTimeoutForHardwareDetectionSettingsKey{"timeoutForHardwareDetection"};

class HardwareDetection : public QObject
{
    Q_OBJECT
public:
    enum class Revision {
        kUndefined = -2,
        kUnknown,
        kCube_d_r16_Cam_Gemalto,
        kCube_d_r17,
        kCube_d_r17_Gemalto,
        kCube_d_r17_Quectel,
        kCube_T_b_r8_Lite,
        kCube_T_b_r7_Gemalto,
        kCube_T_b_r8_Quectel,
        kCube_T_b_r8_Cam_Quectel
    };
    Q_ENUM(Revision)

    explicit HardwareDetection(
        QProcessEnvironment const* env, CubeFlasherManager const* manager, QString const& device,
        QObject* parent = nullptr);

public:
    QString hardwareRevision() const;
    static QString revisionToString(HardwareDetection::Revision revision);
    void checkHWRevision();
    void terminate();
    void setTimeoutForFailedHardwareDetection(std::chrono::milliseconds const& timeout_ms);
    std::chrono::milliseconds timeoutForFailedHardwareDetection() const;
    static std::chrono::milliseconds defaultTimeoutForHardwareDetection();

signals:
    void hardwareRevisionDetected(HardwareDetection::Revision revision);
    void errorOccurred(QString const& error_message);

private:
    struct range {
        range(uint32_t min, uint32_t max) : min(min), max(max)
        {}
        range(uint32_t v) : min(v), max(v)
        {}
        range(QString const& hex_value, bool is_pattern = false)
            : min(hex_value.toUInt(nullptr, 16)), max(min), pattern(hex_value.toLower()), is_pattern(is_pattern)
        {}
        range(QString const& hexMin, QString const& hexMax)
            : min(hexMin.toUInt(nullptr, 16)), max(hexMax.toUInt(nullptr, 16))
        {}
        inline bool
        inRange(uint32_t v) const
        {
            return min == max ? v == min : (v >= min && v <= max);
        }
        inline bool
        inRange(QString v) const
        {
            if (not is_pattern) {
                return inRange(v.toUInt(nullptr, 16));
            }
            v = v.toLower();
            if (v.length() < pattern.length()) {
                return false;
            }
            for (int i = 0; i < pattern.length(); ++i) {
                if (pattern.at(i) == '_') {
                    continue;
                }
                if (pattern.at(i).toLower() != v[i].toLower()) {
                    return false;
                }
            }
            return true;
        }

        uint32_t min;
        uint32_t max;
        QString pattern;
        bool is_pattern{false};
    };

    struct register_data {
        register_data(std::tuple<range, range, range, range> const& values) : register_values(values)
        {}
        std::tuple<range, range, range, range> register_values;
    };

private:
    QTimer m_detection_timer{this};
    QProcess* m_process;
    CubeFlasherManager const* m_cube_flasher_manager{nullptr};
    QString m_stdout;
    QString m_stderr;
    QString const m_device;
    std::chrono::milliseconds m_hardware_detection_timeout;
    std::once_flag m_emit_hardware_revision_ready_flag;
    Revision m_revision;

    bool m_is_hardware_revision_detected{false};
    bool m_is_hw_detection_restarted_after_permission_error{false};
    bool m_is_terminated{false};

private:
    bool _isAllRegistersDataReceived(QString const& stdio) const;
    void _emit(HardwareDetection::Revision revision);
    void _checkIsAllRegistersReceivedFromAnyStdStream();
    void _processFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void _readOutput();
    void _readError();
    void _retry();
    void _handleProcessError(QProcess::ProcessError error);
    static QMap<HardwareDetection::Revision, HardwareDetection::register_data> _matchTable();
    void _matchHWRevision(QString const& stdio);
};
