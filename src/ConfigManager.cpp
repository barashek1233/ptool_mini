#include "ConfigManager.h"

std::unique_ptr<ConfigManager> ConfigManager::m_instance;

ConfigManager* ConfigManager::instance() 
{
    if (!m_instance) {
        m_instance.reset(new ConfigManager());
    }
    return m_instance.get();
}

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
    , m_settings(QDir::homePath() + "/.config/ptool.conf", QSettings::IniFormat)
{
    // Устанавливаем значения по умолчанию
    m_settings.setValue("kiosk_mode", m_settings.value("kiosk_mode", false));
    m_settings.setValue("cpu_type", m_settings.value("cpu_type", "allwinner"));
    m_settings.setValue("address_host", m_settings.value("address_host", "http://localhost:8000"));
    m_settings.setValue("log_level", m_settings.value("log_level", "info"));
}

bool ConfigManager::isKioskEnabled() const 
{
    return m_settings.value("kiosk_mode", false).toBool();
}

QString ConfigManager::cpuType() const 
{
    return m_settings.value("cpu_type", "allwinner").toString().trimmed().toLower();
}

QString ConfigManager::logLevel() const
{
    return m_settings.value("log_level", "info").toString().trimmed().toLower();
}

void ConfigManager::reload() 
{
    m_settings.sync();
    emit configReloaded();
}

QString ConfigManager::addressHost() const {
    return m_settings.value("address_host", "http://localhost:8000").toString();
}

int ConfigManager::kioskId() const {
    return m_settings.value("kiosk_id", 0).toInt();
}

void ConfigManager::ensureKioskIdExists() {
    if (!m_settings.contains("kiosk_id")) {
        m_settings.setValue("kiosk_id", 0);
        m_settings.sync();
    }
}
