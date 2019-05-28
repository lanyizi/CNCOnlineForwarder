#include "precompiled.h"
#include "GameConnection.h"
#include "Logging.h"
#include "NatNegProxy.h"
#include "SimpleWriteHandler.hpp"
#include "WeakRefHandler.hpp"

using UDP = boost::asio::ip::udp;
using ErrorCode = boost::system::error_code;
using LogLevel = CNCOnlineForwarder::Logging::Level;
using WriteHandler = CNCOnlineForwarder::Utility::SimpleWriteHandler<CNCOnlineForwarder::NatNeg::GameConnection>;
using CNCOnlineForwarder::Utility::makeWeakHandler;

namespace CNCOnlineForwarder::NatNeg
{
    template<typename... Arguments>
    void logLine(LogLevel level, Arguments&&... arguments)
    {
        return Logging::logLine<GameConnection>(level, std::forward<Arguments>(arguments)...);
    }

    template<typename Handler = void>
    class GameConnection::ReceiveHandler
    {
    public:
        friend ReceiveHandler<void>;

        template<typename InputHandler>
        static auto create(GameConnection* pointer, InputHandler handler)
        {
            return makeWeakHandler
            (
                pointer,
                ReceiveHandler<InputHandler>{ std::move(handler) }
            );
        }

        std::string& getData() const noexcept
        {
            return *this->data;
        }

        boost::asio::mutable_buffer getBuffer() const noexcept
        {
            return boost::asio::buffer(this->getData());
        }

        EndPoint& getFrom() const noexcept
        {
            return *this->from;
        }

        void operator()(GameConnection& self, const ErrorCode& code, const std::size_t bytesReceived) const
        {
            if (code.failed())
            {
                logLine(LogLevel::error, "Async receive failed: ", code);
            }

            this->getData().resize(bytesReceived);
            return this->handler(self, std::move(this->getData()), this->getFrom());
        }

    private:
        // allow instantiation of ReceiveHandler<>
        using FinalHandlerType = std::conditional_t<std::is_void_v<Handler>, void*, Handler>;

        ReceiveHandler(FinalHandlerType handler) :
            data{ std::make_unique<std::string>(1024, '\0') },
            handler{ std::move(handler) }
        {}

        std::unique_ptr<std::string> data;
        std::unique_ptr<EndPoint> from;
        FinalHandlerType handler;
    };

    std::shared_ptr<GameConnection> GameConnection::create
    (
        IOManager::ObjectMaker objectMaker,
        const std::weak_ptr<NatNegProxy>& proxy,
        const EndPoint& server,
        const EndPoint& client
    )
    {
        const auto self = std::make_shared<GameConnection>
        (
            PrivateConstructor{},
            objectMaker, 
            proxy, 
            server, 
            client
        );

        const auto action = [](GameConnection& self)
        {
            logLine(LogLevel::info, "New Connection ", &self, " created, client address ", self.clientPublicAddress);
            self.prepareForNextPacketToPlayer();
        };
        boost::asio::defer(self->strand, makeWeakHandler(self.get(), action));

        return self;
    }

    GameConnection::GameConnection
    (
        PrivateConstructor,
        IOManager::ObjectMaker objectMaker,
        const std::weak_ptr<NatNegProxy>& proxy,
        const EndPoint& server,
        const EndPoint& clientPublicAddress
    ) :
        strand{ objectMaker.makeStrand() },
        proxy{ proxy },
        server{ server },
        clientPublicAddress{ clientPublicAddress },
        clientRealAddress{ clientPublicAddress },
        remotePlayer{},
        publicSocketForClient{ strand, EndPoint{ UDP::v4(), 0 } },
        fakeRemotePlayerSocket{ strand, EndPoint{ UDP::v4(), 0 } },
        timeout{ strand }
    {}

    const GameConnection::EndPoint& GameConnection::getClientPublicAddress() const noexcept
    {
        return this->clientPublicAddress;
    }

    void GameConnection::handlePacketToServer(const PacketView packet)
    {
        auto action = [data = packet.copyBuffer()](GameConnection& self)
        {
            logLine(LogLevel::info, "Sending data to server through client public proxy...");

            const auto packet = PacketView{ {data} };
            if (!packet.isNatNeg())
            {
                logLine(LogLevel::warning, "Packet from server is not NatNeg, discarded.");
                return;
            }

            auto writeHandler = WriteHandler{ packet.natNegPacket };
            self.publicSocketForClient.asyncSendTo
            (
                writeHandler.getData(),
                self.server,
                std::move(writeHandler)
            );

            self.extendLife();
        };

        boost::asio::defer(this->strand, makeWeakHandler(this, std::move(action)));
    }

    void GameConnection::handleCommunicationPacketFromServer
    (
        const PacketView packet,
        const EndPoint& communicationAddress
    )
    {
        auto action = [data = packet.copyBuffer(), communicationAddress](GameConnection& self)
        {
            return self.handleCommunicationPacketFromServerInternal
            (
                PacketView{ {data} },
                communicationAddress
            );
        };

        boost::asio::defer(this->strand, makeWeakHandler(this, std::move(action)));
    }

    void GameConnection::extendLife()
    {
        auto waitHandler = [self = this->shared_from_this()](const ErrorCode& code){
            if (code == boost::asio::error::operation_aborted)
            {
                return;
            }

            if (code.failed())
            {
                logLine(LogLevel::error, "Async wait failed: ", code);
            }

            logLine(LogLevel::error, "Timeout reached, closing self: ", self.get());
        };

        this->timeout.asyncWait(std::chrono::minutes{ 1 }, std::move(waitHandler));
    }

    void GameConnection::prepareForNextPacketFromPlayer()
    {
        const auto forwarder = [](GameConnection& self, std::string&& data, const EndPoint& from)
        {
            return self.handlePacketToRemotePlayer(std::move(data), from);
        };
        auto handler = ReceiveHandler<>::create(this, forwarder);
        this->fakeRemotePlayerSocket.asyncReceiveFrom
        (
            handler->getBuffer(), 
            handler->getFrom(), 
            std::move(handler)
        );
    }

    void GameConnection::prepareForNextPacketToPlayer()
    {
        const auto forwarder = [](GameConnection& self, std::string&& data, const EndPoint& from)
        {
            if (from == self.server)
            {
                return self.handlePacketFromServer(PacketView{ {data} });
            }

            return self.handlePacketFromRemotePlayer(std::move(data), from);
        };
        auto handler = ReceiveHandler<>::create(this, forwarder);
        this->fakeRemotePlayerSocket.asyncReceiveFrom
        (
            handler->getBuffer(),
            handler->getFrom(),
            std::move(handler)
        );
    }

    void GameConnection::handlePacketFromServer(const PacketView packet)
    {
        const auto proxy = this->proxy.lock();
        if (!proxy)
        {
            logLine(LogLevel::warning, "Proxy already died when handling packet from server");
            return;
        }

        if (!packet.isNatNeg())
        {
            logLine(LogLevel::warning, "Packet from server is not NatNeg, discarded.");
            return;
        }

        logLine(LogLevel::info, "Packet from server will be send to client from proxy.");
        proxy->sendFromProxySocket(packet, this->clientPublicAddress);

        this->extendLife();
    }

    void GameConnection::handleCommunicationPacketFromServerInternal
    (
        const PacketView packet,
        const EndPoint& communicationAddress
    )
    {
        const auto proxy = this->proxy.lock();
        if (!proxy)
        {
            logLine(LogLevel::warning, "Proxy already died when handling CommPacket from server");
            return;
        }

        auto buffer = std::string{ packet.natNegPacket };
        const auto addressOffset = PacketView::getAddressOffset(packet.getStep());
        if (addressOffset.has_value())
        {
            logLine(LogLevel::info, "CommPacket contains address, will try to rewrite it");

            const auto fakeRemotePlayerAddress = fakeRemotePlayerSocket->local_endpoint();
            const auto publicRemoteFakeAddress = proxy->localToPublicEndPoint(fakeRemotePlayerAddress);
            const auto ip = publicRemoteFakeAddress.address().to_v4().to_bytes();
            const auto port = boost::endian::native_to_big(publicRemoteFakeAddress.port());
            rewriteAddress(buffer, addressOffset.value(), ip, port);

            logLine(LogLevel::info, "Address rewritten as ", publicRemoteFakeAddress);
            logLine(LogLevel::info, "Preparing to receive packet from player to fakeRemote");
            this->prepareForNextPacketFromPlayer();
        }
        logLine(LogLevel::info, "CommPacket from server will be send to client from proxy.");
        proxy->sendFromProxySocket(PacketView{ buffer }, communicationAddress);

        this->extendLife();
    }

    void GameConnection::handlePacketFromRemotePlayer(std::string&& packet, const EndPoint& from)
    {
        if (this->remotePlayer != from)
        {
            logLine(LogLevel::warning, "Updating remote address from ", this->remotePlayer, " to ", from);
        }

        auto writeHandler = WriteHandler{ std::move(packet) };
        this->fakeRemotePlayerSocket.asyncSendTo
        (
            writeHandler.getData(),
            this->clientRealAddress,
            std::move(writeHandler)
        );
#ifdef _DEBUG
        logLine(LogLevel::trace, "Packet from remote ", this->remotePlayer, " now forwarded to ", this->clientRealAddress);
#endif

        this->extendLife();
    }

    void GameConnection::handlePacketToRemotePlayer(std::string&& packet, const EndPoint& from)
    {
        auto writeHandler = WriteHandler{ std::move(packet) };
        this->publicSocketForClient.asyncSendTo
        (
            writeHandler.getData(),
            this->remotePlayer,
            std::move(writeHandler)
        );

#ifdef _DEBUG
        logLine(LogLevel::trace, "Packet from client ", this->clientRealAddress, " now forwarded to ", this->remotePlayer);
#endif

        if (from != this->clientRealAddress)
        {
            logLine(LogLevel::warning, "Updating client address from ", this->clientRealAddress, " to ", from);
            this->clientRealAddress = from;
        }

        this->extendLife();
    }
}