#ifndef P2P_CONFIG_HPP
#define P2P_CONFIG_HPP

#include <string>
#include <vector>
#include <optional>

namespace p2p {

    struct CommonConfig {
        int numberOfPreferredNeighbors = 2;
        int unchokingIntervalSec = 5;
        int optimisticUnchokingIntervalSec = 15;
        std::string fileName;
        long long fileSizeBytes = 0;
        int pieceSizeBytes = 32768;

        static CommonConfig fromFile(const std::string& path);
    };

    struct PeerInfoRow {
        int peerId = 0;
        std::string host;
        int port = 0;
        bool hasFile = false;
    };

    struct PeerInfoCfg {
        std::vector<PeerInfoRow> rows;
        static PeerInfoCfg fromFile(const std::string& path);
        std::optional<PeerInfoRow> findById(int peerId) const;
        std::vector<PeerInfoRow> earlierPeers(int peerId) const; // rows with lower index in file
    };

    struct EnvPaths {
        std::string workDir; // project root
        std::string peerDir; // workDir + "/peer_" + id
        std::string logFile; // workDir + "/log_peer_" + id + ".log"
    };

    struct ConfigBundle {
        int selfId = 0;
        CommonConfig common;
        PeerInfoCfg peers;
        PeerInfoRow self;
        EnvPaths paths;

        static ConfigBundle load(int selfId, const std::string& commonPath, const std::string& peersPath, const std::string& workDir);
    };

} // namespace p2p

#endif // P2P_CONFIG_HPP