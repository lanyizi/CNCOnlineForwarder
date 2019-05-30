#pragma once
#include <chrono>
#include <memory>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

namespace CNCOnlineForwarder
{
    class IOManager : public std::enable_shared_from_this<IOManager>
    {
    private:
        struct PrivateConstructor{};
    public:
        using ContextType = boost::asio::io_context;
        using StrandType = decltype(boost::asio::make_strand(std::declval<ContextType&>()));
        class ObjectMaker;

        static constexpr auto description = "IOManager";

        static std::shared_ptr<IOManager> create()
        {
            return std::make_shared<IOManager>(PrivateConstructor{});
        }

        IOManager(PrivateConstructor) {}

        auto stop() { return this->context.stop(); }

        auto run() 
        { 
            try
            {
                return this->context.run();
            }
            catch (...)
            {
                this->stop();
                throw;
            }
        }

    private:
        ContextType context;
    };

    class IOManager::ObjectMaker
    {
    public:
        ObjectMaker(std::weak_ptr<IOManager> ioManager) : 
            ioManager{ std::move(ioManager) } {}

        template<typename T, typename... Arguments>
        T make(Arguments&&... arguments) const
        {
            const auto ioManager = std::shared_ptr{ this->ioManager };
            return T{ ioManager->context, std::forward<Arguments>(arguments)... };
        }

        StrandType makeStrand() const
        {
            const auto ioManager = std::shared_ptr{ this->ioManager };
            return boost::asio::make_strand(ioManager->context);
        }

    private:
        std::weak_ptr<IOManager> ioManager;
    };

    namespace Details
    {
        template<typename T>
        class WithStrandBase
        {
        public:
            template<typename... Args>
            WithStrandBase(IOManager::StrandType& strand, Args&&... args) :
                strand{ strand },
                object{ strand.get_inner_executor(), std::forward<Args>(args)... }
            {}

            T* operator->() noexcept { return &this->object; }

            const T* operator->() const noexcept { return &this->object; }

        protected:
            IOManager::StrandType& strand;
            T object;
        };
    }

    template<typename T>
    class WithStrand : public Details::WithStrandBase<T>
    {
    public:
        using Details::WithStrandBase<T>::WithStrandBase;
    };

    template<>
    class WithStrand<boost::asio::ip::udp::socket> : 
        public Details::WithStrandBase<boost::asio::ip::udp::socket>
    {
    public:
        using Details::WithStrandBase<boost::asio::ip::udp::socket>::WithStrandBase;

        template<typename MutableBufferSequence, typename EndPoint, typename ReadHandler>
        auto asyncReceiveFrom
        (
            const MutableBufferSequence& buffers,
            EndPoint& from,
            ReadHandler&& handler
        )
        {
            return this->object.async_receive_from
            (
                buffers,
                from,
                boost::asio::bind_executor(this->strand, std::forward<ReadHandler>(handler))
            );
        }

        template<typename ConstBufferSequence, typename EndPoint, typename WriteHandler>
        auto asyncSendTo
        (
            const ConstBufferSequence& buffers,
            const EndPoint& to,
            WriteHandler&& handler
        )
        {
            return this->object.async_send_to
            (
                buffers,
                to,
                boost::asio::bind_executor(this->strand, std::forward<WriteHandler>(handler))
            );
        }
    };

    template<>
    class WithStrand<boost::asio::steady_timer> : 
        public Details::WithStrandBase<boost::asio::steady_timer>
    {
    public:
        using Details::WithStrandBase<boost::asio::steady_timer>::WithStrandBase;

        template<typename WaitHandler>
        auto asyncWait(const std::chrono::minutes timeout, WaitHandler&& waitHandler)
        {
            this->object.expires_from_now(timeout);
            this->object.async_wait(std::forward<WaitHandler>(waitHandler));
        }
    };

    template<typename Protocol>
    class WithStrand<boost::asio::ip::basic_resolver<Protocol>> :
        public Details::WithStrandBase<boost::asio::ip::basic_resolver<Protocol>>
    {
    public:
        using Details::WithStrandBase<boost::asio::ip::basic_resolver<Protocol>>::WithStrandBase;

        template<typename ResolveHandler>
        auto asyncResolve
        (
            const std::string_view host, 
            const std::string_view service, 
            ResolveHandler&& resolveHandler
        )
        {
            this->object.async_resolve
            (
                host,
                service,
                std::forward<ResolveHandler>(resolveHandler)
            );
        }
    };
}