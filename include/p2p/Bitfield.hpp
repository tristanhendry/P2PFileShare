#ifndef P2P_BITFIELD_HPP
#define P2P_BITFIELD_HPP

#include <vector>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace p2p {

    class Bitfield {
    public:
        Bitfield() = default;
        explicit Bitfield(size_t pieces) { reset(pieces); }

        void reset(size_t pieces);
        bool has(size_t idx) const;
        void set(size_t idx);
        size_t pieceCount() const { return pieces_; }

        std::vector<uint8_t> toBytes() const { return data_; }
        static Bitfield fromBytes(const std::vector<uint8_t>& bytes, size_t pieces);

    private:
        size_t pieces_ = 0;
        std::vector<uint8_t> data_;
    };

} // namespace p2p

#endif // P2P_BITFIELD_HPP