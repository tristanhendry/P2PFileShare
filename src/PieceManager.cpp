#include "p2p/PieceManager.hpp"

#include <fstream>
#include <stdexcept>
#include <algorithm>

namespace p2p {

    std::shared_ptr<PieceManager> gPieceManager;

PieceManager::PieceManager(const std::string& filePath,
                           long long fileSizeBytes,
                           int pieceSizeBytes,
                           bool hasCompleteFile)
    : filePath_(filePath),
      fileSizeBytes_(fileSizeBytes),
      pieceSizeBytes_(pieceSizeBytes),
      pieceCount_(0) {

    if (fileSizeBytes_ < 0 || pieceSizeBytes_ <= 0) {
        throw std::invalid_argument("Invalid file or piece size");
    }

    computePieceCount_();
    have_.assign(pieceCount_, false);

    if (hasCompleteFile) {
        // Seeder: assume the file on disk is correct and complete.
        for (size_t i = 0; i < pieceCount_; ++i) {
            have_[i] = true;
        }
    }
}

void PieceManager::computePieceCount_() {
    // classic ceil(fileSize / pieceSize)
    pieceCount_ = static_cast<size_t>(
        (fileSizeBytes_ + pieceSizeBytes_ - 1) / pieceSizeBytes_
    );
}

std::pair<long long,long long> PieceManager::pieceOffsetAndSize_(size_t index) const {
    if (index >= pieceCount_) {
        throw std::out_of_range("Piece index out of range");
    }
    long long offset = static_cast<long long>(index) * pieceSizeBytes_;
    // Last piece may be smaller.
    long long maxBytes = fileSizeBytes_ - offset;
    long long size = std::min<long long>(pieceSizeBytes_, maxBytes);
    return {offset, size};
}

bool PieceManager::havePiece(size_t index) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return index < have_.size() && have_[index];
}

bool PieceManager::isComplete() const {
    std::lock_guard<std::mutex> lk(mtx_);
    for (bool h : have_) {
        if (!h) {
            return false;
        }
    }
    return true;
}

void PieceManager::markHave(size_t index) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (index >= have_.size()) {
        throw std::out_of_range("markHave index");
    }
    have_[index] = true;
}

std::vector<uint8_t> PieceManager::readPiece(size_t index) const {
    auto [offset, size] = pieceOffsetAndSize_(index);
    std::vector<uint8_t> buf(size);

    std::ifstream in(filePath_, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open file for reading: " + filePath_);
    }
    in.seekg(offset);
    in.read(reinterpret_cast<char*>(buf.data()), size);
    if (!in) {
        throw std::runtime_error("Failed to read piece from file");
    }
    return buf;
}

bool PieceManager::writePiece(size_t index, const std::vector<uint8_t>& data) {
    auto [offset, expectedSize] = pieceOffsetAndSize_(index);
    if (static_cast<long long>(data.size()) != expectedSize) {
        // You could allow this and only write expectedSize, but mismatched
        // sizes likely indicate a bug in REQUEST/PIECE logic.
        throw std::runtime_error("Piece data size mismatch");
    }

    {
        // Write to file
        std::fstream out(filePath_, std::ios::in | std::ios::out | std::ios::binary);
        if (!out) {
            // If file doesn't exist yet, create it with the right size.
            out.open(filePath_, std::ios::out | std::ios::binary | std::ios::trunc);
            out.close();
            out.open(filePath_, std::ios::in | std::ios::out | std::ios::binary);
        }
        if (!out) {
            throw std::runtime_error("Failed to open file for writing: " + filePath_);
        }

        out.seekp(offset);
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
        if (!out) {
            throw std::runtime_error("Failed to write piece to file");
        }
    }

    bool wasNew = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!have_[index]) {
            have_[index] = true;
            wasNew = true;
        }
    }
    return wasNew;
}

std::vector<uint8_t> PieceManager::toBitfieldBytes() const {
    std::lock_guard<std::mutex> lk(mtx_);
    size_t bytes = (pieceCount_ + 7) / 8;
    std::vector<uint8_t> bf(bytes, 0);

    for (size_t i = 0; i < pieceCount_; ++i) {
        if (!have_[i]) continue;
        size_t byte = i / 8;
        size_t bit  = 7 - (i % 8);
        bf[byte] |= static_cast<uint8_t>(1u << bit);
    }
    return bf;
}

} // namespace p2p
