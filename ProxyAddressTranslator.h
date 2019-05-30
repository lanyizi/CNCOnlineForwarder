#pragma once
#include <memory>
#include <mutex>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/udp.hpp>
#include "IOManager.hpp"

namespace CNCOnlineForwarder
{
    class ProxyAddressTranslator : public std::enable_shared_from_this<ProxyAddressTranslator>
    {
    private:
        struct PrivateConstructor {};
    public:
        using AddressV4 = boost::asio::ip::address_v4;
        using UDPEndPoint = boost::asio::ip::udp::endpoint;

        static constexpr auto description = "ProxyAddressTranslator";

        static std::shared_ptr<ProxyAddressTranslator> create
        (
            const IOManager::ObjectMaker& objectMaker
        );

        ProxyAddressTranslator
        (
            PrivateConstructor,
            const IOManager::ObjectMaker& objectMaker
        );

        AddressV4 getPublicAddress() const;

        void setPublicAddress(const AddressV4& newPublicAddress);

        UDPEndPoint localToPublic(const UDPEndPoint& endPoint) const;

    private:
        static void periodicallySetPublicAddress
        (
            const std::weak_ptr<ProxyAddressTranslator>& ref
        );

        IOManager::ObjectMaker objectMaker;
        mutable std::mutex mutex;
        AddressV4 publicAddress;
    };
}



