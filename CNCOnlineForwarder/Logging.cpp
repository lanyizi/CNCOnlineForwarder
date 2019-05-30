#include "precompiled.h"
#include "BuildConfiguration.h"
#include "Logging.h"

namespace CNCOnlineForwarder::Logging
{

    Logging::Logging()
    {
        namespace logging = boost::log;
        namespace keywords = boost::log::keywords;
        using namespace std::string_literals;

        logging::add_file_log
        (
            keywords::file_name = (PROJECT_NAME + "_%N.log"s),
            keywords::open_mode = (std::ios::out | std::ios::app),
            keywords::rotation_size = 1024 * 1024,
            keywords::format = "[%TimeStamp%]: %Message%"
        );

        setFilterLevel(Level::info);

        logging::add_common_attributes();
    }


    Logging::LogProxy::LogProxy(Logging::SeverityLogger& logger, Level level) :
        logger{ logger },
        record{ logger.open_record(boost::log::keywords::severity = level) }
    {
        if (this->record)
        {
            this->stream.attach_record(this->record);
            static constexpr auto levels = std::array<std::string_view, 6>
            {
                "[trace] ",
                "[debug] ",
                "[info] ",
                "[warning] ",
                "[error] ",
                "[fatal] "
            };
            this->stream << levels.at(level);
        }
    }

    Logging::LogProxy::~LogProxy()
    {
        if (this->record)
        {
            this->stream.flush();
            this->logger.push_record(std::move(this->record));
        }
    }

    Logging::LogProxy log(Level level)
    {
        static auto loggingSetup = Logging{};
        static auto logger = Logging::SeverityLogger{};
        return Logging::LogProxy{ logger, level };
    }

    void setFilterLevel(Level level)
    {
        static auto firstTime = true;
        if (!firstTime)
        {
            throw std::runtime_error{ "Not implemented yet" };
        }
        firstTime = false;
        boost::log::core::get()->set_filter(boost::log::trivial::severity >= level);
    }
    
}