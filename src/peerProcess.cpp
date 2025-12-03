#include <iostream>
#include <vector>
#include <memory>
#include <filesystem>

#include "p2p/Config.hpp"
#include "p2p/Logger.hpp"
#include "p2p/Bitfield.hpp"
#include "p2p/PeerState.hpp"
#include "p2p/Net.hpp"
//#include "p2p/Protocol.hpp"
#include "p2p/Scheduler.hpp"

using namespace p2p;

static size_t computePieceCount(long long fileSize, int pieceSize){
    if (pieceSize<=0) return 0;
    auto full = fileSize / pieceSize;
    return size_t(full + ((fileSize % pieceSize)!=0));
}

int main(int argc, char** argv){
    try {
        if (argc < 2){ std::cerr << "Usage: peerProcess <peerId>\n"; return 1; }
        int selfId = std::stoi(argv[1]);

        // Assume working dir is current directory
        std::string workDir = std::filesystem::current_path().string();
        std::string rootDir = std::filesystem::path(workDir).parent_path().string();
        auto cfg = ConfigBundle::load(selfId, rootDir+"/Common.cfg", rootDir+"/PeerInfo.cfg", rootDir);

        Logger logger(cfg.paths.logFile);
        std::cout << "Log file path: " << cfg.paths.logFile << std::endl;
        std::cout.flush();
        logger.info("peerProcess starting for peerId=" + std::to_string(selfId));
        std::cout << "Logged startup message" << std::endl;
        std::cout.flush();

        // Bitfield setup
        auto pieces = computePieceCount(cfg.common.fileSizeBytes, cfg.common.pieceSizeBytes);

        Bitfield myBits(pieces);
        if (cfg.self.hasFile) {
            for (size_t i = 0; i < pieces; ++i) {
                myBits.set(i);
            }
        }

        auto bitfieldBytes = myBits.toBytes();

        PeerServer server(selfId, logger, cfg.self.port, bitfieldBytes);

        server.start();

        // Connect to earlier peers
        std::vector<std::unique_ptr<ConnectionHandler>> conns;
        for (const auto& r : cfg.peers.earlierPeers(selfId)){
            Endpoint ep{r.host, r.port};
            auto h = PeerClient::connect(selfId, logger, ep, bitfieldBytes);
            if (h){ logger.onConnectOut(selfId, r.peerId); conns.push_back(std::move(h)); }
        }

        // Simple schedulers (midpoint: just log ticks)
        RepeatingTask preferredTick(cfg.common.unchokingIntervalSec, [&]{
            logger.info("[tick] preferred neighbors reselection (stub)");
        });
        RepeatingTask optimisticTick(cfg.common.optimisticUnchokingIntervalSec, [&]{
            logger.info("[tick] optimistic unchoke reselection (stub)");
        });
        preferredTick.start(); optimisticTick.start();

        // Keep main thread alive until Ctrl-C
        logger.info("peerProcess running. Press Ctrl-C to exit.");
        for(;;) std::this_thread::sleep_for(std::chrono::seconds(60));

        // Cleanup (unreachable in this simple loop)
        preferredTick.stop(); optimisticTick.stop();
        server.stop();
        for (auto& h: conns) if (h) h->join();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << "\n";
        return 2;
    }
}