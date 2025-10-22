#ifndef P2P_PEER_STATE_HPP
#define P2P_PEER_STATE_HPP

#include <unordered_map>
#include <optional>
#include <vector>
#include <atomic>

#include "Bitfield.hpp"

namespace p2p {

    struct RemoteNeighborState {
        bool amChoked = true; // I am choked by them
        bool amInterested = false; // I am interested in them
        bool peerChoked = true; // they are choked by me
        bool peerInterested = false;// they are interested in me
        double recentDownloadRate = 0.0; // bytes/sec over last interval
        Bitfield lastBitfield;
    };

    class PeerState {
    public:
        explicit PeerState(Bitfield selfBits) : selfBitfield_(std::move(selfBits)) {}

        Bitfield& selfBitfield() { return selfBitfield_; }

        RemoteNeighborState& neighbor(int peerId) { return neighbors_[peerId]; }

        std::vector<int> interestedNeighbors() const;

    private:
        Bitfield selfBitfield_;
        std::unordered_map<int, RemoteNeighborState> neighbors_;
    };

} // namespace p2p

#endif // P2P_PEER_STATE_HPP