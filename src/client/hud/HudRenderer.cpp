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
#include "mc/deps/input/RectangleArea.h"

namespace vc::client::hud {

bool HudRenderer::draw(ll::event::render::AfterUIRenderEvent& event, Frame const& frame, Layout const& layout) const {
    if (frame.subtitles.empty() && frame.statusText.empty()) return false;

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

    // 状态文案：左下角
    if (!frame.statusText.empty()) {
        std::string text = frame.statusText;
        RectangleArea rect(
            layout.statusX,
            height - layout.statusBottomOffset - layout.subtitleLineHeight,
            width,
            height - layout.statusBottomOffset,
            false
        );
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
    context.flushText(0.0f, std::nullopt);
    return true;
}

} // namespace vc::client::hud
