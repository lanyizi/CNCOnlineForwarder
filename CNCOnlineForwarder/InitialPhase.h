#pragma once
#include <array>
#include <memory>
#include <functional>
#include <optional>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include "IOManager.hpp"
#include "NatNegPacket.hpp"
#include "ProxyAddressTranslator.h"

namespace CNCOnlineForwarder::NatNeg
{
    class NatNegProxy;
    class GameConnection;

    class InitialPhase : public std::enable_shared_from_this<InitialPhase>
    {
    private:
        class ReceiveHandler;
        struct PrivateConstructor {};
    public:
        using Strand = IOManager::StrandType;
        using EndPoint = boost::asio::ip::udp::endpoint;
        using Socket = WithStrand<boost::asio::ip::udp::socket>;
        using Timer = WithStrand<boost::asio::steady_timer>;
        using Resolver = WithStrand<boost::asio::ip::udp::resolver>;
        using NatNegPlayerID = NatNegPlayerID;
        using PacketView = NatNegPacketView;

        static constexpr auto description = "InitialPhase";

        static std::shared_ptr<InitialPhase> create
        (
            const IOManager::ObjectMaker& objectMaker,
            const std::weak_ptr<NatNegProxy>& proxy,
            const NatNegPlayerID id,
            const std::string& natNegServer,
            const std::uint16_t natNegPort
        );

        InitialPhase
        (
            PrivateConstructor,
            const IOManager::ObjectMaker& objectMaker,
            const std::weak_ptr<NatNegProxy>& proxy,
            const NatNegPlayerID id
        );

        void prepareGameConnection
        (
            const IOManager::ObjectMaker& objectMaker,
            const std::weak_ptr<ProxyAddressTranslator>& addressTranslator,
            const EndPoint& client
        );

        void handlePacketToServer(const PacketView packet, const EndPoint& from);

    private:
        void checkPendingActions();

        void close();

        void extendLife();

        void prepareForNextPacketToCommunicationAddress();

        void handlePacketFromServer(const PacketView packet);

        void handlePacketToServerInternal(const PacketView packet, const EndPoint& from);

        Strand strand;
        Resolver resolver;
        Socket communicationSocket;
        Timer timeout;

        std::weak_ptr<NatNegProxy> proxy;
        std::weak_ptr<GameConnection> connection;

        NatNegPlayerID id;
        std::optional<EndPoint> server;
        EndPoint clientCommunication;

        std::optional<std::vector<std::pair<std::string, EndPoint>>> pendingDataToServer;
        std::function<void()> pendingConnectionMaker;
    };
}