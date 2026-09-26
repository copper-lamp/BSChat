#include "shared/util/FileLog.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace bsc::shared {

namespace {

std::mutex& fileMutex() {
    static std::mutex instance;
    return instance;
}

bool& logReady() {
    static bool instance = false;
    return instance;
}

// 缓存流句柄：音频链路每秒可能写入多次，逐次开关文件代价过高。
std::ofstream& logStream() {
    static std::ofstream instance;
    return instance;
}

std::string formatLine(std::string_view level, std::string_view message) {
    auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch()
    )
                         .count();
    std::string line;
    line.reserve(message.size() + level.size() + 32);
    line += '[';
    line += std::to_string(now);
    line += "] [";
    line += level;
    line += "] ";
    line += message;
    line += '\n';
    return line;
}

} // namespace

void FileLog::reset(std::string const& path, std::string_view header) {
    std::lock_guard<std::mutex> guard(fileMutex());
    if (logStream().is_open()) logStream().close();
    std::error_code error;
    std::filesystem::path const target(path);
    if (!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path(), error);
    logStream().open(target, std::ios::binary | std::ios::trunc);
    if (!logStream()) {
        logReady() = false;
        return;
    }
    logReady() = true;
    std::string banner = header.empty() ? std::string("bschat log started") : std::string(header);
    logStream() << formatLine("info", banner);
    logStream().flush();
}

void FileLog::info(std::string_view message) { write("info", message); }
void FileLog::warn(std::string_view message) { write("warn", message); }
void FileLog::error(std::string_view message) { write("error", message); }

bool FileLog::ready() { return logReady(); }

void FileLog::write(std::string_view level, std::string_view message) {
    std::lock_guard<std::mutex> guard(fileMutex());
    if (!logStream().is_open()) return;
    logStream() << formatLine(level, message);
    // 立即 flush：崩溃取证场景下缓冲内容会丢失。
    logStream().flush();
}

} // namespace bsc::shared
