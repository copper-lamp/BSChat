#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "core/protocol/Message.h"

namespace vc::client::hud {

// 字幕数据模型：只负责「谁说了什么、什么时候说的、还要显示多久」，
// 不包含任何绘制与 LeviLamina 依赖，可 host 单测。
// 线程模型：push 由网络线程调用，visibleLines 由渲染线程调用，内部用互斥锁保护。

struct SubtitleLine {
    protocol::PlayerId speaker{};
    std::string text;
    bool isFinal = false;
    int64_t shownAtMs = 0;
};

class SubtitleOverlay final {
public:
    struct Options {
        int maxLines = 4;      // 同时显示的行数上限
        int64_t fadeMs = 5000; // 一行从出现到消失的时长
        bool enabled = true;   // 字幕开关（关闭时不入队也不显示）
    };

    SubtitleOverlay() = default;
    explicit SubtitleOverlay(Options options);

    void setOptions(Options options);
    Options options() const;

    void setEnabled(bool enabled);
    bool enabled() const;

    // 网络线程：入队一条字幕。同一说话者的连续部分结果会覆盖上一行，最终结果开启新行。
    void push(SubtitleLine line, int64_t nowMs);

    // 渲染线程：返回当前应显示的行（按时间先后），并顺手清理已过期行。
    std::vector<SubtitleLine> visibleLines(int64_t nowMs);

    void clear();

private:
    mutable std::mutex mutex_;
    Options options_{};
    std::vector<SubtitleLine> lines_;
};

} // namespace vc::client::hud
