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
            logLine(LogLevel::error, "Removing connection ", id);
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
            logLine(LogLevel::info, "Packet of step ", static_cast<int>(step), " does not have NatNegPlayerID, discarded.");
            return;
        }
        const auto natNegPlayerID = natNegPlayerIDHolder.value();

        switch (step)
        {
        case NatNegStep::init:
        {
            logLine(LogLevel::info, "Processing init packet (step ", static_cast<int>(step) , ") ..." );

            constexpr auto sequenceNumberOffset = 12;
            if (packet.natNegPacket.at(sequenceNumberOffset) == 0)
            {
                this->initialPhases[natNegPlayerID] = InitialPhase::create
                (
                    this->objectMaker,
                    this->weak_from_this(),
                    this->addressTranslator,
                    natNegPlayerID,
                    from,
                    this->serverHostName,
                    this->serverPort
                );
            }
        }
        default:
        {
            const auto found = this->initialPhases.find(natNegPlayerID);
            if (found == this->initialPhases.end())
            {
                logLine(LogLevel::error, "Cannot find InitialPhase: ", natNegPlayerID);
                return;
            }
            const auto initialPhase = found->second.lock();
            if (!initialPhase)
            {
                logLine(LogLevel::error, "InitialPhase already expired: ", natNegPlayerID);
                this->removeConnection(natNegPlayerID);
                return;
            }
            logLine(LogLevel::info, "Processing packet (step ", static_cast<int>(step), ") ...");
            initialPhase->handlePacketToServer(packet, from);
        }
        break;
        }
    }
}