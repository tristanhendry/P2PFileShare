#include "p2p/Net.hpp"

#include <vector>
#include <cstring>
#include <stdexcept>

namespace p2p {

    //static bool setNonBlocking(socket_t){ return true; } 

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
        // 1) Send handshake
        auto hs = Handshake::encode(selfId_);
        if (!sendAll_(hs.data(), hs.size())) {
            return;
        }

        // 2) Receive handshake
        std::array<uint8_t, Handshake::LEN> buf{};
        if (!recvAll_(buf.data(), buf.size())) {
            return;
        }

        try {
            remotePeerId_ = Handshake::decodePeerId(buf);
        } catch (...) {
            return; 
        }

        // 3) Log incoming connection once we know who connected
        if (incoming_) {
            logger_.onConnectIn(selfId_, remotePeerId_);
        }

        // 4) After handshake, send our bitfield (if we have one)
        if (!selfBitfield_.empty()) {
            auto m = msg::bitfield(selfBitfield_);
            send(m);
        }

        // Helper: recompute whether WE are interested in this neighbor,
        // and send INTERESTED / NOT_INTERESTED if our state changes.
        auto recomputeInterestAndSend = [this]() {
            if (remoteBitfield_.empty()) {
                // We don't know anything about the remote's pieces yet.
                return;
            }

            bool interested = false;

            if (selfBitfield_.empty()) {
                // We have nothing: if remote has any 1-bits, we are interested.
                for (uint8_t b : remoteBitfield_) {
                    if (b != 0) {
                        interested = true;
                        break;
                    }
                }
            } else {
                // Compare byte-by-byte: remote & ~self
                size_t n = std::min(selfBitfield_.size(), remoteBitfield_.size());
                for (size_t i = 0; i < n; ++i) {
                    uint8_t newBits =
                        remoteBitfield_[i] &
                        static_cast<uint8_t>(~selfBitfield_[i]);
                    if (newBits != 0) {
                        interested = true;
                        break;
                    }
                }
            }

            if (interested != amInterested_) {
                amInterested_ = interested;
                if (interested) {
                    auto m = msg::interested();
                    send(m);
                } else {
                    auto m = msg::notInterested();
                    send(m);
                }
            }
        };

        // Helper: pick the next piece to request from this neighbor.
        auto pickNextRequestPiece = [this]() -> int {
            if (!p2p::gPieceManager) return -1;

            auto& pm = *p2p::gPieceManager;
            size_t total = pm.pieceCount();

            for (size_t i = 0; i < total; ++i) {
                // Skip pieces we already have.
                if (pm.havePiece(i)) continue;

                // Check if remote has this piece according to remoteBitfield_.
                size_t byte = i / 8;
                size_t bit  = 7 - (i % 8);
                if (byte >= remoteBitfield_.size()) continue;

                bool remoteHas =
                    (remoteBitfield_[byte] & (uint8_t(1) << bit)) != 0;
                if (!remoteHas) continue;

                return static_cast<int>(i);
            }
            return -1; // nothing useful to request
        };

        // 5) Main receive loop for length-prefixed messages
        while (running_.load()) {
            // Read 4-byte length; if socket closes, we break
            uint8_t lenBuf[4];
            if (!recvAll_(lenBuf, 4)) {
                break;
            }

            uint32_t len =
                (uint32_t(lenBuf[0]) << 24) |
                (uint32_t(lenBuf[1]) << 16) |
                (uint32_t(lenBuf[2]) << 8)  |
                 uint32_t(lenBuf[3]);

            // Keep-alive: length 0 => no type, no payload
            if (len == 0) {
                continue;
            }

            // Read message body (type + payload)
            std::vector<uint8_t> body(len);
            if (!recvAll_(body.data(), len)) {
                break;
            }

            // First byte is the message type
            MessageType type = static_cast<MessageType>(body[0]);

            switch (type) {
                case MessageType::BITFIELD: {
                    // Payload is the remote peer's bitfield bytes
                    if (body.size() <= 1) {
                        // malformed bitfield, ignore
                        break;
                    }

                    std::vector<uint8_t> remoteBits(body.begin() + 1, body.end());
                    logger_.info("Received bitfield from peer " +
                                 std::to_string(remotePeerId_) + ".");

                    // Store remote bitfield for this connection
                    remoteBitfield_ = std::move(remoteBits);


                    // Initial interest decision: always send one message 
                    bool interested = false;

                    if (selfBitfield_.empty()) {
                        for (uint8_t b : remoteBitfield_) {
                            if (b != 0) {
                                interested = true;
                                break;
                            }
                        }
                    } else {
                        // Compare byte-by-byte: remote & ~self
                        size_t n = std::min(selfBitfield_.size(), remoteBitfield_.size());
                        for (size_t i = 0; i < n; ++i) {
                            uint8_t newBits =
                                remoteBitfield_[i] &
                                static_cast<uint8_t>(~selfBitfield_[i]);
                            if (newBits != 0) {
                                interested = true;
                                break;
                            }
                        }
                    }

                    // Send INTERESTED or NOT_INTERESTED once for the initial bitfield
                    amInterested_ = interested;
                    if (interested) {
                        auto m = msg::interested();
                        send(m);

                        if (p2p::gPieceManager) {
                            int next = pickNextRequestPiece();
                            if (next >= 0) {
                                auto req = msg::request(static_cast<uint32_t>(next));
                                send(req);
                            }
                        }
                    } else {
                        auto m = msg::notInterested();
                        send(m);
                    }

                    break;
                }

                case MessageType::HAVE: {
                    // Payload: 4-byte piece index 
                    if (body.size() < 1 + 4) {
                        break; 
                    }

                    uint32_t idx =
                        (uint32_t(body[1]) << 24) |
                        (uint32_t(body[2]) << 16) |
                        (uint32_t(body[3]) << 8)  |
                         uint32_t(body[4]);

                    // Log according to spec
                    logger_.onReceivedHave(selfId_, remotePeerId_, idx);

                    // Update remoteBitfield_ to reflect this piece
                    size_t byte = idx / 8;
                    size_t bit  = 7 - (idx % 8);

                    if (remoteBitfield_.size() <= byte) {
                        remoteBitfield_.resize(byte + 1, 0);
                    }

                    remoteBitfield_[byte] |= (uint8_t(1) << bit);

                    // Re-evaluate interest based on the new piece
                    recomputeInterestAndSend();
                    break;
                }

                case MessageType::INTERESTED: {
                    logger_.onReceivedInterested(selfId_, remotePeerId_);
                    break;
                }

                case MessageType::NOT_INTERESTED: {
                    logger_.onReceivedNotInterested(selfId_, remotePeerId_);
                    break;
                }

                // Handle a REQUEST from the remote peer: send them the piece.
                case MessageType::REQUEST: {
                    if (!p2p::gPieceManager) {
                        break;
                    }

                    // Payload: 4-byte piece index 
                    if (body.size() < 1 + 4) {
                        break; 
                    }

                    uint32_t idx =
                        (uint32_t(body[1]) << 24) |
                        (uint32_t(body[2]) << 16) |
                        (uint32_t(body[3]) << 8)  |
                         uint32_t(body[4]);

                    auto& pm = *p2p::gPieceManager;

                    // Only serve if we actually have this piece.
                    if (idx >= pm.pieceCount() || !pm.havePiece(idx)) {
                        break;
                    }

                    try {
                        auto data = pm.readPiece(idx);
                        auto m = msg::piece(idx, data);
                        send(m);
                    } catch (...) {
                    }

                    break;
                }

                // Handle a PIECE sent by the remote: write it and maybe request another.
                case MessageType::PIECE: {
                    if (!p2p::gPieceManager) {
                        break;
                    }

                    // Need at least 4 bytes of piece index
                    if (body.size() < 1 + 4) {
                        break;
                    }

                    uint32_t idx =
                        (uint32_t(body[1]) << 24) |
                        (uint32_t(body[2]) << 16) |
                        (uint32_t(body[3]) << 8)  |
                         uint32_t(body[4]);

                    // Remaining bytes are the piece data
                    std::vector<uint8_t> payload;
                    payload.reserve(body.size() - 1 - 4);
                    payload.insert(payload.end(), body.begin() + 1 + 4, body.end());

                    auto& pm = *p2p::gPieceManager;

                    if (idx >= pm.pieceCount()) {
                        break;
                    }

                    try {
                        bool wasNew = pm.writePiece(idx, payload);
                        if (wasNew) {
                            // Count how many pieces we now have
                            int piecesOwned = 0;
                            for (size_t i = 0; i < pm.pieceCount(); ++i) {
                                if (pm.havePiece(i)) {
                                    piecesOwned++;
                                }
                            }

                            // Log the download event
                            logger_.onDownloadedPiece(selfId_, idx, remotePeerId_, piecesOwned);

                            // Update our local bitfield cache for this connection.
                            size_t byte = idx / 8;
                            size_t bit  = 7 - (idx % 8);

                            if (selfBitfield_.size() <= byte) {
                                selfBitfield_.resize(byte + 1, 0);
                            }
                            selfBitfield_[byte] |= (uint8_t(1) << bit);

                            // Inform neighbor(s) that we now have this piece.
                            auto haveMsg = msg::have(idx);
                            send(haveMsg);

                            // Person B can track download stats here using payload.size().
                        }

                        // Try to request another piece from this neighbor.
                        int next = pickNextRequestPiece();
                        if (next >= 0) {
                            auto req = msg::request(static_cast<uint32_t>(next));
                            send(req);
                        }

                    } catch (...) {
        
                    }

                    break;
                }

                default:
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
                auto* h = new ConnectionHandler(selfId_, logger_, s, true, selfBitfield_);
                h->start();

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
        (void)selfId; (void)logger; (void)ep; return nullptr; 
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
                                                 false,
                                                 selfBitfield);
        h->start();
        return h;
    #endif
    }

} // namespace p2p