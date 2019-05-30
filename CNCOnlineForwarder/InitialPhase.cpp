#include "precompiled.h"
#include "InitialPhase.h"
#include "GameConnection.h"
#include "Logging.h"
#include "NatNegProxy.h"
#include "SimpleWriteHandler.hpp"
#include "WeakRefHandler.hpp"

using AddressV4 = boost::asio::ip::address_v4;
using UDP = boost::asio::ip::udp;
using Resolved = boost::asio::ip::udp::resolver::results_type;
using ErrorCode = boost::system::error_code;
using LogLevel = CNCOnlineForwarder::Logging::Level;
using WriteHandler = CNCOnlineForwarder::Utility::SimpleWriteHandler<CNCOnlineForwarder::NatNeg::InitialPhase>;
using CNCOnlineForwarder::Utility::makeWeakHandler;

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
            }

            const auto packet = PacketView{ {this->buffer->data(), bytesReceived} };
            return self.handlePacketFromServer(packet);
        }

    private:
        ReceiveHandler() :
            buffer{ std::make_unique<std::array<char, 1024>>() }
        {}

        std::unique_ptr<EndPoint> from;
        std::unique_ptr<std::array<char, 1024>> buffer;
    };

    std::shared_ptr<InitialPhase> InitialPhase::create
    (
        const IOManager::ObjectMaker& objectMaker,
        const std::weak_ptr<NatNegProxy>& proxy,
        const std::weak_ptr<ProxyAddressTranslator>& addressTranslator,
        const NatNegPlayerID id,
        const EndPoint& client,
        const std::string& serverHostName,
        const std::uint16_t serverPort
    )
    {
        const auto self = std::make_shared<InitialPhase>
        (
            PrivateConstructor{}, 
            objectMaker, 
            proxy, 
            id
        );

        const auto onHostNameResolved = [objectMaker, addressTranslator, client]
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

            self.server = *resolved;
            self.connection = GameConnection::create
            (
                objectMaker,
                self.proxy,
                addressTranslator,
                self.server,
                client
            );
            self.prepareForNextPacketToCommunicationAddress();

            auto pendingActions = std::move(self.pendingActions.value());
            self.pendingActions.reset();
            for (auto&[data, destination] : pendingActions)
            {
                self.handlePacketToServerInternal(PacketView{ {data} }, destination);
            }
        };
        const auto action = [serverHostName, serverPort, onHostNameResolved](InitialPhase& self)
        {
            logLine(LogLevel::info, "InitialPhase creating, id = ", self.id);
            self.extendLife();

            self.resolver.asyncResolve
            (
                serverHostName,
                std::to_string(serverPort),
                makeWeakHandler(&self, onHostNameResolved)
            );
        };
        boost::asio::defer(self->strand, makeWeakHandler(self, action));

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
        communicationSocket{ strand, UDP::v4() },
        timeout{ strand },
        proxy{ proxy },
        connection{},
        id{ id },
        server{},
        clientCommunication{},
        pendingActions{}
    {}

    void InitialPhase::handlePacketToServer(const PacketView packet, const EndPoint& from)
    {
        auto action = [data = packet.copyBuffer(), from](InitialPhase& self)
        {
            const auto packet = PacketView{ {data} };
            self.handlePacketToServerInternal(packet, from);
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
        auto handler = ReceiveHandler::create(this);
        this->communicationSocket.asyncReceiveFrom
        (
            handler->getBuffer(),
            handler->getFrom(),
            std::move(handler)
        );
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
        const auto connection = this->connection.lock();
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

    void InitialPhase::handlePacketToServerInternal(const PacketView packet, const EndPoint& from)
    {
        const auto connection = this->connection.lock();
        if (!connection)
        {
            logLine(LogLevel::warning, "Packet to server handler: aborting because connection expired");
            this->close();
            return;
        }

        if (from == connection->getClientPublicAddress())
        {
            return connection->handlePacketToServer(packet);
        }

        if (!packet.isNatNeg())
        {
            logLine(LogLevel::warning, "Packet from server is not NatNeg, discarded.");
            return;
        }

        logLine(LogLevel::info, "Updating clientCommunication endpoint to ", from);
        this->clientCommunication = from;

        auto writeHandler = WriteHandler{ packet.copyBuffer() };
        this->communicationSocket.asyncSendTo
        (
            writeHandler.getData(),
            this->server,
            std::move(writeHandler)
        );

        this->extendLife();
    }
}
