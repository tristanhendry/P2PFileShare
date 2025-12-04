#include <iostream>
#include <vector>
#include <memory>
#include <filesystem>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <algorithm>

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
        logger.info("peerProcess starting for peerId=" + std::to_string(selfId));

        // PieceManager setup
        std::string filePath = cfg.paths.peerDir + "/" + cfg.common.fileName;

        auto pieceMgr = std::make_shared<p2p::PieceManager>(
            filePath,
            cfg.common.fileSizeBytes,
            cfg.common.pieceSizeBytes,
            cfg.self.hasFile   
        );

        p2p::gPieceManager = pieceMgr;

        // Build initial BITFIELD bytes from PieceManager
        auto bitfieldBytes = pieceMgr->toBitfieldBytes();

        PeerServer server(selfId, logger, cfg.self.port, bitfieldBytes);
        server.start();

        // Connect to earlier peers - store in global connection list
        for (const auto& r : cfg.peers.earlierPeers(selfId)){
            Endpoint ep{r.host, r.port};
            auto h = PeerClient::connect(selfId, logger, ep, bitfieldBytes);
            if (h){ 
                logger.onConnectOut(selfId, r.peerId); 
            }
        }

        // Track if this peer started with the file (seeder)
        bool wasInitialSeeder = cfg.self.hasFile;
        bool hasCompleteFile = wasInitialSeeder;

        // Implement proper preferred neighbors selection
        RepeatingTask preferredTick(cfg.common.unchokingIntervalSec, [&]{
            std::lock_guard<std::mutex> lk(gConnectionsMutex);
            
            if (gAllConnections.empty()) {
                return;
            }
            
            // Get list of peers that are interested in us
            std::vector<std::shared_ptr<ConnectionHandler>> interestedPeers;
            for (auto& conn : gAllConnections) {
                if (conn && conn->isTheyInterested()) {
                    interestedPeers.push_back(conn);
                }
            }
            
            if (interestedPeers.empty()) {
                // No one is interested, choke everyone
                for (auto& conn : gAllConnections) {
                    if (conn) {
                        conn->chokeRemote();
                    }
                }
                logger.onChangePreferredNeighbors(selfId, {});
                return;
            }
            
            std::vector<std::shared_ptr<ConnectionHandler>> preferred;
            
            if (hasCompleteFile) {
                // If we have complete file, select randomly among interested
                std::vector<std::shared_ptr<ConnectionHandler>> candidates = interestedPeers;
                std::random_shuffle(candidates.begin(), candidates.end());
                
                int toSelect = std::min(cfg.common.numberOfPreferredNeighbors, 
                                       static_cast<int>(candidates.size()));
                for (int i = 0; i < toSelect; ++i) {
                    preferred.push_back(candidates[i]);
                }
            } else {
                // Select based on download rate
                struct PeerRate {
                    std::shared_ptr<ConnectionHandler> conn;
                    size_t bytesDownloaded;
                };
                
                std::vector<PeerRate> rates;
                for (auto& conn : interestedPeers) {
                    size_t bytes = conn->getBytesDownloadedAndReset();
                    rates.push_back({conn, bytes});
                }
                
                // Sort by download rate 
                std::sort(rates.begin(), rates.end(), 
                    [](const PeerRate& a, const PeerRate& b) {
                        return a.bytesDownloaded > b.bytesDownloaded;
                    });
                
                // Select top k
                int toSelect = std::min(cfg.common.numberOfPreferredNeighbors, 
                                       static_cast<int>(rates.size()));
                for (int i = 0; i < toSelect; ++i) {
                    preferred.push_back(rates[i].conn);
                }
            }
            
            // Now unchoke preferred, choke others
            std::vector<int> preferredIds;
            for (auto& conn : gAllConnections) {
                if (!conn) continue;
                
                bool isPreferred = false;
                for (auto& pref : preferred) {
                    if (pref.get() == conn.get()) {
                        isPreferred = true;
                        break;
                    }
                }
                
                if (isPreferred) {
                    conn->unchokeRemote();
                    preferredIds.push_back(conn->remotePeerId());
                } else {
                }
            }
            
            logger.onChangePreferredNeighbors(selfId, preferredIds);
        });
        
        // Implement proper optimistic unchoke
        RepeatingTask optimisticTick(cfg.common.optimisticUnchokingIntervalSec, [&]{
            std::lock_guard<std::mutex> lk(gConnectionsMutex);
            
            if (gAllConnections.empty()) {
                return;
            }
            
            // Get list of peers that are:
            // 1. Interested in us
            // 2. Currently choked by us
            std::vector<std::shared_ptr<ConnectionHandler>> candidates;
            for (auto& conn : gAllConnections) {
                if (conn && conn->isTheyInterested() && conn->isAmChokingThem()) {
                    candidates.push_back(conn);
                }
            }
            
            if (candidates.empty()) {
                return; 
            }
            
            // Pick one randomly
            int idx = rand() % candidates.size();
            auto luckyPeer = candidates[idx];
            
            // Unchoke this peer
            luckyPeer->unchokeRemote();
            
            logger.onChangeOptimisticUnchoke(selfId, luckyPeer->remotePeerId());
        });
        
        preferredTick.start(); 
        optimisticTick.start();

        // thread to check for completion
        std::thread completionChecker([&](){
            bool hasLoggedCompletion = false;
            
            while (!gShouldTerminate.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                
                // Update hasCompleteFile flag
                hasCompleteFile = pieceMgr->isComplete();
                
                // Check if this peer has completed download
                if (!hasLoggedCompletion && pieceMgr->isComplete()) {
                    if (!wasInitialSeeder) {
                        logger.onDownloadComplete(selfId);
                        hasLoggedCompletion = true;
                        gHasLoggedCompletion.store(true);
                    }
                }
                
                // Check if ALL peers have the complete file
                bool allComplete = true;
                {
                    std::lock_guard<std::mutex> lk(gConnectionsMutex);
                    
                    if (!pieceMgr->isComplete()) {
                        allComplete = false;
                    } else {
                        // Check if anyone is still interested in us
                        for (auto& conn : gAllConnections) {
                            if (conn && conn->isTheyInterested()) {
                                allComplete = false;
                                break;
                            }
                        }
                    }
                }
                
                if (allComplete && hasLoggedCompletion) {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    logger.info("Peer " + std::to_string(selfId) + 
                              " terminating - all peers have complete file.");
                    gShouldTerminate.store(true);
                    break;
                }
            }
        });

        // Keep main thread alive until termination signal
        logger.info("peerProcess running. Waiting for file transfer completion...");
        
        while (!gShouldTerminate.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        logger.info("Shutting down peer " + std::to_string(selfId));
        
        preferredTick.stop(); 
        optimisticTick.stop();
        
        if (completionChecker.joinable()) {
            completionChecker.join();
        }
        
        server.stop();
        
        // Join all connections
        {
            std::lock_guard<std::mutex> lk(gConnectionsMutex);
            for (auto& h: gAllConnections) {
                if (h) h->join();
            }
            gAllConnections.clear();
        }
        
        logger.info("Peer " + std::to_string(selfId) + " shutdown complete.");
        
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << "\n";
        return 2;
    }
}