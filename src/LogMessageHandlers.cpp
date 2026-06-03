#include "LogMessageHandlers.h"
#include <QDateTime>
#include <QTimeZone>

LogMessageHandlers&
LogMessageHandlers::instance()
{
    static LogMessageHandlers instance;
    return instance;
}

void
LogMessageHandlers::installAsQtMessageHandler()
{
    if (m_installed)
        return;
    m_original_handler =
        qInstallMessageHandler([](QtMsgType type, QMessageLogContext const& context, QString const& message) {
            LogMessageHandlers::instance().handleQtMessage(type, context, message);
        });
    m_installed = true;
}

void
LogMessageHandlers::reset()
{
    qInstallMessageHandler(m_original_handler);
    m_installed = false;
}

void
LogMessageHandlers::handleQtMessage(QtMsgType type, QMessageLogContext const& context, QString const& message)
{
    auto const current_date_time = QDateTime::currentDateTime();
    auto const time              = current_date_time.toTimeSpec(Qt::LocalTime)
                          .toTimeZone(current_date_time.timeZone())
                          .toString(Qt::ISODateWithMs);

    for (auto const& f : m_handlers) {
        assert(f);
        f(type, context, message, time);
    }
}
