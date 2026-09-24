#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace vc::shared {

// 纯文本文件日志。
//
// 存在意义：LeviLamina 的日志只写控制台窗口，而模组内置自检（SmokeTest）以及
// 服务端运行期诊断需要在没有控制台可看的情况下也能取证。此工具把同一条消息
// 同时写入固定路径的 txt 文件，与宿主日志互为备份，互不依赖。
//
// 约定：
//   - 文件为 UTF-8，逐行追加，格式为 `[ms] [LEVEL] message`，ms 为毫秒级 unix 时间戳。
//   - 进程启动后首次写入会覆盖旧文件，避免多次启动的日志混杂。
//   - 目录不存在时自动创建；任何 IO 失败都静默忽略，绝不因日志问题影响语音链路。
class FileLog final {
public:
    // 清空文件并写入头部，标记本次进程启动。
    static void reset(std::string const& path, std::string_view header);
    // 追加一行 info 消息。
    static void info(std::string_view message);
    // 追加一行 warn 消息。
    static void warn(std::string_view message);
    // 追加一行 error 消息。
    static void error(std::string_view message);
    // 是否已成功初始化。
    static bool ready();

private:
    static void write(std::string_view level, std::string_view message);
};

} // namespace vc::shared
