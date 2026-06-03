#pragma once

#include <QtMessageHandler>
#include <functional>
#include <vector>

class LogMessageHandlers
{
public:
    template <typename F>
    void
    addHandler(F&& f)
    {
        installAsQtMessageHandler();
        m_handlers.emplace_back(std::forward<F>(f));
    }
    void reset();

    static LogMessageHandlers& instance();

private:
    LogMessageHandlers() = default;

    void installAsQtMessageHandler();
    void handleQtMessage(QtMsgType type, QMessageLogContext const& context, QString const& message);

    using LogMessageHandler =
        std::function<void(QtMsgType type, QMessageLogContext const&, QString const&, QString const&)>;

    std::vector<LogMessageHandler> m_handlers;
    bool m_installed{false};
    QtMessageHandler m_original_handler{nullptr};
};
