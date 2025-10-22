#include "p2p/Config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <filesystem>

namespace fs = std::filesystem;

namespace p2p {

    static std::string trim(const std::string& s){
        auto a = s.find_first_not_of(" \t\r\n");
        auto b = s.find_last_not_of(" \t\r\n");
        if (a==std::string::npos) return "";
        return s.substr(a, b-a+1);
    }

    CommonConfig CommonConfig::fromFile(const std::string& path) {
        std::ifstream in(path);
        if (!in) throw std::runtime_error("Failed to open Common.cfg");
        CommonConfig c;
        std::string key; std::string val;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty()) continue;
            std::istringstream iss(line);
            iss >> key >> val;
            if (key=="NumberOfPreferredNeighbors") c.numberOfPreferredNeighbors = std::stoi(val);
            else if (key=="UnchokingInterval") c.unchokingIntervalSec = std::stoi(val);
            else if (key=="OptimisticUnchokingInterval") c.optimisticUnchokingIntervalSec = std::stoi(val);
            else if (key=="FileName") c.fileName = val;
            else if (key=="FileSize") c.fileSizeBytes = std::stoll(val);
            else if (key=="PieceSize") c.pieceSizeBytes = std::stoi(val);
        }
        return c;
    }

    PeerInfoCfg PeerInfoCfg::fromFile(const std::string& path) {
        std::ifstream in(path);
        if (!in) throw std::runtime_error("Failed to open PeerInfo.cfg");
        PeerInfoCfg cfg;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty()) continue;
            std::istringstream iss(line);
            PeerInfoRow r; int has;
            iss >> r.peerId >> r.host >> r.port >> has;
            r.hasFile = (has==1);
            cfg.rows.push_back(std::move(r));
        }
        return cfg;
    }

    std::optional<PeerInfoRow> PeerInfoCfg::findById(int peerId) const {
        for (const auto& r: rows) if (r.peerId==peerId) return r;
        return std::nullopt;
    }

    std::vector<PeerInfoRow> PeerInfoCfg::earlierPeers(int peerId) const {
        std::vector<PeerInfoRow> out;
        size_t idx=0, selfIdx=rows.size();
        for (; idx<rows.size(); ++idx) if (rows[idx].peerId==peerId){ selfIdx=idx; break; }
        for (size_t i=0;i<rows.size() && i<selfIdx;++i) out.push_back(rows[i]);
        return out;
    }

    ConfigBundle ConfigBundle::load(int selfId, const std::string& commonPath, const std::string& peersPath, const std::string& workDir) {
        ConfigBundle b; b.selfId = selfId; b.common = CommonConfig::fromFile(commonPath); b.peers = PeerInfoCfg::fromFile(peersPath);
        auto me = b.peers.findById(selfId);
        if (!me) throw std::runtime_error("Self peerId not found in PeerInfo.cfg");
        b.self = *me;

        fs::path root = workDir;
        b.paths.workDir = root.string();
        b.paths.peerDir = (root / (std::string("peer_") + std::to_string(selfId))).string();
        b.paths.logFile = (root / (std::string("log_peer_") + std::to_string(selfId) + ".log")).string();

        fs::create_directories(b.paths.peerDir);
        return b;
    }

} // namespace p2p