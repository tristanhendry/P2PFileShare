#ifndef P2P_NET_HPP
#define P2P_NET_HPP

#include <thread>
#include <vector>
#include <atomic>
#include <functional>
#include <string>
#include <memory>
#include <optional>
#include <mutex>

#include "Protocol.hpp"
#include "Logger.hpp"

// POSIX sockets (Linux/macOS). Windows: stubs only.
#if defined(_WIN32)
#include <winsock2.h>
using socket_t = SOCKET;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
using socket_t = int;
#endif

namespace p2p {

    struct Endpoint { std::string host; int port = 0; };

    class ConnectionHandler {
    public:
        ConnectionHandler(int selfId, Logger& logger, socket_t sock);
        ~ConnectionHandler();

        void start();
        void join();

        //int remotePeerId() const { return remotePeerId_; }

        // Send message (thread-safe)
        void send(const Message& m);

        // disable copy
        ConnectionHandler(const ConnectionHandler&) = delete;
        ConnectionHandler& operator=(const ConnectionHandler&) = delete;

    private:
        int selfId_;
        [[maybe_unused]] Logger& logger_;
        socket_t sock_;
        std::thread thr_;
        std::mutex sendMtx_;
        std::atomic<bool> running_{false};
        int remotePeerId_ = -1;

        void run_();
        bool sendAll_(const uint8_t* data, size_t n) const;
        bool recvAll_(uint8_t* data, size_t n) const;
    };

    class PeerServer {
    public:
        PeerServer(int selfId, Logger& logger, int listenPort);
        ~PeerServer();

        void start();
        void stop();

    private:
        int selfId_;
        Logger& logger_;
        int port_;
        std::thread thr_;
        std::atomic<bool> running_{false};
        #if defined(_WIN32)
            socket_t srv_ = INVALID_SOCKET;
        #else
            socket_t srv_ = -1;
        #endif
    };

    class PeerClient {
    public:
        static std::unique_ptr<ConnectionHandler> connect(int selfId, Logger& logger, const Endpoint& ep);
    };

} // namespace p2p

#endif // P2P_NET_HPP