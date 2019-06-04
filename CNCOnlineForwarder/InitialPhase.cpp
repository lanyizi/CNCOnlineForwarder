#include "precompiled.h"
#include "InitialPhase.h"
#include "GameConnection.h"
#include "Logging.h"
#include "NatNegProxy.h"
//#include "SocketAutoBinder.hpp"
#include "SimpleWriteHandler.hpp"
#include "WeakRefHandler.hpp"

using AddressV4 = boost::asio::ip::address_v4;
using UDP = boost::asio::ip::udp;
using Resolved = boost::asio::ip::udp::resolver::results_type;
using ErrorCode = boost::system::error_code;
using LogLevel = CNCOnlineForwarder::Logging::Level;

using CNCOnlineForwarder::Utility::makeWeakHandler;
using CNCOnlineForwarder::Utility::makeWriteHandler;
//using CNCOnlineForwarder::Utility::SocketAutoBinder::makeWeakWriteHandler;

namespace CNCOnlineForwarder::NatNeg
{
    template<typename... Arguments>
    void logLine(LogLevel level, Arguments&&... arguments)
    {
        return Logging::logLine<InitialPhase>(level, std::forward<Arguments>(arguments)...);
    }

    class InitialPhase::ReceiveHandler
    {
    public:
        static auto create(InitialPhase* pointer)
        {
            return makeWeakHandler(pointer, ReceiveHandler{});
        }

        boost::asio::mutable_buffer getBuffer() 
        { 
            return boost::asio::buffer(*this->buffer); 
        }

        EndPoint& getFrom() 
        { 
            return *this->from; 
        }

        void operator()(InitialPhase& self, const ErrorCode& code, const std::size_t bytesReceived) const
        {
            self.prepareForNextPacketToCommunicationAddress();

            if (code.failed())
            {
                logLine(LogLevel::error, "Receive failed: ", code);
                return;
            }

            // When receiving, server is already resolved
            if (*this->from != self.server->endPoint.value())
            {
                logLine(LogLevel::warning, "Packet is not from server, but from ", *this->from,", discarded");
                return;
            }

            const auto packet = PacketView{ {this->buffer->data(), bytesReceived} };
            return self.handlePacketFromServer(packet);
        }

    private:
        ReceiveHandler() :
            buffer{ std::make_unique<std::array<char, 1024>>() },
            from{ std::make_unique<EndPoint>() }
        {}

        std::unique_ptr<EndPoint> from;
        std::unique_ptr<std::array<char, 1024>> buffer;
    };

    std::shared_ptr<InitialPhase> InitialPhase::create
    (
        const IOManager::ObjectMaker& objectMaker,
        const std::weak_ptr<NatNegProxy>& proxy,
        const NatNegPlayerID id,
        const std::string& natNegServer,
        const std::uint16_t natNegPort
    )
    {
        const auto self = std::make_shared<InitialPhase>
        (
            PrivateConstructor{}, 
            objectMaker, 
            proxy, 
            id
        );

        

        const auto action = [self, natNegServer, natNegPort]
        {
            logLine(LogLevel::info, "InitialPhase creating, id = ", self->id);
            self->extendLife();

            const auto onResolved = []
            (
                InitialPhase& self,
                const ErrorCode& code,
                const Resolved resolved
            )
            {
                if (code.failed())
                {
                    logLine(LogLevel::error, "Failed to resolve server hostname: ", code);
                    return;
                }

                self.server->endPoint = *resolved;
                logLine(LogLevel::info, "server hostname resolved: ", self.server->endPoint.value());
                self.server.trySetReady();
            };
            logLine(LogLevel::info, "Resolving server hostname: ", natNegServer);
            self->resolver.asyncResolve
            (
                natNegServer,
                std::to_string(natNegPort),
                makeWeakHandler(self, onResolved)
            );

            self->server.asyncDo
            (
                [&self = *self](const EndPoint&)
                {
                    logLine(LogLevel::info, "Starting to receive comm packet on local endpoint ", self.communicationSocket->local_endpoint());
                    self.prepareForNextPacketToCommunicationAddress();
                }
            );
        };
        boost::asio::defer(self->strand, action);

        return self;
    }

    InitialPhase::InitialPhase
    (
        PrivateConstructor,
        const IOManager::ObjectMaker& objectMaker,
        const std::weak_ptr<NatNegProxy>& proxy,
        const NatNegPlayerID id
    ) :
        strand{ objectMaker.makeStrand() },
        resolver{ strand },
        communicationSocket{ strand, EndPoint{ UDP::v4(), 0 } },
        timeout{ strand },
        proxy{ proxy },
        connection{ {} },
        id{ id },
        server{ {} }, 
        clientCommunication{}/*,
        socketReadyToReceive{ {} }*/
    {}

    void InitialPhase::prepareGameConnection
    (
        const IOManager::ObjectMaker& objectMaker,
        const std::weak_ptr<ProxyAddressTranslator>& addressTranslator,
        const EndPoint& client
    )
    {
        const auto maker = [this, objectMaker, addressTranslator, client]
        (
            const EndPoint& server
        )
        {
            if (!this->connection->isReady())
            {
                this->connection->ref = GameConnection::create
                (
                    objectMaker,
                    this->proxy,
                    addressTranslator,
                    server,
                    client
                );
            }
            this->connection.trySetReady();
        };
        const auto action = [maker](InitialPhase& self)
        {
            self.server.asyncDo(maker);
        };
        boost::asio::defer(this->strand, makeWeakHandler(this, action));
    }

    void InitialPhase::handlePacketToServer
    (
        const PacketView packet, 
        const EndPoint& from
    )
    {
        auto action = [data = packet.copyBuffer(), from](InitialPhase& self) mutable
        {
            if (const auto packet = PacketView{ {data} }; !packet.isNatNeg())
            {
                logLine(LogLevel::warning, "Packet from server is not NatNeg, discarded.");
                return;
            }

            auto dispatcher = [data = std::move(data), from, &self]
            (
                const std::weak_ptr<GameConnection>& connectionRef
            )
            {
                const auto connection = connectionRef.lock();
                if (!connection)
                {
                    logLine(LogLevel::warning, "Packet to server handler: aborting because connection expired");
                    self.close();
                    return;
                }
                
                const auto packet = PacketView{ {data} };

                if (connection->getClientPublicAddress() == from)
                {
                    connection->handlePacketToServer(packet);
                    return;
                }

                self.handlePacketToServerInternal
                (
                    packet,
                    from, 
                    self.server->endPoint.value() // When connection is ready, server is certainly ready as well
                );
            };
            self.connection.asyncDo(std::move(dispatcher));
        };

        boost::asio::defer(this->strand, makeWeakHandler(this, std::move(action)));
    }

    void InitialPhase::close()
    {
        const auto proxy = this->proxy.lock();
        if (!proxy)
        {
            logLine(LogLevel::warning, "Proxy already died when closing InitialPhase");
            return;
        }
        proxy->removeConnection(this->id);
    }

    void InitialPhase::extendLife()
    {
        auto waitHandler = [self = this->shared_from_this()](const ErrorCode& code)
        {
            if (code == boost::asio::error::operation_aborted)
            {
                return;
            }

            if (code.failed())
            {
                logLine(LogLevel::error, "Async wait failed: ", code);
            }

            logLine(LogLevel::info, "Closing self (natNegId ", self->id, ")");
            self->close();
        };
        this->timeout.asyncWait(std::chrono::minutes{ 1 }, std::move(waitHandler));
    }

    void InitialPhase::prepareForNextPacketToCommunicationAddress()
    {
        /*const auto action = [this]
        {*/
        auto handler = ReceiveHandler::create(this);
        this->communicationSocket.asyncReceiveFrom
        (
            handler->getBuffer(),
            handler->getFrom(),
            std::move(handler)
        );
        /*};
        this->socketReadyToReceive.asyncDo(action);*/
    }

    void InitialPhase::handlePacketFromServer(const PacketView packet)
    {
        const auto proxy = this->proxy.lock();
        if (!proxy)
        {
            logLine(LogLevel::warning, "Proxy already died when handling packet from server");
            this->close();
            return;
        }

        if (!packet.isNatNeg())
        {
            logLine(LogLevel::warning, "Packet from server is not NatNeg, discarded.");
            return;
        }

        logLine(LogLevel::info, "Packet from server will be processed by GameConnection.");
        // When handlePacketFromServer is called, connection should already be ready.
        const auto connection = this->connection->ref.lock();
        if (!connection)
        {
            logLine(LogLevel::warning, "Packet from server handler: aborting because connection expired");
            this->close();
            return;
        }

        connection->handleCommunicationPacketFromServer
        (
            packet, 
            this->clientCommunication
        );

        this->extendLife();
    }

    void InitialPhase::handlePacketToServerInternal
    (
        const PacketView packet, 
        const EndPoint& from,
        const EndPoint& server
    )
    {
        // TODO: Don't update address if packet is init and seqnum is not 1
        logLine(LogLevel::info, "Updating clientCommunication endpoint to ", from);
        this->clientCommunication = from;
        /*auto writeHandler = makeWeakWriteHandler
        (
            packet.copyBuffer(), 
            this, 
            [](InitialPhase& self) { return self.socketReadyToReceive; }
        );*/
        auto writeHandler = makeWriteHandler<InitialPhase>(packet.copyBuffer());
        this->communicationSocket.asyncSendTo
        (
            writeHandler.getData(),
            server,
            std::move(writeHandler)
        );

        this->extendLife();
    }
}
