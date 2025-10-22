#ifndef P2P_PROTOCOL_HPP
#define P2P_PROTOCOL_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace p2p {

    enum class MessageType : uint8_t {
        //CHOKE=0, UNCHOKE=1, INTERESTED=2, NOT_INTERESTED=3,
        HAVE=4, BITFIELD=5, REQUEST=6, PIECE=7
    };

    struct Handshake {
        static constexpr size_t LEN = 32;
        static constexpr size_t HDR_LEN = 18;
        //static constexpr size_t ZERO_LEN = 10;
        static const std::array<uint8_t, HDR_LEN> HEADER;

        static std::array<uint8_t, LEN> encode(int peerId);
        static int decodePeerId(const std::array<uint8_t, LEN>& msg);
    };

    struct Message {
        uint32_t length = 0; // excludes this 4-byte length field
        MessageType type{};
        std::vector<uint8_t> payload; // size = length - 1

        static Message make(MessageType t, std::vector<uint8_t> payload = {});
        static std::vector<uint8_t> serialize(const Message& m);

        [[maybe_unused]] static Message parse(const std::vector<uint8_t>& buf);
    };

    namespace msg {
//        inline Message choke() { return Message::make(MessageType::CHOKE); }
//        inline Message unchoke() { return Message::make(MessageType::UNCHOKE); }
//        inline Message interested() { return Message::make(MessageType::INTERESTED); }
//        inline Message notInterested() { return Message::make(MessageType::NOT_INTERESTED); }
//        Message have(uint32_t pieceIndex);
//        Message bitfield(const std::vector<uint8_t>& bits);
//        Message request(uint32_t pieceIndex);
//        Message piece(uint32_t pieceIndex, const std::vector<uint8_t>& data);
    }

} // namespace p2p

#endif // P2P_PROTOCOL_HPP