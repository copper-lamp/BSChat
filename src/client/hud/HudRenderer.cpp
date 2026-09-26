#include "client/hud/HudRenderer.h"

#include <optional>
#include <utility>

#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/service/TargetedBedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/TextAlignment.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/input/RectangleArea.h"

namespace vc::client::hud {
namespace {

// flushImages 提交贴图时使用的 Bedrock UI 材质名。
// 注意：SDK 头文件里没有材质名字符串（材质由 .material.bin 在运行时注册，
// 头文件只有 RenderMaterialGroup 的查询接口），此值沿用社区通行写法，
// 尚未在真机验证；若真机贴图不显示，优先改这里。
constexpr char kImageMaterialName[] = "ui_textured_and_glcolor";

HashedString const& imageMaterialName() {
    // 静态构造一次，避免逐帧构造 HashedString 触发堆分配。
    static HashedString const name{kImageMaterialName};
    return name;
}

} // namespace

bool HudRenderer::draw(ll::event::render::AfterUIRenderEvent& event, Frame const& frame, Layout const& layout) const {
    if (frame.subtitles.empty() && frame.statusText.empty() && frame.statusIcon == nullptr) return false;

    auto client = ll::service::getClientInstance();
    if (!client) return false;
    FontHandle fontHandle = client->getFontHandle();
    if (!fontHandle.mDefaultFont) return false;
    Font& font = *fontHandle.mDefaultFont;

    auto& context = event.uiRenderContext();
    // mSize 是 UI 缩放后的逻辑尺寸，用它做底部锚定，避免固定坐标在不同分辨率/GUI 缩放下漂移。
    glm::vec2 const size = event.screenView().mSize.get();
    float const width = size.x;
    float const height = size.y;
    if (width <= 0.0f || height <= 0.0f) return false;

    float const alpha = 1.0f;
    bool        drewImage = false;

    // 状态：左下角，图标 + 间距 + 文字
    if (!frame.statusText.empty() || frame.statusIcon != nullptr) {
        float const lineTop    = height - layout.statusBottomOffset - layout.subtitleLineHeight;
        float const lineBottom = height - layout.statusBottomOffset;
        float       textX      = layout.statusX;

        if (frame.statusIcon != nullptr) {
            // 图标 12x12 与文字行同高（subtitleLineHeight），底边对齐文字行底边。
            glm::vec2 const iconSize(layout.statusIconSize, layout.statusIconSize);
            context.drawImage(
                *frame.statusIcon,
                glm::vec2(layout.statusX, lineTop),
                iconSize,
                glm::vec2(0.0f, 0.0f),
                glm::vec2(1.0f, 1.0f),
                false
            );
            drewImage = true;
            textX += layout.statusIconSize + layout.statusIconGap;
        }

        if (!frame.statusText.empty()) {
            std::string text = frame.statusText;
            RectangleArea rect(textX, lineTop, width, lineBottom, false);
            context.drawText(
                font,
                rect,
                std::move(text),
                mce::Color(0.85f, 0.92f, 1.0f, 1.0f),
                alpha,
                ui::TextAlignment::Left,
                TextMeasureData{layout.fontSize, 0.0f, true, false, false, ui::TextAlignment::Left},
                CaretMeasureData{0, false}
            );
        }
    }

    // 字幕：底部居中，越新的行越靠下
    std::size_t const count = frame.subtitles.size();
    for (std::size_t i = 0; i < count; ++i) {
        // i = 0 是最早的一行；最后一行贴近 subtitleBottomOffset
        float const distanceFromBottom = layout.subtitleBottomOffset + static_cast<float>(count - 1 - i) * layout.subtitleLineHeight;
        float const y0 = height - distanceFromBottom - layout.subtitleLineHeight;
        float const y1 = y0 + layout.subtitleLineHeight;
        std::string text = frame.subtitles[i].text;
        RectangleArea rect(0.0f, y0, width, y1, false);
        context.drawText(
            font,
            rect,
            std::move(text),
            mce::Color(1.0f, 1.0f, 1.0f, 1.0f),
            alpha,
            ui::TextAlignment::Center,
            TextMeasureData{layout.fontSize, 0.0f, true, false, false, ui::TextAlignment::Center},
            CaretMeasureData{0, false}
        );
    }

    // origin() 内部已经提交过一次绘制，这里必须自己收尾，否则新画的内容不会上屏。
    // 图和文字走两条提交路径，各收各的。
    if (drewImage) {
        context.flushImages(mce::Color(1.0f, 1.0f, 1.0f, 1.0f), alpha, imageMaterialName());
    }
    context.flushText(0.0f, std::nullopt);
    return true;
}

} // namespace vc::client::hud
