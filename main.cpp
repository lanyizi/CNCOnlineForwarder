#include "precompiled.h"
#include "IOManager.hpp"
#include "NatNegProxy.h"
#include "Logging.h"
#include "WeakRefHandler.hpp"

using AddressV4 = boost::asio::ip::address_v4;
using UDP = boost::asio::ip::udp;
using UDPEndPoint = boost::asio::ip::udp::endpoint;

namespace CNCOnlineForwarder
{
    struct SignalHandler
    {
        using ErrorCode = boost::system::error_code;
        void operator()(IOManager& manager, const ErrorCode& errorCode, const int signal) const
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
    };

    void run(const AddressV4& publicAddress)
    {
        using namespace Logging;
        using namespace Utility;
        try
        {
            log(Level::info) << "Begin!";

            const auto ioManager = IOManager::create();
            auto objectMaker = IOManager::ObjectMaker{ ioManager };

            auto signals = objectMaker.make<boost::asio::signal_set>(SIGINT, SIGTERM);
            signals.async_wait(makeWeakHandler(ioManager.get(), SignalHandler{}));

            const auto natNegEndPoint = UDPEndPoint{ UDP::v4(), 27901 };
            const auto natNegProxy = NatNeg::NatNegProxy::create
            (
                objectMaker,
                natNegEndPoint,
                publicAddress
            );

            {
                const auto f1 = std::async(std::launch::async, [ioManager] { ioManager->run(); });
                const auto f2 = std::async(std::launch::async, [ioManager] { ioManager->run(); });
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
        if (argc < 2)
        {
            using namespace CNCOnlineForwarder::Logging;
            log(Level::fatal) << "Missing argument (public IP)";
            return 1;
        }
        CNCOnlineForwarder::run(AddressV4::from_string(argv[1]));
    }
    catch (...)
    {
        using namespace CNCOnlineForwarder::Logging;
        log(Level::fatal) << "Unknown exception";
        return 1;
    }
    return 0;
}