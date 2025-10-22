#ifndef P2P_SCHEDULER_HPP
#define P2P_SCHEDULER_HPP

#include <thread>
#include <atomic>
#include <functional>
#include <chrono>

namespace p2p {

    class RepeatingTask {
    public:
        template<class F>
        RepeatingTask(int intervalSec, F&& f) : interval_(intervalSec), fn_(std::forward<F>(f)) {}
        ~RepeatingTask() { stop(); }

        void start() {
            running_.store(true);
            thr_ = std::thread([this]{
                while (running_.load()) {
                    auto next = std::chrono::steady_clock::now() + std::chrono::seconds(interval_);
                    try { fn_(); } catch (...) { /* swallow for midpoint */ }
                    std::this_thread::sleep_until(next);
                }
            });
        }

        void stop() {
            running_.store(false);
            if (thr_.joinable()) thr_.join();
        }

    private:
        int interval_;
        std::function<void()> fn_;
        std::atomic<bool> running_{false};
        std::thread thr_;
    };

} // namespace p2p

#endif // P2P_SCHEDULER_HPP