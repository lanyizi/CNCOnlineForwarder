#include "precompiled.h"
#include "GameConnection.h"
#include "Logging.h"
#include "NatNegProxy.h"
#include "ProxyAddressTranslator.h"
#include "WeakRefHandler.hpp"

using UDP = boost::asio::ip::udp;
using ErrorCode = boost::system::error_code;
using LogLevel = CNCOnlineForwarder::Logging::Level;

using CNCOnlineForwarder::Utility::makeDebugWeakHandler;

namespace CNCOnlineForwarder::NatNeg
{
    template<typename... Arguments>
    void logLine(LogLevel level, Arguments&&... arguments)
    {
        return Logging::logLine<GameConnection>(level, std::forward<Arguments>(arguments)...);
    }

    template<typename NextAction, typename Handler>
    class ReceiveHandler
    {
    public:
        template<typename InputNextAction, typename InputNextHandler>
        ReceiveHandler(InputNextAction&& nextAction, InputNextHandler&& handler) :
            size{ 512 },
            buffer{ std::make_unique<char[]>(this->size) },
            from{ std::make_unique<GameConnection::EndPoint>() },
            nextAction{ std::forward<InputNextAction>(nextAction) },
            handler{ std::forward<InputNextHandler>(handler) }
        {}

        boost::asio::mutable_buffer getBuffer() const noexcept
        {
            return boost::asio::buffer(this->buffer.get(), this->size);
        }

        GameConnection::EndPoint& getFrom() const noexcept
        {
            return *this->from;
        }

        void operator()
        (
            GameConnection& self, 
            const ErrorCode& code, 
            const std::size_t bytesReceived
        )
        {
            this->nextAction(self);

            if (code.failed())
            {
                logLine(LogLevel::error, "Async receive failed: ", code);
                return;
            }

            if (bytesReceived >= this->size)
            {
                logLine(LogLevel::warning, "Received data may be truncated: ", bytesReceived, "/",  this->size);
            }

            return this->handler
            (
                self, 
                std::move(this->buffer), 
                bytesReceived, 
                this->getFrom()
            );
        }

    private:
        std::size_t size;
        GameConnection::Buffer buffer;
        std::unique_ptr<GameConnection::EndPoint> from;
        NextAction nextAction;
        Handler handler;
    };

    template<typename NextAction, typename Handler>
    auto makeReceiveHandler
    (
        std::string what,
        GameConnection* pointer, 
        NextAction&& nextAction, 
        Handler&& hanlder
    )
    {
        using NextActionValue = std::remove_reference_t<NextAction>;
        using HandlerValue = std::remove_reference_t<Handler>;

        return makeDebugWeakHandler
        (
            std::move(what),
            pointer, 
            ReceiveHandler<NextActionValue, HandlerValue>
            {
                std::forward<NextAction>(nextAction),
                std::forward<Handler>(hanlder)
            }
        );
    }

    class SendHandler
    {
    public:
        SendHandler
        (
            GameConnection::Buffer buffer,
            const std::size_t bytes
        ) :
            buffer{ std::move(buffer) },
            bytes{ bytes }
        {}

        boost::asio::const_buffer getBuffer() const noexcept
        {
            return boost::asio::buffer(this->buffer.get(), this->bytes);
        }

        void operator()(const ErrorCode& code, std::size_t bytesSent) const
        {
            if (code.failed())
            {
                logLine(LogLevel::error, "Async write failed: ", code);
                return;
            }

            if (bytesSent != this->bytes)
            {
                logLine(LogLevel::error, "Only part of packet was sent: ", bytesSent, "/", this->bytes);
                return;
            }
        }

    private:
        GameConnection::Buffer buffer;
        std::size_t bytes;
    };

    std::shared_ptr<GameConnection> GameConnection::create
    (
        const IOManager::ObjectMaker& objectMaker,
        const std::weak_ptr<NatNegProxy>& proxy,
        const std::weak_ptr<ProxyAddressTranslator>& addressTranslator,
        const EndPoint& server,
        const EndPoint& client
    )
    {
        const auto self = std::make_shared<GameConnection>
        (
            PrivateConstructor{},
            objectMaker, 
            proxy, 
            addressTranslator,
            server, 
            client
        );

        const auto action = [self]
        {
            logLine(LogLevel::info, "New Connection ", self, " created, client = ", self->clientPublicAddress);
            self->extendLife();
            self->prepareForNextPacketToClient();
        };
        boost::asio::defer(self->strand, action);

        return self;
    }

    GameConnection::GameConnection
    (
        PrivateConstructor,
        const IOManager::ObjectMaker& objectMaker,
        const std::weak_ptr<NatNegProxy>& proxy,
        const std::weak_ptr<ProxyAddressTranslator>& addressTranslator,
        const EndPoint& server,
        const EndPoint& clientPublicAddress
    ) :
        strand{ objectMaker.makeStrand() },
        proxy{ proxy },
        addressTranslator{ addressTranslator },
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
            const auto packet = PacketView{ data };
            if (!packet.isNatNeg())
            {
                logLine(LogLevel::warning, "Packet to server is not NatNeg, discarded.");
                return;
            }

            logLine(LogLevel::info, "Packet to server handler: NatNeg step ", packet.getStep());
            logLine(LogLevel::info, "Sending data to server through client public socket...");

            const auto& packetContent = packet.natNegPacket;
            auto copy = std::make_unique<char[]>(packetContent.size());
            std::copy_n(packetContent.begin(), packetContent.size(), copy.get());
            auto handler = SendHandler{ std::move(copy), packetContent.size() };
            self.publicSocketForClient.asyncSendTo
            (
                handler.getBuffer(),
                self.server,
                std::move(handler)
            );

            self.extendLife();
        };

        boost::asio::defer(this->strand, makeDebugWeakHandler("handlePacketToServer", this, std::move(action)));
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
                PacketView{ data },
                communicationAddress
            );
        };

        boost::asio::defer(this->strand, makeDebugWeakHandler("handleCommunicationPacketFromServer", this, std::move(action)));
    }

    void GameConnection::extendLife()
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

            logLine(LogLevel::error, "Timeout reached, closing self: ", self.get());
        };

        this->timeout.asyncWait(std::chrono::minutes{ 1 }, std::move(waitHandler));
    }

    void GameConnection::prepareForNextPacketFromClient()
    {
        const auto then = [](GameConnection& self)
        {
            return self.prepareForNextPacketFromClient();
        };

        const auto dispatcher = []
        (
            GameConnection& self, 
            Buffer&& data, 
            const std::size_t size,
            const EndPoint& from
        )
        {
            return self.handlePacketToRemotePlayer(std::move(data), size, from);
        };

        auto handler = makeReceiveHandler("prepareForNextPacketFromClient", this, then, dispatcher);
        this->fakeRemotePlayerSocket.asyncReceiveFrom
        (
            handler->getBuffer(),
            handler->getFrom(),
            std::move(handler)
        );
    }

    void GameConnection::prepareForNextPacketToClient()
    {
        const auto then = [](GameConnection& self)
        {
            return self.prepareForNextPacketToClient();
        };

        const auto dispatcher = []
        (
            GameConnection& self, 
            Buffer&& data,
            const std::size_t size,
            const EndPoint& from
        )
        {
            self.prepareForNextPacketToClient();

            if (from == self.server)
            {
                return self.handlePacketFromServer(std::move(data), size);
            }

            return self.handlePacketFromRemotePlayer(std::move(data), size, from);
        };
        auto handler = makeReceiveHandler("prepareForNextPacketToClient", this, then, dispatcher);
        this->publicSocketForClient.asyncReceiveFrom
        (
            handler->getBuffer(),
            handler->getFrom(),
            std::move(handler)
        );
    }

    void GameConnection::handlePacketFromServer(Buffer buffer, const std::size_t size)
    {
        const auto proxy = this->proxy.lock();
        if (!proxy)
        {
            logLine(LogLevel::warning, "Proxy already died when handling packet from server");
            return;
        }

        const auto packet = PacketView{ { buffer.get(), size } };

        if (!packet.isNatNeg())
        {
            logLine(LogLevel::warning, "Packet from server is not NatNeg, discarded.");
            return;
        }

        logLine(LogLevel::info, "Packet from server handler: NatNeg step ", packet.getStep());
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
            logLine(LogLevel::error, "Proxy already died when handling CommPacket from server");
            return;
        }

        logLine(LogLevel::info, "CommPacket handler: NatNeg step ", packet.getStep());

        auto outputBuffer = std::string{ packet.natNegPacket };
        const auto addressOffset = PacketView::getAddressOffset(packet.getStep());
        if (addressOffset.has_value())
        {
            logLine(LogLevel::info, "CommPacket contains address, will try to rewrite it");

            {
                const auto[ip, port] = parseAddress(packet.natNegPacket, addressOffset.value());
                this->remotePlayer.address(boost::asio::ip::address_v4{ ip });
                this->remotePlayer.port(boost::endian::big_to_native(port));
                logLine(LogLevel::info, "CommPacket's address stored in this->remotePlayer: ", this->remotePlayer);
            }

            const auto fakeRemotePlayerAddress = fakeRemotePlayerSocket->local_endpoint();
            logLine(LogLevel::info, "FakeRemote local endpoint:", fakeRemotePlayerAddress);
            const auto addressTranslator = this->addressTranslator.lock();
            if (!addressTranslator)
            {
                logLine(LogLevel::error, "AddressTranslator already died when rewriting CommPacket");
                return;
            }
            const auto publicRemoteFakeAddress = 
                addressTranslator->localToPublic(fakeRemotePlayerAddress);
            const auto ip = publicRemoteFakeAddress.address().to_v4().to_bytes();
            const auto port = boost::endian::native_to_big(publicRemoteFakeAddress.port());
            rewriteAddress(outputBuffer, addressOffset.value(), ip, port);

            logLine(LogLevel::info, "Address rewritten as ", publicRemoteFakeAddress);
            logLine(LogLevel::info, "Preparing to receive packet from player to fakeRemote");
            this->prepareForNextPacketFromClient();
        }
        logLine(LogLevel::info, "CommPacket from server will be send to client from proxy.");
        proxy->sendFromProxySocket(PacketView{ outputBuffer }, communicationAddress);

        this->extendLife();
    }

    void GameConnection::handlePacketFromRemotePlayer
    (
        Buffer buffer,
        const std::size_t size,
        const EndPoint& from
    )
    {
        if (this->remotePlayer != from)
        {
            logLine(LogLevel::warning, "Updating remote player address from ", this->remotePlayer, " to ", from);
            this->remotePlayer = from;
        }

        if (PacketView{ { buffer.get(), size } }.isNatNeg())
        {
            logLine(LogLevel::info, "Forwarding NatNeg Packet from remote ", this->remotePlayer, " to ", this->clientRealAddress);
        }

        auto handler = SendHandler{ std::move(buffer), size };
        this->fakeRemotePlayerSocket.asyncSendTo
        (
            handler.getBuffer(),
            this->clientRealAddress,
            std::move(handler)
        );

        this->extendLife();
    }

    void GameConnection::handlePacketToRemotePlayer
    (
        Buffer buffer,
        const std::size_t size,
        const EndPoint& from
    )
    {
        if (from != this->clientRealAddress)
        {
            logLine(LogLevel::warning, "Updating client address from ", this->clientRealAddress, " to ", from);
            this->clientRealAddress = from;
        }

        if (PacketView{ { buffer.get(), size } }.isNatNeg())
        {
            logLine(LogLevel::info, "Forwarding NatNeg Packet from client ", this->remotePlayer, " to ", this->clientRealAddress);
        }

        auto handler = SendHandler{ std::move(buffer), size };
        this->publicSocketForClient.asyncSendTo
        (
            handler.getBuffer(),
            this->remotePlayer,
            std::move(handler)
        );

        this->extendLife();
    }
}