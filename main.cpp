#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QTimer>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <sambatool.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

#include "ConsoleAtmelFlasher.h"
#include "CubeFlasherManager.h"
#include "DevNotifyQtStyle.h"
#include "LogMessageHandlers.h"
#include "Mapping.h"
#include "loggingCategories.h"
#include "allwinner/AllwinnerDevicesManager.h"
#include "allwinner/ConsoleAllwinnerFlasher.h"

namespace {
bool shouldPrintAtmelConsoleLog(QString const& category, QString const& message)
{
    if (category == QStringLiteral("aqsi.ptool.dialog")) {
        return true;
    }

    static QStringList const important_messages{
        QStringLiteral("Запущен процесс прошивки устройства"),
        QStringLiteral("Идет процесс стирания данных с устройства"),
        QStringLiteral("Идет процесс записи прошивки"),
        QStringLiteral("Идет процесс верификации прошивки"),
        QStringLiteral("Прогресс прошивки"),
        QStringLiteral("Не удалось запустить прошивку устройства"),
        QStringLiteral("Снимите джампер"),
        QStringLiteral("Прошивка не смогла начаться"),
        QStringLiteral("Завершение работы")};

    for (QString const& marker : important_messages) {
        if (message.contains(marker, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QString logLabel(QtMsgType type)
{
    if (type == QtCriticalMsg || type == QtFatalMsg) {
        return QStringLiteral("ERROR");
    }
    if (type == QtWarningMsg) {
        return QStringLiteral("WARNING");
    }
    return QStringLiteral("INFO");
}


int runSambaTool(int argc, char* argv[])
{
    QLoggingCategory::setFilterRules("*.debug=false\n"
                                     "qml.debug=true");
    qSetMessagePattern("%{message}");

    SambaTool app(argc, argv);
    QDir d = SambaTool::applicationDirPath();

    QString qml_path;
    QString metadata_path;

    QStringList const qml_candidates{
        d.filePath("lib/qml"),
        d.filePath("qml"),
        d.filePath("../lib/qml"),
        d.filePath("../lib/ptool/qml")};
    for (QString const& candidate : qml_candidates) {
        if (QDir(candidate).exists()) {
            qml_path = candidate;
            break;
        }
    }

    QStringList const metadata_candidates{
        d.filePath("lib/metadata"),
        d.filePath("metadata"),
        d.filePath("../lib/metadata"),
        d.filePath("../lib/ptool/metadata")};
    for (QString const& candidate : metadata_candidates) {
        if (QDir(candidate).exists()) {
            metadata_path = candidate;
            break;
        }
    }

    if (!qml_path.isEmpty()) {
        app.qmlEngine()->addImportPath(qml_path.toLatin1());
    }
    if (!metadata_path.isEmpty()) {
        qputenv("SAM_BA_METADATA_PATH", metadata_path.toLatin1());
    }

    app.init();
    QTimer::singleShot(0, &app, &SambaTool::run);
    return app.exec();
}

void ensureRuntimeDir()
{
    if (!qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR")) {
        return;
    }

    QString const runtime_dir = QStringLiteral("/tmp/runtime-%1").arg(QString::number(getuid()));
    QDir().mkpath(runtime_dir);
    QFile::setPermissions(
        runtime_dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("XDG_RUNTIME_DIR", runtime_dir.toLocal8Bit());
}

int runAtmelHeadless(QCoreApplication& application, const QString& image_path)
{
    if (!QDir(image_path).exists()) {
        QTextStream(stderr) << "ptool_mini: image directory does not exist: " << image_path << "\n";
        return 2;
    }

    {
        QDir const dir(image_path);
        if (dir.entryList({"*.ubi", "*.ubi.bin"}, QDir::Files).isEmpty()) {
            QTextStream(stderr) << "ptool_mini: no *.ubi image files found in: " << image_path << "\n";
            return 2;
        }
        if (!dir.exists("u-boot.bin")) {
            QTextStream(stderr) << "ptool_mini: u-boot.bin not found in: " << image_path << "\n";
            return 2;
        }
        if (!dir.exists("at91bootstrap.bin")) {
            QTextStream(stderr) << "ptool_mini: at91bootstrap.bin not found in: " << image_path << "\n";
            return 2;
        }
    }

    Mapping mapping;
    CubeFlasherManager cube_flasher_manager;
    static DevNotifyQtStyle dev;

    cube_flasher_manager.onImagesDirPathChanged(image_path);
    cube_flasher_manager.onUsePmeccOptionChanged(true);

    QObject::connect(
        &dev, &DevNotifyQtStyle::deviceConnectedOnCable, &mapping, &Mapping::on_deviceConnectedOnCable);
    QObject::connect(
        &dev, &DevNotifyQtStyle::deviceDisconnectedOnCable, &mapping, &Mapping::on_deviceDisconnectedOnCable);
    QObject::connect(
        &mapping, &Mapping::cubeConnected, &cube_flasher_manager, &CubeFlasherManager::onDeviceConnected);
    QObject::connect(
        &mapping, &Mapping::cubeDisconnected, &cube_flasher_manager, &CubeFlasherManager::onDeviceDisconnected);

    ConsoleAtmelFlasher console_flasher(cube_flasher_manager);
    QObject::connect(
        &console_flasher, &ConsoleAtmelFlasher::flashingFinished, &application,
        [](int exit_code) {
            if (exit_code != 0) {
                QTextStream(stderr) << "ptool_mini: flashing failed\n";
                fflush(stderr);
            }
            QCoreApplication::exit(exit_code);
        });

    qCInfo(cat_dialog).noquote() << QStringLiteral("Подключите куб к черной коробке");
    qInfo() << "ptool_mini: waiting for Atmel device connection";
    return application.exec();
}

int runAllwinnerHeadless(QCoreApplication& application, const QString& image_path)
{
    AllwinnerDevicesManager devices_manager;
    ConsoleAllwinnerFlasher flasher(devices_manager, image_path);

    QObject::connect(&flasher, &ConsoleAllwinnerFlasher::flashingFinished, &application, [&application](int exit_code) {
        QTimer::singleShot(0, &application, [exit_code, &application]() {
            if (exit_code != 0) {
                QTextStream(stderr) << "ptool_mini: flashing failed\n";
                fflush(stderr);
            }
            application.exit(exit_code);
        });
    });

    qCInfo(cat_dialog).noquote() << QStringLiteral("Подключите куб к черной коробке");
    qInfo() << "ptool_mini: waiting for Allwinner device connection";
    devices_manager.startPolling();
    return application.exec();
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc > 1 && !strcmp(argv[1], "--sam-ba")) {
        argv[1] = argv[0];
        return runSambaTool(--argc, ++argv);
    }

    ensureRuntimeDir();
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QCoreApplication application(argc, argv);
    application.setApplicationName("ptool_mini");
    application.setApplicationVersion(QStringLiteral(PTOOL_MINI_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription("Minimal console flasher: flash one device and exit");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption("cpu", "CPU type: atmel or allwinner", "type"));
    parser.addOption(QCommandLineOption("image-path", "Image path (directory for atmel, file for allwinner)", "path"));
    parser.process(application);

    QTextStream err(stderr);
    QString const cpu = parser.value("cpu").trimmed().toLower();
    QString const image_path = parser.value("image-path").trimmed();

    if (cpu.isEmpty() || image_path.isEmpty()) {
        err << "Both --cpu and --image-path are required\n";
        return 2;
    }

    if (cpu != "atmel" && cpu != "allwinner") {
        err << "Unsupported --cpu value. Use: atmel or allwinner\n";
        return 2;
    }

    bool const is_atmel = (cpu == "atmel");
    LogMessageHandlers::instance().addHandler(
        [is_atmel](QtMsgType type, QMessageLogContext const& context, QString const& msg, QString const& time) {
            if (msg.startsWith(QStringLiteral("CAF:"), Qt::CaseInsensitive)) {
                return;
            }
            QString const category = QString::fromLatin1(context.category);
            if (is_atmel && !shouldPrintAtmelConsoleLog(category, msg)) {
                return;
            }
            bool const is_dialog = (category == QStringLiteral("aqsi.ptool.dialog"));
            bool const is_error = (type == QtCriticalMsg || type == QtFatalMsg);
            QTextStream out(is_error ? stderr : stdout);
            if (is_dialog) {
                out << time << " [DIALOG] " << msg << "\n";
            } else {
                out << time << " [" << logLabel(type) << "] " << msg << "\n";
            }
            out.flush();
        });

    if (cpu == "atmel") {
        return runAtmelHeadless(application, image_path);
    }
    return runAllwinnerHeadless(application, image_path);
}
