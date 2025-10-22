#ifndef P2P_LOGGER_HPP
#define P2P_LOGGER_HPP

#include <fstream>
#include <mutex>
#include <string>
#include <chrono>
#include <ctime>

namespace p2p {

    class Logger {
    public:
        explicit Logger(const std::string& path);
        ~Logger();

        void info(const std::string& msg);

        [[maybe_unused]] void error(const std::string& msg);

        // Required log formats (subset for midpoint)
        void onConnectOut(int fromId, int toId);

        [[maybe_unused]] void onConnectIn(int toId, int fromId);

    private:
        std::ofstream out_;
        std::mutex mtx_;
        static std::string nowTs();
        void write(const std::string& level, const std::string& msg);
    };

} // namespace p2p

#endif // P2P_LOGGER_HPP