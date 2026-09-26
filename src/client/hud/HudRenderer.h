#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "client/hud/StatusOverlay.h"
#include "client/hud/SubtitleOverlay.h"

namespace ll::event::inline render {
class AfterUIRenderEvent;
}

namespace mce {
struct ClientTexture;
}

namespace vc::client::hud {

// HUD 绘制实现：唯一一处直接调用 Bedrock UI 渲染上下文的地方。
// 只负责「把已经算好的内容画到屏幕上」，不含状态推导与配置读取，便于替换绘制载体。
class HudRenderer final {
public:
    // 布局常量集中在结构体里，方便按 UI 缩放实测调整。
    struct Layout {
        float fontSize = 1.0f;
        float subtitleLineHeight = 12.0f;
        float subtitleBottomOffset = 46.0f; // 字幕底部到屏幕底边的距离
        float statusX = 10.0f;
        float statusBottomOffset = 16.0f;
        // 状态图标：12x12 与文字行高（subtitleLineHeight）一致，图标底与文字行底对齐；
        // 文字左边界 = statusX + statusIconSize + statusIconGap（无图标时仍是 statusX）。
        // 均为 UI 缩放坐标常量，未按不同 GUI 缩放真机校准。
        float statusIconSize = 12.0f;
        float statusIconGap = 4.0f;
    };

    struct Frame {
        std::vector<SubtitleLine> subtitles; // 按时间先后，最后一条在最下方
        std::string statusText;              // 已本地化的状态文案，空表示不画
        // 状态图标贴图；为空（资源包未启用或加载失败）时状态只画文字。
        mce::ClientTexture const* statusIcon = nullptr;
    };

    // 返回是否真的提交了绘制（供日志与自检使用）。
    bool draw(ll::event::render::AfterUIRenderEvent& event, Frame const& frame, Layout const& layout) const;
};

} // namespace vc::client::hud
