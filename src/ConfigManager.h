#pragma once

#include <QObject>
#include <QSettings>
#include <memory>
#include <QDir>

class ConfigManager : public QObject
{
    Q_OBJECT

public:
    static ConfigManager* instance();
    
    bool isKioskEnabled() const;
    QString cpuType() const;
    QString logLevel() const;
    void reload();
    QString addressHost() const;
    int kioskId() const;
    void ensureKioskIdExists();

signals:
    void configReloaded();

private:
    explicit ConfigManager(QObject *parent = nullptr);
    QSettings m_settings;
    
    static std::unique_ptr<ConfigManager> m_instance;
};
