#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "core/protocol/Message.h"

namespace bsc::client::hud {

// 字幕数据模型：只负责「谁说了什么、什么时候说的、还要显示多久」，
// 不包含任何绘制与 LeviLamina 依赖，可 host 单测。
// 线程模型：push 由网络线程调用，visibleLines 由渲染线程调用，内部用互斥锁保护。

struct SubtitleLine {
    protocol::PlayerId speaker{};
    std::string speakerName;
    std::string text;
    bool isFinal = false;
    uint64_t utteranceId = 0;
    int64_t shownAtMs = 0;
};

class SubtitleOverlay final {
public:
    struct Options {
        int maxLines = 4;          // 同时显示的屏幕行数上限（换行后的行）
        int maxCharsPerLine = 25;  // 一行最多多少个字符（含「名字：」前缀）
        int64_t fadeMs = 1000;     // 一句话说完后多少毫秒消失
        bool enabled = true;       // 字幕开关（关闭时不入队也不显示）
    };

    SubtitleOverlay() = default;
    explicit SubtitleOverlay(Options options);

    void setOptions(Options options);
    Options options() const;

    void setEnabled(bool enabled);
    bool enabled() const;

    // 网络线程：入队一条字幕。同一说话者同一句话的部分结果会覆盖已有行；
    // 每收到一次更新都会重置该行的显示计时（说话过程中不消失）。
    void push(SubtitleLine line, int64_t nowMs);

    // 渲染线程：返回当前应显示的行（按时间先后，最后一行在最下方），并顺手清理已过期行。
    // 返回的是「换行后的屏幕行」：超长文本按 maxCharsPerLine 切分，说话者前缀只出现在第一段，
    // 总行数不超过 maxLines（超出时优先保留最新的话）。
    std::vector<SubtitleLine> visibleLines(int64_t nowMs);

    // 把一条字幕按 maxCharsPerLine 切成若干显示行（含「名字：」前缀，前缀计入长度）。
    // 公开为静态函数便于 host 单测；UTF-8 按码点切分，不会切坏多字节字符。
    static std::vector<std::string> wrapForDisplay(const SubtitleLine& line, int maxCharsPerLine);

    void clear();

private:
    mutable std::mutex mutex_;
    Options options_{};
    std::vector<SubtitleLine> lines_;
};

} // namespace bsc::client::hud
