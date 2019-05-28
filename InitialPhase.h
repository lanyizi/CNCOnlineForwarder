#pragma once
#include <array>
#include <memory>
#include <functional>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include "IOManager.hpp"
#include "NatNegPacket.hpp"

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
        using NatNegPlayerID = NatNegPlayerID;
        using PacketView = NatNegPacketView;

        static constexpr auto description = "InitialPhase";

        static std::shared_ptr<InitialPhase> create
        (
            IOManager::ObjectMaker objectMaker,
            const std::weak_ptr<NatNegProxy>& proxy,
            const NatNegPlayerID id,
            const EndPoint& server,
            const EndPoint& clientPublicAddress
        );

        InitialPhase
        (
            PrivateConstructor,
            IOManager::ObjectMaker objectMaker,
            const std::weak_ptr<NatNegProxy>& proxy,
            const NatNegPlayerID id,
            const EndPoint& server,
            std::weak_ptr<GameConnection>&& connection
        );

        void handlePacketToServer(const PacketView packet, const EndPoint& from);

    private:
        void close();

        void extendLife();

        void prepareForNextPacketToCommunicationAddress();

        void handlePacketFromServer(const PacketView packet);

        Strand strand;
        std::weak_ptr<NatNegProxy> proxy;
        NatNegPlayerID id;
        Socket communicationSocket;
        EndPoint serverAddress;
        EndPoint clientCommunicationAddress;
        Timer timeout;
        std::weak_ptr<GameConnection> connection;
        
    };
}