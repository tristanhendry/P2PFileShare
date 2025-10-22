#include "p2p/Bitfield.hpp"

namespace p2p {

    void Bitfield::reset(size_t pieces){
        pieces_ = pieces;
        size_t bytes = (pieces + 7) / 8;
        data_.assign(bytes, 0);
    }

    bool Bitfield::has(size_t idx) const{
        if (idx >= pieces_) throw std::out_of_range("bitfield index");
        size_t byte = idx / 8; size_t bit = 7 - (idx % 8);
        return (data_[byte] >> bit) & 1U;
    }

    void Bitfield::set(size_t idx){
        if (idx >= pieces_) throw std::out_of_range("bitfield index");
        size_t byte = idx / 8; size_t bit = 7 - (idx % 8);
        data_[byte] |= (1u << bit);
    }

    Bitfield Bitfield::fromBytes(const std::vector<uint8_t>& bytes, size_t pieces){
        Bitfield bf; bf.pieces_ = pieces; bf.data_ = bytes; return bf;
    }

} // namespace p2p