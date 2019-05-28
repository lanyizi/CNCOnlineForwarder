#include "precompiled.h"
#include "ProxyAddressTranslator.h"

namespace CNCOnlineForwarder
{
    ProxyAddressTranslator::ProxyAddressTranslator
    (
        const AddressV4& serverPublicAddress
    ) : serverPublicAddress{ serverPublicAddress }
    {
    }

    ProxyAddressTranslator::UDPEndPoint ProxyAddressTranslator::localToPublic
    (
        const UDPEndPoint& endPoint
    ) const
    {
        auto publicEndPoint = endPoint;
        publicEndPoint.address(this->serverPublicAddress);
        return publicEndPoint;
    }
}