#include <iostream>
#include <vector>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <cstdlib>

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
        int optimisticNeighborId = -1;

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

        // Simple preferred neighbors: unchoke all interested neighbors.
        RepeatingTask preferredTick(cfg.common.unchokingIntervalSec, [&]{
            // How many preferred neighbors we want (from Common.cfg)
            const int k = cfg.common.numberOfPreferredNeighbors;

            // Collect all interested neighbors with their download rates.
            struct NeighborStat {
                ConnectionHandler* handler;
                long bytes;
            };
            std::vector<NeighborStat> stats;

            for (auto &h : conns) {
                if (!h) continue;

                // Only consider neighbors who are interested in us.
                if (h->areTheyInterested()) {
                    stats.push_back(NeighborStat{
                            h.get(),
                            h->bytesDownloadedThisInterval()
                    });
                }
            }

            // Sort by bytes downloaded this interval (descending).
            std::sort(stats.begin(), stats.end(),
                      [](const NeighborStat& a, const NeighborStat& b){
                          return a.bytes > b.bytes;
                      });

            std::vector<int> preferredIds;

            // First, choke everyone by default (we'll unchoke the chosen ones).
            for (auto &h : conns) {
                if (!h) continue;
                h->setChokeState(true);
            }

            // Unchoke up to k best interested neighbors.
            for (size_t i = 0; i < stats.size() && (int)i < k; ++i) {
                auto* h = stats[i].handler;
                h->setChokeState(false);  // unchoke preferred
                preferredIds.push_back(h->remotePeerId());
            }

            // Also keep the current optimistic neighbor unchoked (if any),
            // even if they are not in the top-k.
            if (optimisticNeighborId != -1) {
                for (auto &h : conns) {
                    if (!h) continue;
                    if (h->remotePeerId() == optimisticNeighborId) {
                        h->setChokeState(false);
                        // If not already in preferredIds, we do NOT add them here,
                        // because they are "optimistically" unchoked, not preferred.
                        break;
                    }
                }
            }

            // Reset stats for next interval.
            for (auto &h : conns) {
                if (!h) continue;
                h->resetBytesDownloadedThisInterval();
            }

            // Log preferred neighbors (if any).
            if (!preferredIds.empty()) {
                std::string msg = "Peer " + std::to_string(selfId) +
                                  " has the preferred neighbors ";
                for (size_t i = 0; i < preferredIds.size(); ++i) {
                    if (i > 0) msg += ", ";
                    msg += std::to_string(preferredIds[i]);
                }
                msg += ".";
                logger.info(msg);
            }
            // else: don't log anything if we currently have no interested neighbors.
        });
        RepeatingTask optimisticTick(cfg.common.optimisticUnchokingIntervalSec, [&]{
            std::vector<ConnectionHandler*> candidates;

            for (auto &h : conns) {
                if (!h) continue;

                // Candidate: currently choked by us
                if (h->amChokingThem()) {
                    candidates.push_back(h.get());
                }
            }

            if (candidates.empty()) {
                return; // nothing to do
            }

            // Pick random candidate
            ConnectionHandler* chosen =
                    candidates[std::rand() % candidates.size()];

            optimisticNeighborId = chosen->remotePeerId();

            // Ensure they're unchoked
            chosen->setChokeState(false);

            std::string msg = "Peer " + std::to_string(selfId) +
                              " has the optimistically unchoked neighbor " +
                              std::to_string(optimisticNeighborId) + ".";
            logger.info(msg);
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