#include "p2p/Protocol.hpp"

#include <cstring>

namespace p2p {

    const std::array<uint8_t, Handshake::HDR_LEN> Handshake::HEADER = {
            'P','2','P','F','I','L','E','S','H','A','R','I','N','G','P','R','O','J'
    };

    std::array<uint8_t, Handshake::LEN> Handshake::encode(int peerId){
        std::array<uint8_t, LEN> out{};
        std::memcpy(out.data(), HEADER.data(), HEADER.size());
        // bytes 18..27 are zeros by default (value-initialized)
        // peerId at 28..31 big-endian
        out[28] = static_cast<uint8_t>((peerId >> 24) & 0xFF);
        out[29] = static_cast<uint8_t>((peerId >> 16) & 0xFF);
        out[30] = static_cast<uint8_t>((peerId >> 8) & 0xFF);
        out[31] = static_cast<uint8_t>((peerId) & 0xFF);
        return out;
    }

    int Handshake::decodePeerId(const std::array<uint8_t, LEN>& msg){
        if (!std::equal(HEADER.begin(), HEADER.end(), msg.begin())) {
            throw std::runtime_error("Bad handshake header");
        }
        int id = (msg[28] << 24) | (msg[29] << 16) | (msg[30] << 8) | msg[31];
        return id;
    }

    Message Message::make(MessageType t, std::vector<uint8_t> payload){
        Message m; m.type = t; m.payload = std::move(payload); m.length = static_cast<uint32_t>(1 + m.payload.size()); return m;
    }

    static void put32(std::vector<uint8_t>& b, uint32_t v){ b.push_back((v>>24)&0xFF); b.push_back((v>>16)&0xFF); b.push_back((v>>8)&0xFF); b.push_back(v&0xFF);}
    static uint32_t get32(const uint8_t* p){ return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|uint32_t(p[3]); }

    std::vector<uint8_t> Message::serialize(const Message& m){
        std::vector<uint8_t> b; b.reserve(4 + m.length);
        put32(b, m.length);
        b.push_back(static_cast<uint8_t>(m.type));
        b.insert(b.end(), m.payload.begin(), m.payload.end());
        return b;
    }

    [[maybe_unused]] Message Message::parse(const std::vector<uint8_t>& buf){
        if (buf.size() < 5) throw std::runtime_error("Short message");
        uint32_t len = get32(buf.data());
        if (len + 4 != buf.size()) throw std::runtime_error("Length mismatch");
        Message m; m.length = len; m.type = static_cast<MessageType>(buf[4]);
        m.payload.assign(buf.begin()+5, buf.end());
        return m;
    }

    namespace msg {

        // ---- Control messages: no payload ----

        Message choke() {
            return Message::make(MessageType::CHOKE);
        }

        Message unchoke() {
            return Message::make(MessageType::UNCHOKE);
        }

        Message interested() {
            return Message::make(MessageType::INTERESTED);
        }

        Message notInterested() {
            return Message::make(MessageType::NOT_INTERESTED);
        }

        // ---- Data-related messages ----

        Message have(uint32_t pieceIndex){
            std::vector<uint8_t> p;
            p.reserve(4);
            put32(p, pieceIndex);
            return Message::make(MessageType::HAVE, std::move(p));
        }

        Message bitfield(const std::vector<uint8_t>& bits){
            return Message::make(MessageType::BITFIELD, bits);
        }

        Message request(uint32_t pieceIndex){
            std::vector<uint8_t> p;
            p.reserve(4);
            put32(p, pieceIndex);
            return Message::make(MessageType::REQUEST, std::move(p));
        }

        Message piece(uint32_t pieceIndex, const std::vector<uint8_t>& data){
            std::vector<uint8_t> p;
            p.reserve(4 + data.size());
            put32(p, pieceIndex);
            p.insert(p.end(), data.begin(), data.end());
            return Message::make(MessageType::PIECE, std::move(p));
        }

    } // namespace msg



} // namespace p2p