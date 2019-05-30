#include "precompiled.h"
#include "ProxyAddressTranslator.h"
#include "SimpleHTTPClient.h"
#include "WeakRefHandler.hpp"
#include "Logging.h"

using ErrorCode = boost::system::error_code;
using LogLevel = CNCOnlineForwarder::Logging::Level;

namespace CNCOnlineForwarder
{
    template<typename... Arguments>
    void log(const LogLevel level, Arguments&&... arguments)
    {
        return Logging::logLine<ProxyAddressTranslator>(level, std::forward<Arguments>(arguments)...);
    }

    std::shared_ptr<ProxyAddressTranslator> ProxyAddressTranslator::create
    (
        const IOManager::ObjectMaker& objectMaker
    )
    {
        const auto self = std::make_shared<ProxyAddressTranslator>
        (
            PrivateConstructor{},
            objectMaker
        );
        periodicallySetPublicAddress(self);
        return self;
    }

    ProxyAddressTranslator::ProxyAddressTranslator
    (
        PrivateConstructor,
        const IOManager::ObjectMaker& objectMaker
    ) :
        objectMaker{ objectMaker },
        publicAddress{}
    {
    }

    ProxyAddressTranslator::AddressV4 ProxyAddressTranslator::getPublicAddress() const
    {
        const auto lock = std::scoped_lock{ this->mutex };
        return this->publicAddress;
    }

    void ProxyAddressTranslator::setPublicAddress(const AddressV4& newPublicAddress)
    {
        {
            const auto lock = std::scoped_lock{ this->mutex };
            this->publicAddress = newPublicAddress;
        }
        log(LogLevel::info, "Public address updated to ", newPublicAddress);
    }

    ProxyAddressTranslator::UDPEndPoint ProxyAddressTranslator::localToPublic
    (
        const UDPEndPoint& endPoint
    ) const
    {
        auto publicEndPoint = endPoint;
        publicEndPoint.address(this->getPublicAddress());
        return publicEndPoint;
    }

    void ProxyAddressTranslator::periodicallySetPublicAddress
    (
        const std::weak_ptr<ProxyAddressTranslator>& ref
    )
    {
        auto self = ref.lock();
        if (!self)
        {
            log(LogLevel::info, "ProxyAddressTranslator expired, not updating anymore");
            return;
        }

        log(LogLevel::info, "Will update public address now.");

        const auto action = [](ProxyAddressTranslator& self, std::string newIP)
        {
            boost::algorithm::trim(newIP);
            log(LogLevel::info, "Retrieved public IP address: ", newIP);
            self.setPublicAddress(AddressV4::from_string(newIP));
        };
        Utility::asyncHttpGet
        (
            self->objectMaker,
            "api.ipify.org",
            "/",
            Utility::makeWeakHandler(self, action)
        );

        using Timer = boost::asio::steady_timer;
        const auto timer = std::make_shared<Timer>(self->objectMaker.make<Timer>());
        timer->expires_after(std::chrono::minutes{ 1 });
        timer->async_wait([ref, timer](const ErrorCode& code)
        {
            if (code.failed())
            {
                log(LogLevel::error, "Address Updater: async wait failed");
                return;
            }
            periodicallySetPublicAddress(ref);
        });
    }
}