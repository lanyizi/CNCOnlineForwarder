#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/udp.hpp>
#include "IOManager.hpp"
#include "NatNegPacket.hpp"
#include "ProxyAddressTranslator.h"

namespace CNCOnlineForwarder::NatNeg
{
    class InitialPhase;
    class Connection;

    class NatNegProxy : public std::enable_shared_from_this<NatNegProxy>
    {
    private:
        struct PrivateConstructor{};
        class ReceiveHandler;
    public:
        using Strand = IOManager::StrandType;
        using EndPoint = boost::asio::ip::udp::endpoint;
        using Socket = WithStrand<boost::asio::ip::udp::socket>;
        using AddressV4 = boost::asio::ip::address_v4;
        using NatNegPlayerID = NatNegPlayerID;
        using PacketView = NatNegPacketView;

        static constexpr auto description = "NatNegProxy";

        static std::shared_ptr<NatNegProxy> create
        (
            const IOManager::ObjectMaker& objectMaker,
            const std::string_view serverHostName,
            const std::uint16_t serverPort,
            const std::weak_ptr<ProxyAddressTranslator>& addressTranslator
        );

        NatNegProxy
        (
            PrivateConstructor,
            const IOManager::ObjectMaker& objectMaker,
            const std::string_view serverHostName,
            const std::uint16_t serverPort,
            const std::weak_ptr<ProxyAddressTranslator>& addressTranslator
        );

        void sendFromProxySocket(const PacketView packetView, const EndPoint& to);

        void removeConnection(const NatNegPlayerID id);

    private:
        void prepareForNextPacketToServer();

        void handlePacketToServer(const PacketView packetView, const EndPoint& from);

        IOManager::ObjectMaker objectMaker;
        Strand proxyStrand;
        Socket serverSocket;
        std::string serverHostName;
        std::uint16_t serverPort;
        std::unordered_map<NatNegPlayerID, std::weak_ptr<InitialPhase>, NatNegPlayerID::Hash> initialPhases;
        std::shared_ptr<ProxyAddressTranslator> addressTranslator;
    };
}