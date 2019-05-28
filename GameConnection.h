#pragma once
#include <array>
#include <memory>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include "IOManager.hpp"
#include "NatNegPacket.hpp"

namespace CNCOnlineForwarder::NatNeg
{
    class NatNegProxy;
    class InitialPhase;

    class GameConnection : public std::enable_shared_from_this<GameConnection>
    {
    private:
        struct PrivateConstructor {};

        template<typename Handler>
        class ReceiveHandler;
    public:
        using Strand = IOManager::StrandType;
        using EndPoint = boost::asio::ip::udp::endpoint;
        using Socket = WithStrand<boost::asio::ip::udp::socket>;
        using Timer = WithStrand<boost::asio::steady_timer>;
        using NatNegPlayerID = NatNegPlayerID;
        using PacketView = NatNegPacketView;

        static constexpr auto description = "GameConnection";

        static std::shared_ptr<GameConnection> create
        (
            IOManager::ObjectMaker objectMaker,
            const std::weak_ptr<NatNegProxy>& proxy,
            const EndPoint& server,
            const EndPoint& clientPublicAddress
        );

        GameConnection
        (
            PrivateConstructor,
            IOManager::ObjectMaker objectMaker,
            const std::weak_ptr<NatNegProxy>& proxy,
            const EndPoint& server,
            const EndPoint& clientPublicAddress
        );

        const EndPoint& getClientPublicAddress() const noexcept;

        void handlePacketToServer(const PacketView packet);

        void handleCommunicationPacketFromServer
        (
            const PacketView packet,
            const EndPoint& communicationAddress
        );

    private:
        void extendLife();

        void prepareForNextPacketFromPlayer();

        void prepareForNextPacketToPlayer();

        void handlePacketFromServer(const PacketView packet);

        void handleCommunicationPacketFromServerInternal
        (
            const PacketView packet,
            const EndPoint& communicationAddress
        );

        void handlePacketFromRemotePlayer(std::string&& packet, const EndPoint& from);

        void handlePacketToRemotePlayer(std::string&& packet, const EndPoint& from);

        Strand strand;
        std::weak_ptr<NatNegProxy> proxy;
        EndPoint server;
        EndPoint clientPublicAddress;
        EndPoint clientRealAddress;
        EndPoint remotePlayer;
        Socket publicSocketForClient;
        Socket fakeRemotePlayerSocket;
        Timer timeout;
    };
}

