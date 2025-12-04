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
#include <chrono>

#include "Protocol.hpp"
#include "Logger.hpp"
#include "p2p/PieceManager.hpp"

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
        ConnectionHandler(int selfId, Logger& logger, socket_t sock, bool incoming,
                      std::vector<uint8_t> selfBitfield);
        ~ConnectionHandler();

        void start();
        void join();

        int remotePeerId() const { return remotePeerId_; }
        
        // Send message 
        void send(const Message& m);
        
        // State queries
        bool isTheyInterested() const { return theyAreInterested_.load(); }
        bool isAmChokingThem() const { return amChokingThem_.load(); }
        
        // Get and reset download statistics
        size_t getBytesDownloadedAndReset() {
            return bytesDownloaded_.exchange(0);
        }
        
        // Control choking state
        void chokeRemote();
        void unchokeRemote();

        // disable copy
        ConnectionHandler(const ConnectionHandler&) = delete;
        ConnectionHandler& operator=(const ConnectionHandler&) = delete;

    private:
        int selfId_;
        Logger& logger_;
        socket_t sock_;
        std::thread thr_;
        std::mutex sendMtx_;
        std::atomic<bool> running_{false};
        std::vector<uint8_t> selfBitfield_;

        // Track what the remote peer has
        std::vector<uint8_t> remoteBitfield_;

        // Whether WE are currently interested in this remote peer.
        bool amInterested_ = false;
        
        // Track remote peer's interest in us
        std::atomic<bool> theyAreInterested_{false};
        
        // Track if we are currently choking them
        std::atomic<bool> amChokingThem_{true};  
        
        // Track download statistics
        std::atomic<size_t> bytesDownloaded_{0};

        int remotePeerId_ = -1;
        bool incoming_ = false;

        void run_();
        bool sendAll_(const uint8_t* data, size_t n) const;
        bool recvAll_(uint8_t* data, size_t n) const;
    };


    class PeerServer {
    public:
        PeerServer(int selfId, Logger& logger, int listenPort, std::vector<uint8_t> selfBitfield);
        ~PeerServer();

        void start();
        void stop();

    private:
        int selfId_;
        Logger& logger_;
        int port_;
        std::thread thr_;
        std::atomic<bool> running_{false};
        std::vector<uint8_t> selfBitfield_;
        #if defined(_WIN32)
            socket_t srv_ = INVALID_SOCKET;
        #else
            socket_t srv_ = -1;
        #endif
    };

    class PeerClient {
    public:
        static std::shared_ptr<ConnectionHandler> connect(
            int selfId,
            Logger& logger,
            const Endpoint& ep,
            const std::vector<uint8_t>& selfBitfield);
    };

    // Global connection registry
    extern std::vector<std::shared_ptr<ConnectionHandler>> gAllConnections;
    extern std::mutex gConnectionsMutex;
    
    // Broadcast message to all connected peers
    void broadcastToAllPeers(const Message& m);

} // namespace p2p

#endif 