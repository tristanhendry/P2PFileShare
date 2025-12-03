#ifndef P2P_PIECEMANAGER_HPP
#define P2P_PIECEMANAGER_HPP

#include <vector>
#include <string>
#include <cstdint>
#include <mutex>
#include <memory>

namespace p2p {

    class PieceManager {
    public:
        // fileSizeBytes: total file size from Common.cfg
        // pieceSizeBytes: piece size from Common.cfg
        // hasCompleteFile: if this peer starts with the entire file (hasFile==1)
        PieceManager(const std::string& filePath,
                     long long fileSizeBytes,
                     int pieceSizeBytes,
                     bool hasCompleteFile);

        // Number of pieces for this file.
        size_t pieceCount() const { return pieceCount_; }

        // True if we have this piece fully.
        bool havePiece(size_t index) const;

        // True if we have all pieces.
        bool isComplete() const;

        // Mark a piece as "have" (used when seeder starts with full file).
        void markHave(size_t index);

        // Read a piece (for serving REQUESTs). Throws std::runtime_error on failure.
        std::vector<uint8_t> readPiece(size_t index) const;

        // Write a piece from network. Returns true if this piece was newly completed.
        bool writePiece(size_t index, const std::vector<uint8_t>& data);

        // Convert our have[] into a compact byte bitfield (bit 7..0 = pieces 0..7 etc).
        std::vector<uint8_t> toBitfieldBytes() const;

    private:
        std::string filePath_;
        long long fileSizeBytes_;
        int pieceSizeBytes_;
        size_t pieceCount_;

        // One entry per piece: true if we have it.
        std::vector<bool> have_;

        mutable std::mutex mtx_; // protect have_ during writes

        void computePieceCount_();
        std::pair<long long,long long> pieceOffsetAndSize_(size_t index) const;
    };

    // Global pointer to this process's PieceManager instance.
    // Set in peerProcess.cpp, used in Net.cpp.
    extern std::shared_ptr<PieceManager> gPieceManager;

} // namespace p2p

#endif // P2P_PIECEMANAGER_HPP
