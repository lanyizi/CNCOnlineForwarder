#pragma once
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/udp.hpp>

namespace CNCOnlineForwarder
{
    class ProxyAddressTranslator
    {
    public:
        using AddressV4 = boost::asio::ip::address_v4;
        using UDPEndPoint = boost::asio::ip::udp::endpoint;

        ProxyAddressTranslator(const AddressV4& serverPublicAddress);

        UDPEndPoint localToPublic(const UDPEndPoint& endPoint) const;
    private:
        AddressV4 serverPublicAddress;
    };
}



