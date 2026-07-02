#include "core/log.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <mutex>

namespace sm::log {

namespace {
std::mutex g_mutex;
std::string g_path;
bool g_enabled = false;
} // namespace

void init(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_path = path;
    g_enabled = !path.empty();
}

bool enabled() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_enabled;
}

void write(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_enabled) return;
    std::ofstream f(g_path, std::ios::app);
    if (!f) return;

    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char stamp[32] = "";
    // std::localtime uses a static buffer; safe here because g_mutex serializes writes.
    if (std::tm* tmv = std::localtime(&t))
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", tmv);
    f << stamp << " " << msg << "\n";
}

} // namespace sm::log
