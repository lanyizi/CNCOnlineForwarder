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
#include "PendingActions.hpp"
//#include "PromisedReady.hpp"

namespace CNCOnlineForwarder::NatNeg
{
    class NatNegProxy;
    class GameConnection;

    class InitialPhase : public std::enable_shared_from_this<InitialPhase>
    {
    private:
        class ReceiveHandler;
        struct PromisedEndPoints;
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

        InitialPhase(const InitialPhase&) = delete;
        InitialPhase& operator=(const InitialPhase&) = delete;

        void prepareGameConnection
        (
            const IOManager::ObjectMaker& objectMaker,
            const std::weak_ptr<ProxyAddressTranslator>& addressTranslator,
            const EndPoint& client
        );

        void handlePacketToServer(const PacketView packet, const EndPoint& from);

    private:
        struct PromisedEndPoint
        {
            using ActionType = std::function<void(const EndPoint&)>;

            template<typename Action>
            void apply(Action&& action)
            {
                action(this->endPoint.value());
            }

            bool isReady() const noexcept
            {
                return this->endPoint.has_value();
            }

            std::optional<EndPoint> endPoint;
        };

        struct PromisedConnection
        {
            using ActionType = std::function<void(std::weak_ptr<GameConnection>)>;

            template<typename Action>
            void apply(Action&& action)
            {
                action(this->ref);
            }

            bool isReady() const noexcept
            {
                const auto defaultValue = decltype(this->ref){};
                // from https://stackoverflow.com/a/45507610/4399840
                const auto isNotAssignedYet = 
                    (!this->ref.owner_before(defaultValue))
                    &&
                    (!defaultValue.owner_before(this->ref));
                return !isNotAssignedYet;
            }

            std::weak_ptr<GameConnection> ref;
        };

        using FutureEndPoint = Utility::PendingActions<PromisedEndPoint>;
        using FutureConnection = Utility::PendingActions<PromisedConnection>;
        //using FutureSocketReady = Utility::PendingReadyState;

        void close();

        void extendLife();

        void prepareForNextPacketToCommunicationAddress();

        void handlePacketFromServer(const PacketView packet);

        void handlePacketToServerInternal
        (
            const PacketView packet,
            const EndPoint& from,
            const EndPoint& server
        );

        Strand strand;
        Resolver resolver;
        Socket communicationSocket;
        Timer timeout;

        std::weak_ptr<NatNegProxy> proxy;
        FutureConnection connection;

        NatNegPlayerID id;
        FutureEndPoint server;
        EndPoint clientCommunication;
        //FutureSocketReady socketReadyToReceive;
    };
}