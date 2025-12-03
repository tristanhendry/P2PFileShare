#include "p2p/Net.hpp"

#include <vector>
#include <cstring>
#include <stdexcept>

namespace p2p {

    //static bool setNonBlocking(socket_t){ return true; } // midpoint: ignore

    static void closesock(socket_t s){
        #if defined(_WIN32)
            closesocket(s);
        #else
            ::close(s);
        #endif
    }

    ConnectionHandler::ConnectionHandler(int selfId, Logger& logger, socket_t sock,
                                     bool incoming, std::vector<uint8_t> selfBitfield)
    : selfId_(selfId),
      logger_(logger),
      sock_(sock),
      incoming_(incoming),
      selfBitfield_(std::move(selfBitfield)) {}



    ConnectionHandler::~ConnectionHandler(){
        running_.store(false);
        if (thr_.joinable()) thr_.join();
    #if defined(_WIN32)
        if (sock_ != INVALID_SOCKET) closesock(sock_);
    #else
        if (sock_ >= 0) closesock(sock_);
    #endif
    }


    void ConnectionHandler::start(){ running_.store(true); thr_ = std::thread(&ConnectionHandler::run_, this); }
    void ConnectionHandler::join(){ if (thr_.joinable()) thr_.join(); }

    bool ConnectionHandler::sendAll_(const uint8_t* data, size_t n) const{
        size_t sent=0;
        while (sent<n){
        #if defined(_WIN32)
            int r = ::send(sock_, reinterpret_cast<const char*>(data+sent), int(n-sent), 0);
        #else
            ssize_t r = ::send(sock_, data+sent, n-sent, 0);
        #endif
            if (r<=0) return false;
            sent += size_t(r);
        }
        return true;
    }

    bool ConnectionHandler::recvAll_(uint8_t* data, size_t n) const{
        size_t got=0;
        while (got<n){
        #if defined(_WIN32)
            int r = ::recv(sock_, reinterpret_cast<char*>(data+got), int(n-got), MSG_WAITALL);
        #else
            ssize_t r = ::recv(sock_, data+got, n-got, MSG_WAITALL);
        #endif
            if (r<=0) return false; got += size_t(r);
        }
        return true;
    }

    void ConnectionHandler::run_(){
        // Send handshake
        auto hs = Handshake::encode(selfId_);
        if (!sendAll_(hs.data(), hs.size())) return;

        // Read handshake
        std::array<uint8_t, Handshake::LEN> buf{};
        if (!recvAll_(buf.data(), buf.size())) return;
        try { remotePeerId_ = Handshake::decodePeerId(buf); }
        catch(...) { return; }

        // Log incoming connection once we know who connected
        if (incoming_) {
            // "Peer X is connected from Peer Y."
            logger_.onConnectIn(selfId_, remotePeerId_);
        }

        // Peers that don't have any pieces yet may skip the bitfield message.
        bool hasAnyPiece = false;
        for (auto b : selfBitfield_) {
            if (b != 0) { hasAnyPiece = true; break; }
        }
        if (hasAnyPiece) {
            auto bfMsg = msg::bitfield(selfBitfield_);
            send(bfMsg);
        }

        // After handshake, read loop for actual messages
        while (running_.load()) {
            // Read 4-byte length; graceful exit if socket closed
            uint8_t lenBuf[4];
            if (!recvAll_(lenBuf, 4)) break;
            uint32_t len = (uint32_t(lenBuf[0])<<24) |
                           (uint32_t(lenBuf[1])<<16) |
                           (uint32_t(lenBuf[2])<<8)  |
                           uint32_t(lenBuf[3]);

            if (len == 0) {
                // Possible keep-alive or malformed; ignore and continue.
                continue;
            }

            std::vector<uint8_t> body(len);
            if (!recvAll_(body.data(), len)) break;

            // First byte is message type, remaining bytes (if any) are payload
            MessageType type = static_cast<MessageType>(body[0]);

            switch (type) {
                case MessageType::BITFIELD: {
                    // Payload is the remote peer's bitfield bytes
                    if (body.size() <= 1) {
                        // malformed bitfield, ignore
                        break;
                    }

                    std::vector<uint8_t> remoteBits(body.begin() + 1, body.end());

                    // Optional debug log (you already saw these earlier)
                    logger_.info("Received bitfield from peer " +
                                 std::to_string(remotePeerId_) + ".");

                    // Decide if we are interested:
                    // interested if the remote has at least one piece that we don't.
                    bool interested = false;

                    if (selfBitfield_.empty()) {
                        // We have nothing: if remote has any 1-bits, we are interested.
                        for (uint8_t b : remoteBits) {
                            if (b != 0) {
                                interested = true;
                                break;
                            }
                        }
                    } else {
                        // Compare byte-by-byte: remote & ~self
                        size_t n = std::min(selfBitfield_.size(), remoteBits.size());
                        for (size_t i = 0; i < n; ++i) {
                            uint8_t newBits =
                                remoteBits[i] &
                                static_cast<uint8_t>(~selfBitfield_[i]);
                            if (newBits != 0) {
                                interested = true;
                                break;
                            }
                        }
                    }

                    // Send INTERESTED or NOT_INTERESTED accordingly
                    if (interested) {
                        auto m = msg::interested();
                        send(m);
                    } else {
                        auto m = msg::notInterested();
                        send(m);
                    }

                    break;
                }

                case MessageType::INTERESTED: {
                    // [Time]: Peer [peer_ID 1] received the 'interested' message from [peer_ID 2].
                    logger_.onReceivedInterested(selfId_, remotePeerId_);
                    // Later: mark neighbor as "interested" in shared state.
                    break;
                }

                case MessageType::NOT_INTERESTED: {
                    // [Time]: Peer [peer_ID 1] received the 'not interested' message from [peer_ID 2].
                    logger_.onReceivedNotInterested(selfId_, remotePeerId_);
                    // Later: mark neighbor as "not interested" in shared state.
                    break;
                }

                default:
                    // For now, ignore other message types; we will flesh these out later.
                    break;
            }
        }
    }

    void ConnectionHandler::send(const Message& m){
        std::lock_guard<std::mutex> lk(sendMtx_);
        auto bytes = Message::serialize(m);
        sendAll_(bytes.data(), bytes.size());
    }

    PeerServer::PeerServer(int selfId, Logger& logger, int listenPort,
                       std::vector<uint8_t> selfBitfield)
    : selfId_(selfId),
      logger_(logger),
      port_(listenPort),
      selfBitfield_(std::move(selfBitfield)) {}


    PeerServer::~PeerServer(){ stop(); }

    void PeerServer::start(){
        running_.store(true);
        thr_ = std::thread([this]{
        #if defined(_WIN32)
            // Midpoint: not implemented
            (void)port_; return;
        #else
            srv_ = ::socket(AF_INET, SOCK_STREAM, 0);
            if (srv_ < 0) return;
            int opt=1; setsockopt(srv_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port_);
            if (::bind(srv_, (sockaddr*)&addr, sizeof(addr))<0) { closesock(srv_); srv_=-1; return; }
            if (::listen(srv_, 16)<0) { closesock(srv_); srv_=-1; return; }
            while (running_.load()){
                sockaddr_in cli{}; socklen_t cl = sizeof(cli);
                socket_t s = ::accept(srv_, (sockaddr*)&cli, &cl);
                if (s<0) continue;
                // Spawn handler for an incoming connection
                auto* h = new ConnectionHandler(selfId_, logger_, s, /*incoming=*/true, selfBitfield_);
                h->start();
                // We intentionally leak handlers for midpoint simplicity; OS reclaims on exit.

            }
            if (srv_>=0) { closesock(srv_); srv_=-1; }
            #endif
        });
    }

    void PeerServer::stop(){ running_.store(false); if (thr_.joinable()) thr_.join(); }

    std::unique_ptr<ConnectionHandler>
    PeerClient::connect(int selfId, Logger& logger, const Endpoint& ep,
                        const std::vector<uint8_t>& selfBitfield) {
    #if defined(_WIN32)
        (void)selfId; (void)logger; (void)ep; return nullptr; // midpoint
    #else
        addrinfo hints{}; hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM;
        addrinfo* res=nullptr;
        std::string port = std::to_string(ep.port);
        if (getaddrinfo(ep.host.c_str(), port.c_str(), &hints, &res)!=0) return nullptr;

        socket_t s=-1; addrinfo* rp;
        for (rp=res; rp; rp=rp->ai_next){
            s = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (s<0) continue;
            if (::connect(s, rp->ai_addr, rp->ai_addrlen)==0) break;
            closesock(s); s=-1;
        }
        freeaddrinfo(res);
        if (s<0) return nullptr;

        auto h = std::make_unique<ConnectionHandler>(selfId, logger, s,
                                                 /*incoming=*/false,
                                                 selfBitfield);
        h->start();
        return h;
    #endif
    }

} // namespace p2p