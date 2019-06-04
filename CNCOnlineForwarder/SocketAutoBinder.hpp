#pragma once
#include <memory>
#include <string>
#include <utility>
#include "Logging.h"
#include "PromisedReady.hpp"
#include "WeakRefHandler.hpp"

namespace CNCOnlineForwarder::Utility::SocketAutoBinder
{
    template<typename Type, typename AfterWrite>
    class HelperWriteHandler
    {
    public:
        template<typename String>
        HelperWriteHandler(String&& data, AfterWrite afterWrite) :
            data{ std::make_unique<std::string>(std::forward<String>(data)) },
            afterWrite{ std::move(afterWrite) }
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
                logLine<Type>(Level::error, "Proxy: only part of packet was sent: ", bytesSent, "/", this->data->size());
                return;
            }

            afterWrite();
        }

    private:
        std::unique_ptr<std::string> data;
        AfterWrite afterWrite;
    };

    template<typename Type, typename String, typename AfterWrite>
    auto makeHelperWriteHandler(String&& string, AfterWrite afterWrite)
    {
        return HelperWriteHandler<Type, AfterWrite>
        {
            std::forward<String>(string),
            std::move(afterWrite)
        };
    }

    template<typename Type, typename String, typename SocketReadyStateProvider>
    auto makeWeakWriteHandler
    (
        String&& string,
        Type* pointer,
        SocketReadyStateProvider provider
    )
    {
        auto handler = makeWeakHandler
        (
            pointer,
            [f = std::move(provider)](Type& data)
            {
                auto& socketReadyState = f(data);
                socketReadyState->setReady();
                socketReadyState.trySetReady();
            }
        );

        return HelperWriteHandler<Type>
        {
            std::forward<String>(string),
            std::move(handler)
        };
    }
}