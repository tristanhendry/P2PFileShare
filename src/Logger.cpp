#include "p2p/Logger.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace p2p {

    Logger::Logger(std::string path) : out_(std::move(path), std::ios::app) {
        if (!out_) {
            std::cerr << "Failed to open log file: " << path << std::endl;
            throw std::runtime_error("Logger failed to open file");
        }
    }
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

} // namespace p2p