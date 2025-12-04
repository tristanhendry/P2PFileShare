#include "p2p/Logger.hpp"

#include <iomanip>
#include <sstream>

namespace p2p {

    Logger::Logger(const std::string& path) : out_(path, std::ios::app) {}
    Logger::~Logger(){ out_.flush(); }

    std::string Logger::nowTs(){
        using namespace std::chrono;
        auto t = system_clock::to_time_t(system_clock::now());
        std::tm tm{};
        #if defined(_WIN32)
            localtime_s(&tm, &t);
        #else
            localtime_r(&t, &tm);
        #endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    void Logger::write(const std::string& level, const std::string& msg){
        std::lock_guard<std::mutex> lk(mtx_);
        out_ << "[" << nowTs() << "] [" << level << "] " << msg << "\n";
        out_.flush();
    }

    void Logger::info(const std::string& msg){ write("INFO", msg); }
    void Logger::error(const std::string& msg){ write("ERROR", msg); }

    void Logger::onConnectOut(int fromId, int toId){
        info("Peer " + std::to_string(fromId) + " makes a connection to Peer " + std::to_string(toId) + ".");
    }

    void Logger::onConnectIn(int toId, int fromId){
        info("Peer " + std::to_string(toId) + " is connected from Peer " + std::to_string(fromId) + ".");
    }

    void Logger::onReceivedInterested(int selfId, int fromId){
        info("Peer " + std::to_string(selfId) +
             " received the 'interested' message from " +
             std::to_string(fromId) + ".");
    }

    void Logger::onReceivedNotInterested(int selfId, int fromId){
        info("Peer " + std::to_string(selfId) +
             " received the 'not interested' message from " +
             std::to_string(fromId) + ".");
    }

    void Logger::onReceivedHave(int selfId, int fromId, uint32_t pieceIndex){
        info("Peer " + std::to_string(selfId) +
             " received the 'have' message from " +
             std::to_string(fromId) +
             " for the piece " + std::to_string(pieceIndex) + ".");
    }

    void Logger::onChoked(int selfId, int fromId){
        // [Time]: Peer [peer_ID 1] is choked by [peer_ID 2].
        info("Peer " + std::to_string(selfId) +
             " is choked by " + std::to_string(fromId) + ".");
    }

    void Logger::onUnchoked(int selfId, int fromId){
        // [Time]: Peer [peer_ID 1] is unchoked by [peer_ID 2].
        info("Peer " + std::to_string(selfId) +
             " is unchoked by " + std::to_string(fromId) + ".");
    }

    void Logger::onDownloadedPiece(int selfId, uint32_t pieceIndex, int fromId, int totalPieces){
        // [Time]: Peer [peer_ID 1] has downloaded the piece [piece index] from [peer_ID 2]. 
        // Now the number of pieces it has is [number of pieces].
        info("Peer " + std::to_string(selfId) +
             " has downloaded the piece " + std::to_string(pieceIndex) +
             " from " + std::to_string(fromId) +
             ". Now the number of pieces it has is " + std::to_string(totalPieces) + ".");
    }

    void Logger::onDownloadComplete(int selfId){
        // [Time]: Peer [peer_ID] has downloaded the complete file.
        info("Peer " + std::to_string(selfId) + " has downloaded the complete file.");
    }

    void Logger::onChangePreferredNeighbors(int selfId, const std::vector<int>& neighborIds){
        // [Time]: Peer [peer_ID] has the preferred neighbors [preferred neighbor ID list].
        std::string list;
        for (size_t i = 0; i < neighborIds.size(); ++i) {
            if (i > 0) list += ",";
            list += std::to_string(neighborIds[i]);
        }
        info("Peer " + std::to_string(selfId) +
             " has the preferred neighbors " + list + ".");
    }

    void Logger::onChangeOptimisticUnchoke(int selfId, int neighborId){
        // [Time]: Peer [peer_ID] has the optimistically unchoked neighbor [optimistically unchoked neighbor ID].
        info("Peer " + std::to_string(selfId) +
             " has the optimistically unchoked neighbor " +
             std::to_string(neighborId) + ".");
    }

} // namespace p2p