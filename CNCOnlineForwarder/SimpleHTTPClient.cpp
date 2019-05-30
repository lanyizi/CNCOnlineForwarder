#include "precompiled.h"
#include "SimpleHTTPClient.h"
#include "Logging.h"

namespace CNCOnlineForwarder::Utility
{
    // From https://www.boost.org/doc/libs/1_70_0/libs/beast/example/http/client/async/http_client_async.cpp
    // Performs an HTTP GET and prints the response
    class SimpleHTTPClient : public std::enable_shared_from_this<SimpleHTTPClient>
    {
        struct PrivateConstructor {};
    public:
        static constexpr auto description = "SimpleHTTPClient";

        static void startGet
        (
            const IOManager::ObjectMaker& objectMaker,
            const std::string_view hostName,
            const std::string_view target,
            std::function<void(std::string)> onGet
        )
        {
            const auto session = std::make_shared<SimpleHTTPClient>
            (
                PrivateConstructor{},
                objectMaker, 
                std::move(onGet)
            );
            session->run(hostName, "80", target, 11);
        }

        // Objects are constructed with a strand to
        // ensure that handlers do not execute concurrently.
        SimpleHTTPClient
        (
            PrivateConstructor,
            const IOManager::ObjectMaker& objectMaker,
            std::function<void(std::string)>&& onGet
        ) :
            strand{ objectMaker.makeStrand() },
            resolver{ strand },
            stream{ strand },
            buffer{},
            request{},
            response{},
            onGet{ std::move(onGet) }
        {}

    private:
        using Strand = IOManager::StrandType;
        using Resolver = WithStrand<boost::asio::ip::tcp::resolver>;
        using TCPStream = boost::beast::tcp_stream;
        using FlatBuffer = boost::beast::flat_buffer;
        using HTTPRequest = boost::beast::http::request<boost::beast::http::empty_body>;
        using HTTPResponse = boost::beast::http::response<boost::beast::http::string_body>;

        using ErrorCode = boost::beast::error_code;
        using TCPEndPoint = boost::asio::ip::tcp::resolver::endpoint_type;
        using ResolvedHostName = boost::asio::ip::tcp::resolver::results_type;

        using LogLevel = Logging::Level;

        template<typename... Arguments>
        static void log(const LogLevel level, Arguments&&... arguments)
        {
            return Logging::logLine<SimpleHTTPClient>(level, std::forward<Arguments>(arguments)...);
        }

        // Start the asynchronous operation
        void run
        (
            const std::string_view host,
            const std::string_view port,
            const std::string_view target,
            int version
        )
        {
            namespace Http = boost::beast::http;
            // Set up an HTTP GET request message
            this->request.version(version);
            this->request.method(Http::verb::get);
            this->request.target({ target.data(), target.size() });
            this->request.set(Http::field::host, host);
            this->request.set(Http::field::user_agent, BOOST_BEAST_VERSION_STRING);

            log(LogLevel::info, "Starting HTTP Get on ", host, "/", target);

            // Look up the domain name
            this->resolver.asyncResolve
            (
                host,
                port,
                boost::beast::bind_front_handler
                (
                    &SimpleHTTPClient::onResolve,
                    this->shared_from_this()
                )
            );
        }

        void onResolve(const ErrorCode& code, const ResolvedHostName& results)
        {
            if (code.failed())
            {
                log(LogLevel::error, "Cannot resolve hostname: ", code);
                return;
            }

            // Set a timeout on the operation
            this->stream.expires_after(std::chrono::seconds(30));

            log(LogLevel::info, "Hostname resolved. Connecting...");

            // Make the connection on the IP address we get from a lookup
            this->stream.async_connect
            (
                results,
                boost::beast::bind_front_handler
                (
                    &SimpleHTTPClient::onConnect,
                    this->shared_from_this()
                )
            );
        }

        void onConnect(const ErrorCode& code, const TCPEndPoint& endPoint)
        {
            if (code.failed())
            {
                log(LogLevel::error, "Connect failed: ", code);
                return;
            }

            // Set a timeout on the operation
            this->stream.expires_after(std::chrono::seconds(30));

            log(LogLevel::info, "Connected to ", endPoint, "; Writing header.");

            // Send the HTTP request to the remote host
            boost::beast::http::async_write
            (
                this->stream,
                this->request,
                boost::beast::bind_front_handler
                (
                    &SimpleHTTPClient::onWrite,
                    this->shared_from_this()
                )
            );
        }

        void onWrite(const ErrorCode& code, const std::size_t /* bytesTransferred */)
        {
            if (code.failed())
            {
                log(LogLevel::error, "Async write failed: ", code);
                return;
            }

            log(LogLevel::info, "Start receiving response.");

            // Receive the HTTP response
            boost::beast::http::async_read
            (
                this->stream,
                this->buffer,
                this->response,
                boost::beast::bind_front_handler
                (
                    &SimpleHTTPClient::onRead,
                    this->shared_from_this()
                )
            );
        }

        void onRead(const ErrorCode& code, const std::size_t /* bytesTransferred */)
        {
            if (code.failed())
            {
                log(LogLevel::error, "Async read failed: ", code);
                return;
            }

            // Write the message to standard out
            auto stringStream = std::stringstream{};
            stringStream << this->response.body();
            
            log(LogLevel::info, "Response read.");
            this->onGet(stringStream.str());

            // Gracefully close the socket
            try
            {
                this->stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both);
            }
            catch (const std::exception& error)
            {
                log(LogLevel::warning, "Socket shutdown failed: ", error.what());
            }
        }

        Strand strand;
        Resolver resolver;
        TCPStream stream;
        FlatBuffer buffer; // (Must persist between reads)
        HTTPRequest request;
        HTTPResponse response;
        std::function<void(std::string)> onGet;
    };

    void asyncHttpGet
    (
        const IOManager::ObjectMaker& objectMaker,
        const std::string_view hostName,
        const std::string_view target,
        std::function<void(std::string)> onGet
    )
    {
        SimpleHTTPClient::startGet(objectMaker, hostName, target, std::move(onGet));
    }
}
