#include "precompiled.h"
#include "IOManager.hpp"
#include "NatNegProxy.h"
#include "Logging.h"
#include "WeakRefHandler.hpp"

using AddressV4 = boost::asio::ip::address_v4;
using UDP = boost::asio::ip::udp;
using UDPEndPoint = boost::asio::ip::udp::endpoint;
using ErrorCode = boost::system::error_code;

namespace CNCOnlineForwarder
{
    void signalHandler(IOManager& manager, const ErrorCode& errorCode, const int signal)
    {
        using namespace Logging;
        if (errorCode.failed())
        {
            logLine<IOManager>(Level::error, "Signal async wait failed: ", errorCode);
        }
        else
        {
            logLine<IOManager>(Level::info, "Received signal ", signal);
        }

        logLine<IOManager>(Level::info, "Shutting down.");
        manager.stop();
    }

    void run()
    {
        using namespace Logging;
        using namespace Utility;
        try
        {
            log(Level::info) << "Begin!";

            const auto ioManager = IOManager::create();
            auto objectMaker = IOManager::ObjectMaker{ ioManager };

            auto signals = objectMaker.make<boost::asio::signal_set>(SIGINT, SIGTERM);
            signals.async_wait(makeWeakHandler(ioManager.get(), signalHandler));
            
            const auto addressTranslator = ProxyAddressTranslator::create(objectMaker);

            const auto natNegProxy = NatNeg::NatNegProxy::create
            (
                objectMaker,
                "natneg.server.cnc-online.net",
                27901,
                addressTranslator
            );

            
            auto testStrand = objectMaker.makeStrand();
            using UDP = boost::asio::ip::udp;
            auto socket = WithStrand<UDP::socket>{ testStrand, UDP::endpoint{UDP::v4(), 0} };
            auto buffer = std::string{};
            auto remote = UDP::endpoint{};
            buffer.resize(256);
            const auto test = [&socket, &buffer, &remote]
            {
                std::cout << "Local Address: " << socket->local_endpoint();

                const auto receiveFrom = [&](const auto handler)
                {
                    socket.asyncReceiveFrom
                    (
                        boost::asio::buffer(buffer),
                        remote,
                        [handler](const auto& error, const auto bytes)
                        {
                            return handler(handler, error, bytes);
                        }
                    );
                };

                const auto handler = [&, receiveFrom](const auto self, const auto& error, const auto bytes)
                {
                    receiveFrom(self);
                    if (error.failed())
                    {
                        std::cout << "Error: " << error << std::endl;
                        return;
                    }
                    std::cout << "Received from " << remote << ": " << buffer.substr(0, bytes) << std::endl;
                };

                receiveFrom(handler);
            };
            boost::asio::defer(testStrand, test);


            {
                const auto runner = [ioManager] { ioManager->run(); };
                auto f1 = std::async(std::launch::async, runner);
                auto f2 = std::async(std::launch::async, runner);

                f1.get();
                f2.get();
            }

            log(Level::info) << "End";
        }
        catch (const std::exception& error)
        {
            log(Level::fatal) << "Unhandled exception: " << error.what();
        }
    }
}


int main(int argc, char** argv)
{
    try
    {
        CNCOnlineForwarder::run();
    }
    catch (...)
    {
        using namespace CNCOnlineForwarder::Logging;
        log(Level::fatal) << "Unknown exception";
        return 1;
    }
    return 0;
}