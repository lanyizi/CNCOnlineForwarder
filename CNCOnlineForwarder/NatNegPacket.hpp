#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <boost/container_hash/hash.hpp>

namespace CNCOnlineForwarder::NatNeg
{
    enum class NatNegStep : char
    {
        init = 0,
        initAck = 1,
        connect = 5,
        connectAck = 6,
        connectPing = 7,
        report = 13,
        reportAck = 14,
        preInit = 15,
        preInitAck = 16,
    };

    using NatNegID = std::uint32_t;

    struct NatNegPlayerID
    {
        struct Hash;

        NatNegID natNegID;
        std::int8_t playerID;
        
    };

    inline bool operator==(const NatNegPlayerID a, const NatNegPlayerID& b)
    {
        return a.natNegID == b.natNegID && a.playerID == b.playerID;
    }
    
    struct NatNegPlayerID::Hash
    {
        std::size_t operator()(const NatNegPlayerID& id) const
        {
            auto hash = std::size_t{ 0 };
            boost::hash_combine(hash, id.natNegID);
            boost::hash_combine(hash, id.playerID);
            return hash;
        }
    };

    inline std::ostream& operator<<(std::ostream& out, const NatNegPlayerID& id)
    {
        return out << '[' << id.natNegID << ':' << static_cast<int>(id.playerID) << ']';
    }

    struct NatNegPacketView
    {
        NatNegPacketView(const std::string_view data) :
            natNegPacket{ data }
        {
        }

        std::string copyBuffer() const
        {
            return std::string{ this->natNegPacket };
        }

        bool isNatNeg() const noexcept
        {
            using namespace std::string_view_literals;
            static constexpr auto natNegMagic = "\xFD\xFC\x1E\x66\x6A\xB2"sv;
            static constexpr auto versionSize = 1;
            static constexpr auto stepSize = 1;

            if (natNegPacket.size() < natNegMagic.size() + versionSize + stepSize)
            {
                return false;
            }

            if (natNegPacket.rfind(natNegMagic, 0) == natNegPacket.npos)
            {
                return false;
            }

            return true;
        }

        NatNegStep getStep() const
        {
            constexpr auto stepPosition = 7;

            if (this->isNatNeg() == false)
            {
                throw new std::invalid_argument{ "Invalid NatNeg packet: incorrect magic." };
            }

            return static_cast<NatNegStep>(this->natNegPacket.at(stepPosition));
        }

        // Returns: NatNegID of the packet,
        // if this packet actually contains a NatNegID
        std::optional<NatNegID> getNatNegID() const
        {
            switch (this->getStep())
            {
            case NatNegStep::init:
            case NatNegStep::initAck:
            case NatNegStep::connect:
            case NatNegStep::connectAck:
            case NatNegStep::connectPing:
            case NatNegStep::report:
            case NatNegStep::reportAck:
            {
                constexpr auto natNegIDPosition = 8;
                if (this->natNegPacket.size() < (natNegIDPosition + sizeof(NatNegID)))
                {
                    throw std::invalid_argument{ "Invalid NatNeg packet: packet too small to contain NatNegID." };
                }
                auto id = NatNegID{};
                std::copy_n(this->natNegPacket.begin(), sizeof(id), reinterpret_cast<char*>(&id));
                return id;
            }
            break;
            }

            return std::nullopt;
        }

        // Returns: NatNegID and PlayerID of the packet,
        // if this packet actually contains both NatNegID and PlayerID
        std::optional<NatNegPlayerID> getNatNegPlayerID() const
        {
            const auto natNegID = this->getNatNegID();
            if (natNegID == std::nullopt)
            {
                return std::nullopt;
            }

            auto playerIDPosition = this->natNegPacket.npos;
            switch (this->getStep())
            {
            case NatNegStep::init:
            case NatNegStep::initAck:
            case NatNegStep::connectAck:
            case NatNegStep::report:
            case NatNegStep::reportAck:
                playerIDPosition = 13;
                break;
            case NatNegStep::preInit:
            case NatNegStep::preInitAck:
                playerIDPosition = 12;
                break;
            default:
                return std::nullopt;
            }

            if (this->natNegPacket.size() < (playerIDPosition + sizeof(char)))
            {
                throw std::invalid_argument{ "Invalid NatNeg packet: packet too small to contain playerID." };
            }

            return NatNegPlayerID{ natNegID.value(), this->natNegPacket.at(playerIDPosition) };
        }

        // Returns: Position of IP relative to the beginning of the packet,
        // if this packet actually contains an IP address
        static constexpr std::optional<std::size_t> getAddressOffset(const NatNegStep step)
        {
            switch (step)
            {
            case NatNegStep::connect:
            case NatNegStep::connectPing:
            {
                return 12;
            }
            break;
            }

            return std::nullopt;
        }

        std::string_view natNegPacket;
    };

    inline std::pair<std::array<std::uint8_t, 4>, std::uint16_t> parseAddress
    (
        const std::string_view source,
        const std::size_t position
    )
    {
        auto ip = std::array<std::uint8_t, 4>{};
        auto port = std::uint16_t{};
        if (source.size() < (position + ip.size() + sizeof(port)))
        {
            throw std::out_of_range{ "NatNeg Packet too short!" };
        }

        const auto ipHolder = source.substr(position, ip.size());
        const auto portHolder = source.substr(position + ip.size(), sizeof(port));
        std::copy_n(ipHolder.begin(), ip.size(), ip.begin());
        std::copy_n(portHolder.begin(), sizeof(port), reinterpret_cast<char*>(&port));
        return std::pair{ ip, port };
    }

    inline void rewriteAddress
    (
        std::string& destination,
        const std::size_t position,
        const std::array<std::uint8_t, 4>& ip,
        const std::uint16_t port
    )
    {
        if (destination.size() < (position + ip.size() + sizeof(port)))
        {
            throw std::out_of_range{ "NatNeg Packet too short!" };
        }

        const auto then = std::copy_n(ip.begin(), ip.size(), destination.begin() + position);
        std::copy_n(reinterpret_cast<const char*>(&port), sizeof(port), then);
    }
}