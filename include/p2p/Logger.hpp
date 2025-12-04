#ifndef P2P_LOGGER_HPP
#define P2P_LOGGER_HPP

#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>

namespace p2p {

    class Logger {
    public:
        explicit Logger(const std::string& path);
        ~Logger();

        void info(const std::string& msg);
        void error(const std::string& msg);

        // Connection events
        void onConnectOut(int fromId, int toId);
        void onConnectIn(int toId, int fromId);

        // Interest messages
        void onReceivedInterested(int selfId, int fromId);
        void onReceivedNotInterested(int selfId, int fromId);
        void onReceivedHave(int selfId, int fromId, uint32_t pieceIndex);

        // Choking events
        void onChoked(int selfId, int fromId);
        void onUnchoked(int selfId, int fromId);

        // Download events
        void onDownloadedPiece(int selfId, uint32_t pieceIndex, int fromId, int totalPieces);
        void onDownloadComplete(int selfId);

        // Neighbor selection events
        void onChangePreferredNeighbors(int selfId, const std::vector<int>& neighborIds);
        void onChangeOptimisticUnchoke(int selfId, int neighborId);

    private:
        std::ofstream out_;
        std::mutex mtx_;
        static std::string nowTs();
        void write(const std::string& level, const std::string& msg);
    };

} // namespace p2p

#endif // P2P_LOGGER_HPP