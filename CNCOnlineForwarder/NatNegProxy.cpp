#include "precompiled.h"
#include "NatNegProxy.h"
#include "InitialPhase.h"
#include "Logging.h"
#include "SimpleWriteHandler.hpp"
#include "WeakRefHandler.hpp"

using UDP = boost::asio::ip::udp;
using ErrorCode = boost::system::error_code;
using LogLevel = CNCOnlineForwarder::Logging::Level;
using WriteHandler = CNCOnlineForwarder::Utility::SimpleWriteHandler<CNCOnlineForwarder::NatNeg::NatNegProxy>;
using CNCOnlineForwarder::Utility::makeWeakHandler;

namespace CNCOnlineForwarder::NatNeg
{
    template<typename... Arguments>
    void logLine(LogLevel level, Arguments&&... arguments)
    {
        return Logging::logLine<NatNegProxy>(level, std::forward<Arguments>(arguments)...);
    }

    class NatNegProxy::ReceiveHandler
    {
    public:

        static auto create(NatNegProxy* pointer)
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

        void operator()(NatNegProxy& self, const ErrorCode& code, const std::size_t bytesReceived) const
        {
            self.prepareForNextPacketToServer();

            if (code.failed())
            {
                logLine(LogLevel::error, "Async receive failed: ", code);
                return;
            }

            const auto view = PacketView{ {this->buffer->data(), bytesReceived} };
            self.handlePacketToServer(view, *this->from);
        }

    private:
        ReceiveHandler() :
            buffer{ std::make_unique<std::array<char, 1024>>() },
            from{ std::make_unique<EndPoint>() }
        {}

        std::unique_ptr<std::array<char, 1024>> buffer;
        std::unique_ptr<EndPoint> from;
    };

    std::shared_ptr<NatNegProxy> NatNegProxy::create
    (
        const IOManager::ObjectMaker& objectMaker,
        const std::string_view serverHostName,
        const std::uint16_t serverPort,
        const std::weak_ptr<ProxyAddressTranslator>& addressTranslator
    )
    {
        const auto self = std::make_shared<NatNegProxy>
        (
            PrivateConstructor{},
            objectMaker, 
            serverHostName,
            serverPort,
            addressTranslator
        );

        const auto action = [](NatNegProxy& self)
        {
            logLine(LogLevel::info, "NatNegProxy created.");
            self.prepareForNextPacketToServer();
        };
        boost::asio::defer(self->proxyStrand, makeWeakHandler(self, action));

        return self;
    }

    NatNegProxy::NatNegProxy
    (
        PrivateConstructor,
        const IOManager::ObjectMaker& objectMaker,
        const std::string_view serverHostName,
        const std::uint16_t serverPort,
        const std::weak_ptr<ProxyAddressTranslator>& addressTranslator
    ) :
        objectMaker{ objectMaker },
        proxyStrand{ objectMaker.makeStrand() },
        serverSocket{ proxyStrand, EndPoint{ UDP::v4(), serverPort } },
        serverHostName{ serverHostName },
        serverPort{ serverPort },
        addressTranslator{ addressTranslator }
    {}

    void NatNegProxy::sendFromProxySocket(const PacketView packetView, const EndPoint& to)
    {
        auto action = [data = packetView.copyBuffer(), to](NatNegProxy& self)
        {
            logLine(LogLevel::info, "Sending data to ", to);
            auto writeHandler = WriteHandler{ data };
            self.serverSocket.asyncSendTo
            (
                writeHandler.getData(), 
                to, 
                std::move(writeHandler)
            );
        };

        boost::asio::defer
        (
            this->proxyStrand,
            makeWeakHandler(this, std::move(action))
        );
    }

    void NatNegProxy::removeConnection(const NatNegPlayerID id)
    {
        auto action = [id](NatNegProxy& self)
        {
            logLine(LogLevel::error, "Removing InitaialPhase ", id);
            self.initialPhases.erase(id);
        };

        boost::asio::defer
        (
            this->proxyStrand, 
            makeWeakHandler(this, std::move(action))
        );
    }

    void NatNegProxy::prepareForNextPacketToServer()
    {
        auto handler = ReceiveHandler::create(this);
        this->serverSocket.asyncReceiveFrom
        (
            handler->getBuffer(),
            handler->getFrom(),
            std::move(handler)
        );
    }

    void NatNegProxy::handlePacketToServer(const PacketView packet, const EndPoint& from)
    {
        if (!packet.isNatNeg())
        {
            logLine(LogLevel::warning, "Packet is not natneg, discarded.");
            return;
        }

        const auto step = packet.getStep();
        const auto natNegPlayerIDHolder = packet.getNatNegPlayerID();
        if (!natNegPlayerIDHolder.has_value())
        {
            logLine(LogLevel::info, "Packet of step ", step, " does not have NatNegPlayerID, discarded.");
            return;
        }
        const auto natNegPlayerID = natNegPlayerIDHolder.value();

        auto& initialPhaseRef = this->initialPhases[natNegPlayerID];
        if (initialPhaseRef.expired())
        {
            logLine(LogLevel::info, "New NatNegPlayerID, creating InitialPhase: ", natNegPlayerID);
            initialPhaseRef = InitialPhase::create
            (
                this->objectMaker,
                this->weak_from_this(),
                natNegPlayerID,
                this->serverHostName,
                this->serverPort
            );
        }

        const auto initialPhase = initialPhaseRef.lock();
        if (!initialPhase)
        {
            logLine(LogLevel::error, "InitialPhase already expired: ", natNegPlayerID);
            this->removeConnection(natNegPlayerID);
            return;
        }

        logLine(LogLevel::info, "Processing packet (step ", step, ") from ", from);
        if (step == NatNegStep::init)
        {
            constexpr auto sequenceNumberOffset = 12;
            const auto sequenceNumber = 
                static_cast<int>(packet.natNegPacket.at(sequenceNumberOffset));

            logLine(LogLevel::info, "Init packet, seq num = ", sequenceNumber);

            if (sequenceNumber == 0)
            {
                // Packet is from client public address
                logLine(LogLevel::info, "Preparing GameConnection, client = ", from);
                initialPhase->prepareGameConnection
                (
                    this->objectMaker, 
                    this->addressTranslator, 
                    from
                );
            }
        }

        initialPhase->handlePacketToServer(packet, from);
    }
}