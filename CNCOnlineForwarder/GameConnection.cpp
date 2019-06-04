#include "precompiled.h"
#include "GameConnection.h"
#include "Logging.h"
#include "NatNegProxy.h"
#include "ProxyAddressTranslator.h"
#include "SimpleWriteHandler.hpp"
#include "WeakRefHandler.hpp"

using UDP = boost::asio::ip::udp;
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
            from{ std::make_unique<EndPoint>() },
            handler{ std::move(handler) }
        {}

        std::unique_ptr<std::string> data;
        std::unique_ptr<EndPoint> from;
        FinalHandlerType handler;
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

            /*auto writeHandler = makeWeakWriteHandler
            (
                "DummyPacketForAutoBind", 
                self.get(), 
                [](GameConnection& self) 
                {
                    return self.whenFakeRemoteIsReadyToReceive;
                }
            );
            
            self->fakeRemotePlayerSocket.asyncSendTo
            (
                writeHandler.getData(),
            )*/
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
        timeout{ strand }/*,
        whenPublicSocketForClientIsReadyToReceive{ {} },
        whenFakeRemoteIsReadyToReceive{ {} }*/
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

            /*auto writeHandler = makeWeakWriteHandler
            (
                packet.copyBuffer(),
                &self,
                [](const GameConnection& self) 
                {
                    return self.whenFakeRemoteIsReadyToReceive;
                }
            );*/
            auto writeHandler = makeWriteHandler<GameConnection>(packet.copyBuffer());
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
                PacketView{ data },
                communicationAddress
            );
        };

        boost::asio::defer(this->strand, makeWeakHandler(this, std::move(action)));
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
        /*const auto action = [this]
        {*/
        const auto dispatcher = []
        (
            GameConnection& self, 
            std::string&& data, 
            const EndPoint& from
        )
        {
            return self.handlePacketToRemotePlayer(std::move(data), from);
        };
        auto handler = ReceiveHandler<>::create(this, dispatcher);
        this->fakeRemotePlayerSocket.asyncReceiveFrom
        (
            handler->getBuffer(),
            handler->getFrom(),
            std::move(handler)
        );
        /*};

        this->whenFakeRemoteIsReadyToReceive.asyncDo(action);*/
    }

    void GameConnection::prepareForNextPacketToClient()
    {
        /*const auto action = [this]
        {*/
        const auto dispatcher = []
        (
            GameConnection& self, 
            std::string&& data, 
            const EndPoint& from
        )
        {
            if (from == self.server)
            {
                return self.handlePacketFromServer(PacketView{ data });
            }

            return self.handlePacketFromRemotePlayer(std::move(data), from);
        };
        auto handler = ReceiveHandler<>::create(this, dispatcher);
        this->publicSocketForClient.asyncReceiveFrom
        (
            handler->getBuffer(),
            handler->getFrom(),
            std::move(handler)
        );
        /*};

        this->whenPublicSocketForClientIsReadyToReceive.asyncDo(action);*/
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

    void GameConnection::handlePacketFromRemotePlayer(std::string&& packet, const EndPoint& from)
    {
        if (this->remotePlayer != from)
        {
            logLine(LogLevel::warning, "Updating remote player address from ", this->remotePlayer, " to ", from);
            this->remotePlayer = from;
        }

        if (PacketView{ packet }.isNatNeg())
        {
            logLine(LogLevel::info, "Forwarding NatNeg Packet from remote ", this->remotePlayer, " to ", this->clientRealAddress);
        }

        auto writeHandler = makeWriteHandler<GameConnection>(std::move(packet));
        this->fakeRemotePlayerSocket.asyncSendTo
        (
            writeHandler.getData(),
            this->clientRealAddress,
            std::move(writeHandler)
        );

        this->extendLife();
    }

    void GameConnection::handlePacketToRemotePlayer(std::string&& packet, const EndPoint& from)
    {
        if (from != this->clientRealAddress)
        {
            logLine(LogLevel::warning, "Updating client address from ", this->clientRealAddress, " to ", from);
            this->clientRealAddress = from;
        }

        if (PacketView{ packet }.isNatNeg())
        {
            logLine(LogLevel::info, "Forwarding NatNeg Packet from client ", this->remotePlayer, " to ", this->clientRealAddress);
        }

        auto writeHandler = makeWriteHandler<GameConnection>(std::move(packet));
        this->publicSocketForClient.asyncSendTo
        (
            writeHandler.getData(),
            this->remotePlayer,
            std::move(writeHandler)
        );

        this->extendLife();
    }
}