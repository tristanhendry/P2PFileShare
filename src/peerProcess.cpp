#include <iostream>
#include <vector>
#include <memory>
#include <filesystem>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <ctime>

#include "p2p/Config.hpp"
#include "p2p/Logger.hpp"
#include "p2p/Bitfield.hpp"
#include "p2p/PeerState.hpp"
#include "p2p/Net.hpp"
#include "p2p/Scheduler.hpp"
#include "p2p/PieceManager.hpp"

using namespace p2p;

// Global flag to track if we've logged completion
static std::atomic<bool> gHasLoggedCompletion{false};

// Global flag to signal all peers to terminate
static std::atomic<bool> gShouldTerminate{false};

static size_t computePieceCount(long long fileSize, int pieceSize){
    if (pieceSize<=0) return 0;
    auto full = fileSize / pieceSize;
    return size_t(full + ((fileSize % pieceSize)!=0));
}

int main(int argc, char** argv){
    try {
        if (argc < 2){ std::cerr << "Usage: peerProcess <peerId>\n"; return 1; }
        int selfId = std::stoi(argv[1]);

        // Seed random for optimistic unchoke
        srand(time(nullptr) + selfId);

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

        // PieceManager setup
        // Store the data file inside this peer's directory.
        std::string filePath = cfg.paths.peerDir + "/" + cfg.common.fileName;

        // Create the PieceManager on the heap and store it in the global pointer
        auto pieceMgr = std::make_shared<p2p::PieceManager>(
            filePath,
            cfg.common.fileSizeBytes,
            cfg.common.pieceSizeBytes,
            cfg.self.hasFile   
        );

        // Make it globally visible to all connections
        p2p::gPieceManager = pieceMgr;

        // Build initial BITFIELD bytes from PieceManager
        auto bitfieldBytes = pieceMgr->toBitfieldBytes();

        PeerServer server(selfId, logger, cfg.self.port, bitfieldBytes);
        server.start();

        // Connect to earlier peers
        std::vector<std::unique_ptr<ConnectionHandler>> conns;
        for (const auto& r : cfg.peers.earlierPeers(selfId)){
            Endpoint ep{r.host, r.port};
            auto h = PeerClient::connect(selfId, logger, ep, bitfieldBytes);
            if (h){ 
                logger.onConnectOut(selfId, r.peerId); 
                conns.push_back(std::move(h));
            }
        }

        
        RepeatingTask preferredTick(cfg.common.unchokingIntervalSec, [&]{
           
            std::vector<int> preferred;
            logger.onChangePreferredNeighbors(selfId, preferred);
        });
        
        RepeatingTask optimisticTick(cfg.common.optimisticUnchokingIntervalSec, [&]{
            // pick a random peer (if any exist)
            if (!conns.empty() && !conns.empty()) {
                int randomIdx = rand() % conns.size();
                auto peers = cfg.peers.rows;
                if (!peers.empty()) {
                    int luckyPeer = peers[rand() % peers.size()].peerId;
                    logger.onChangeOptimisticUnchoke(selfId, luckyPeer);
                }
            }
        });
        
        preferredTick.start(); 
        optimisticTick.start();

        // Track if this peer started with the file (seeder)
        bool wasInitialSeeder = cfg.self.hasFile;

        // Background thread to check for completion
        std::thread completionChecker([&](){
            bool hasLoggedCompletion = false;
            
            while (!gShouldTerminate.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                
                // Check if this peer has completed download
                if (!hasLoggedCompletion && pieceMgr->isComplete()) {
                    // Only log completion if we WEREN'T a seeder
                    if (!wasInitialSeeder) {
                        logger.onDownloadComplete(selfId);
                        hasLoggedCompletion = true;
                        gHasLoggedCompletion.store(true);
                        
                        // After completing download, wait 10 seconds then terminate
                        std::this_thread::sleep_for(std::chrono::seconds(10));
                        logger.info("Peer " + std::to_string(selfId) + 
                                  " terminating - download complete and grace period elapsed.");
                        gShouldTerminate.store(true);
                        break;
                    }
                }
            }
        });

        // Keep main thread alive until termination signal or Ctrl-C
        logger.info("peerProcess running. Waiting for file transfer completion...");
        
        while (!gShouldTerminate.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Cleanup
        logger.info("Shutting down peer " + std::to_string(selfId));
        
        preferredTick.stop(); 
        optimisticTick.stop();
        
        if (completionChecker.joinable()) {
            completionChecker.join();
        }
        
        server.stop();
        
        for (auto& h: conns) {
            if (h) h->join();
        }
        
        logger.info("Peer " + std::to_string(selfId) + " shutdown complete.");
        
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << "\n";
        return 2;
    }
}