#include "JsonParser.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "flashingconstants.h"
#include "loggingCategories.h"

namespace {
static constexpr auto kDeviceTypeField{"device-type"};
static constexpr auto kFirmwareField{"firmware"};
static constexpr auto kImageField{"image"};
static constexpr auto kDirField{"dir"};
static constexpr auto kUBootField{"uboot"};
static constexpr auto kAt91BootstrapField{"at91bootstrap"};
static constexpr auto kRootfsField{"rootfs"};
}  // namespace

QString
jsonTypeToString(QJsonValue::Type type)
{
    switch (type) {
    case QJsonValue::Type::Array:
        return QStringLiteral("array");
    case QJsonValue::Type::Bool:
        return QStringLiteral("bool");
    case QJsonValue::Type::Double:
        return QStringLiteral("double");
    case QJsonValue::Type::Null:
        return QStringLiteral("null");
    case QJsonValue::Type::Object:
        return QStringLiteral("object");
    case QJsonValue::Type::String:
        return QStringLiteral("string");
    case QJsonValue::Type::Undefined:
    default:
        return QStringLiteral("undefined");
    }
}

JsonParser::JsonParser(QObject* parent) : QObject{parent}
{
    qRegisterMetaType<QVector<Firmware>>("QVector<Firmware>");

    auto const config_file_path = m_settings.value(flashingConstants::kConfigFilePathSettingsKey).toString();
    if (not config_file_path.isEmpty()) {
        processConfigFile(config_file_path, false);
        qCInfo(cat_json_parser).noquote()
            << QStringLiteral("Загружены данные о прошивках. Количество доступных прошивок:") << m_firmware.size();
    }
}

void
JsonParser::printProcessedFields(JsonParser::Firmware const& firmware) const
{
    qCDebug(cat_json_parser).noquote() << "Поле device_type: " << firmware.m_device_type;
    qCDebug(cat_json_parser).noquote() << "Поле directory: " << firmware.m_directory;
    qCDebug(cat_json_parser).noquote() << "Поле at91bootstrap_file_path: " << firmware.m_at91bootstrap_file_path;
    qCDebug(cat_json_parser).noquote() << "Поле firmware_description: " << firmware.m_firmware_description;
    qCDebug(cat_json_parser).noquote() << "Поле image_type: " << firmware.m_image_type;
    qCDebug(cat_json_parser).noquote() << "Поле rootfs_file_path: " << firmware.m_rootfs_file_path;
    qCDebug(cat_json_parser).noquote() << "Поле uboot_file_path: " << firmware.m_uboot_file_path;
}

QVector<JsonParser::Firmware>
JsonParser::processConfigFile(QString const& config_file_path, bool show_message_boxes)
{
    qCDebug(cat_json_parser).noquote() << QStringLiteral("Обработка файла '%1'").arg(config_file_path);
    auto const l_logAndShowError = [show_message_boxes](
                                       QString const& error_title, QString const& error_message,
                                       QString const& log_error) -> QVector<JsonParser::Firmware> {
        qCWarning(cat_json_parser).noquote() << log_error;
        Q_UNUSED(show_message_boxes);
        Q_UNUSED(error_title);
        Q_UNUSED(error_message);
        return {};
    };

    QByteArray data_from_file;
    {
        QFile config_file(config_file_path);

        if (not config_file.exists()) {
            return {};
        }

        if (not config_file.open(QIODevice::ReadOnly)) {
            return l_logAndShowError(
                QStringLiteral("File opening error"),
                QStringLiteral("Failed to open file for reading. Error: '%1'").arg(config_file.errorString()),
                QStringLiteral("Не удалось открыть файл '%1' для чтения. Ошибка: '%2'")
                    .arg(config_file.fileName(), config_file.errorString()));
        }

        data_from_file = config_file.readAll();
        qCDebug(cat_json_parser).noquote() << "Содержимое файла прочитано успешно";
    }

    QJsonParseError parse_error;
    auto const json = QJsonDocument::fromJson(data_from_file, &parse_error);

    auto const kContentErrorMessage{QStringLiteral("File content error")};

    if (parse_error.error != QJsonParseError::ParseError::NoError) {
        return l_logAndShowError(
            kContentErrorMessage, QStringLiteral("Json error: '%1'").arg(parse_error.errorString()),
            QStringLiteral("Ошибка преобразования данных в JSON документ: '%1'").arg(parse_error.errorString()));
    }

    if (not json.isObject()) {
        return l_logAndShowError(
            kContentErrorMessage, QStringLiteral("File content error: expecting that file is contain JSON-object"),
            QStringLiteral("Указанный пользователем файл не содержит JSON-объект. Данные в файле: %1")
                .arg(QString::fromUtf8(json.toJson(QJsonDocument::Indented))));
    }

    auto const json_object = json.object();

    QVector<JsonParser::Firmware> result;
    unsigned processed_items = 0;

    for (unsigned index = 1;; ++index) {
        auto const index_key = QString::number(index);
        auto const value     = json_object.value(index_key);

        if (value.isUndefined()) {
            qCDebug(cat_json_parser).noquote() << "Обработка завершена после " << processed_items << " элементов";
            break;
        }

        if (not value.isObject()) {
            qCWarning(cat_json_parser).noquote()
                << QStringLiteral("Ошибка во время обработки данных ключа '%1'").arg(QString::number(index));
            continue;
        }

        try {
            result.push_back(_processRecord(value.toObject()));
            qCDebug(cat_json_parser).noquote() << "Элемент " << processed_items << " успешно обработан";
            printProcessedFields(result.back());

        } catch (std::runtime_error const& error) {
            qCWarning(cat_json_parser).noquote()
                << QStringLiteral("Ошибка во время извлечения данных ключа. ") << error.what();
        }
    }

    qCInfo(cat_json_parser).noquote() << "Обработка конфигурационного файла завершена успешно";
    qCInfo(cat_json_parser).noquote() << "Обработано элементов: " << processed_items;

    m_settings.setValue(flashingConstants::kConfigFilePathSettingsKey, config_file_path);
    m_firmware = result;
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QMetaObject::invokeMethod(this, [this]() { emit firmwareUpdated(m_firmware); }, Qt::QueuedConnection);
#else
    QMetaObject::invokeMethod(this, "firmwareUpdated", Qt::QueuedConnection, Q_ARG(QVector<Firmware>, m_firmware));
#endif
    return result;
}

QVector<JsonParser::Firmware>
JsonParser::availableFirmware() const
{
    return m_firmware;
}

JsonParser::Firmware
JsonParser::_processRecord(QJsonObject const& object) const
{
    JsonParser::Firmware result;

    auto const l_extractValue = [&object](QString const& key, QString& extract_to) -> bool {
        auto const json_value = object.value(key);

        if (json_value.isUndefined()) {
            qCWarning(cat_json_parser).noquote() << QStringLiteral("Объект не содержит данных ключа '%1'").arg(key);
            return false;
        }

        if (not json_value.isString()) {
            qCWarning(cat_json_parser).noquote()
                << QStringLiteral(
                       "Данные ключа '%1' имеют неверный тип. Тип данных значения ключа '%2',"
                       "ожидается строка.")
                       .arg(key, jsonTypeToString(json_value.type()));
            return false;
        }

        auto const json_string = json_value.toString();

        if (json_string.isEmpty()) {
            qCWarning(cat_json_parser).noquote()
                << QStringLiteral("Пустая строка указана указана в качестве значения ключа '%1'").arg(key);
            return false;
        }

        extract_to = json_value.toString();
        return true;
    };

    std::vector<std::pair<decltype(kDeviceTypeField), QString&>> keys_to_extraction_references{
        {kDeviceTypeField, result.m_device_type},
        {kDirField, result.m_directory},
        {kAt91BootstrapField, result.m_at91bootstrap_file_path},
        {kFirmwareField, result.m_firmware_description},
        {kImageField, result.m_image_type},
        {kRootfsField, result.m_rootfs_file_path},
        {kUBootField, result.m_uboot_file_path}};

    for (auto it = keys_to_extraction_references.begin(); it != keys_to_extraction_references.end(); ++it) {
        if (not l_extractValue(it->first, it->second)) {
            throw std::runtime_error(
                QStringLiteral("Ошибка извлечения значения ключа '%1'").arg(it->first).toStdString());
        }
    }

    if (not result.m_directory.startsWith('/')) {
        result.m_directory.prepend('/');
    }

    if (result.m_directory.endsWith('/')) {
        result.m_directory.chop(1);
    }

    printProcessedFields(result);

    return result;
}
