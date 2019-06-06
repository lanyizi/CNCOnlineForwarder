#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>
#include "Logging.h"

namespace CNCOnlineForwarder::Utility
{
    template<typename Type>
    class SimpleWriteHandler
    {
    public:
        template<typename String>
        SimpleWriteHandler(String&& data) :
            data{ std::make_unique<std::string>(std::forward<String>(data)) }
        {
        }

        boost::asio::const_buffer getData() const noexcept 
        { 
            return boost::asio::buffer(*this->data);
        }

        void operator()
        (
            const boost::system::error_code& code, 
            const std::size_t bytesSent
        ) const
        {
            using namespace Logging;

            if (code.failed())
            {
                logLine<Type>(Level::error, "Async write failed: ", code);
                return;
            }

            if (bytesSent != this->data->size())
            {
                logLine<Type>(Level::error, "Only part of packet was sent: ", bytesSent, "/", this->data->size());
                return;
            }
        }

    private:
        std::unique_ptr<std::string> data;
    };

    template<typename Type, typename String>
    auto makeWriteHandler(String&& data)
    {
        return SimpleWriteHandler<Type>{std::forward<String>(data)};
    }
}
