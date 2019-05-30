#pragma once
#include <utility>
#include <boost/log/core/record.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/trivial.hpp>

namespace CNCOnlineForwarder::Logging
{
    using Level = boost::log::trivial::severity_level;

    class Logging
    {
    public:
        using SeverityLogger = boost::log::sources::severity_logger_mt<Level>;
        using LogRecord = boost::log::record;
        using LogStream = boost::log::record_ostream;
        using LogSink = boost::log::sinks::synchronous_sink<boost::log::sinks::text_file_backend>;
        class LogProxy;

        Logging();
    };

    class Logging::LogProxy
    {
    public:
        LogProxy(Logging::SeverityLogger& logger, Level level);
        LogProxy(const LogProxy&) = delete;
        LogProxy& operator=(const LogProxy&) = delete;
        ~LogProxy();

        template<typename T>
        LogStream& operator<<(T&& argument);

    private:
        SeverityLogger& logger;
        LogRecord record;
        LogStream stream;
    };

    Logging::LogProxy log(Level level);

    void setFilterLevel(Level level);

    template<typename T>
    Logging::LogStream& Logging::LogProxy::operator<<(T&& argument)
    {
        if (this->record)
        {
            this->stream << std::forward<T>(argument);
        }
        return this->stream;
    }

    template<typename Type, typename... Arguments>
    void logLine(const Level level, Arguments&&... arguments)
    {
        auto logProxy = log(level);
        logProxy << Type::description << ": ";
        (logProxy << ... << std::forward<Arguments>(arguments));
    }
}
