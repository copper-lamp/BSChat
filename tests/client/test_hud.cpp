#include "Harness.h"

#include <cstdint>
#include <string>

#include "client/hud/StatusOverlay.h"
#include "client/hud/SubtitleOverlay.h"

using namespace bsc::client::hud;
using namespace bsc::protocol;

namespace {

PlayerId speaker(uint8_t v) {
    PlayerId id{};
    id[0] = v;
    return id;
}

SubtitleLine line(uint8_t who, std::string text, bool isFinal) {
    SubtitleLine l;
    l.speaker = speaker(who);
    l.text = std::move(text);
    l.isFinal = isFinal;
    return l;
}

} // namespace

TEST(hud_subtitle_keeps_only_newest_lines) {
    SubtitleOverlay overlay(SubtitleOverlay::Options{3, 5000, true});
    for (uint8_t i = 1; i <= 5; ++i) {
        overlay.push(line(i, "line" + std::to_string(i), true), 1000);
    }
    auto visible = overlay.visibleLines(1000);
    EXPECT_EQ(visible.size(), static_cast<std::size_t>(3));
    if (visible.size() == 3) {
        EXPECT_EQ(visible[0].text, std::string("line3"));
        EXPECT_EQ(visible[2].text, std::string("line5"));
    }
}

TEST(hud_subtitle_replaces_partial_of_same_speaker) {
    SubtitleOverlay overlay(SubtitleOverlay::Options{4, 5000, true});
    overlay.push(line(1, "你", false), 1000);
    overlay.push(line(1, "你好", false), 1100);
    overlay.push(line(1, "你好世", false), 1200);

    auto visible = overlay.visibleLines(1200);
    EXPECT_EQ(visible.size(), static_cast<std::size_t>(1));
    if (!visible.empty()) {
        EXPECT_EQ(visible[0].text, std::string("你好世"));
        EXPECT_EQ(visible[0].shownAtMs, 1200);
    }
}

TEST(hud_subtitle_final_then_partial_starts_new_line) {
    SubtitleOverlay overlay(SubtitleOverlay::Options{4, 5000, true});
    overlay.push(line(1, "第一句", true), 1000);
    overlay.push(line(1, "第二", false), 1100);

    auto visible = overlay.visibleLines(1100);
    EXPECT_EQ(visible.size(), static_cast<std::size_t>(2));
    if (visible.size() == 2) {
        EXPECT_EQ(visible[0].text, std::string("第一句"));
        EXPECT_EQ(visible[1].text, std::string("第二"));
    }
}

TEST(hud_subtitle_expires_after_fade) {
    SubtitleOverlay overlay(SubtitleOverlay::Options{4, 1000, true});
    overlay.push(line(1, "hello", true), 1000);

    EXPECT_EQ(overlay.visibleLines(1500).size(), static_cast<std::size_t>(1));
    EXPECT_EQ(overlay.visibleLines(2000).size(), static_cast<std::size_t>(1));
    EXPECT_EQ(overlay.visibleLines(2001).size(), static_cast<std::size_t>(0));

    overlay.clear();
    EXPECT_EQ(overlay.visibleLines(1500).size(), static_cast<std::size_t>(0));
}

TEST(hud_subtitle_disabled_ignores_pushes) {
    SubtitleOverlay overlay(SubtitleOverlay::Options{4, 5000, true});
    overlay.push(line(1, "before", true), 1000);
    EXPECT_EQ(overlay.visibleLines(1000).size(), static_cast<std::size_t>(1));

    overlay.setEnabled(false);
    EXPECT_FALSE(overlay.enabled());
    overlay.push(line(2, "while-disabled", true), 1100);
    EXPECT_EQ(overlay.visibleLines(1100).size(), static_cast<std::size_t>(0));

    overlay.setEnabled(true);
    overlay.push(line(3, "after", true), 1200);
    // 重新打开：关闭期间的字幕没有入队，但未过期的旧行仍在显示窗口内
    auto visible = overlay.visibleLines(1200);
    EXPECT_EQ(visible.size(), static_cast<std::size_t>(2));
    if (visible.size() == 2) {
        EXPECT_EQ(visible[0].text, std::string("before"));
        EXPECT_EQ(visible[1].text, std::string("after"));
    }
}

TEST(hud_status_idle_when_not_in_session) {
    StatusOverlay status;
    EXPECT_EQ(status.status(), AudioStatus::Idle);

    status.update(StatusInputs{false, true, true, true});
    EXPECT_EQ(status.status(), AudioStatus::Idle);
}

TEST(hud_status_priority) {
    StatusOverlay status;

    status.update(StatusInputs{true, false, false, false});
    EXPECT_EQ(status.status(), AudioStatus::Silent);

    status.update(StatusInputs{true, false, false, true});
    EXPECT_EQ(status.status(), AudioStatus::Playing);

    status.update(StatusInputs{true, true, false, false});
    EXPECT_EQ(status.status(), AudioStatus::Speaking);

    // 静音优先级最高：即使同时在说或在听
    status.update(StatusInputs{true, true, true, true});
    EXPECT_EQ(status.status(), AudioStatus::Muted);
}

TEST(hud_status_i18n_keys) {
    EXPECT_EQ(StatusOverlay::i18nKey(AudioStatus::Idle), std::string_view("bschat.status.idle"));
    EXPECT_EQ(StatusOverlay::i18nKey(AudioStatus::Speaking), std::string_view("bschat.status.speaking"));
    EXPECT_EQ(StatusOverlay::i18nKey(AudioStatus::Muted), std::string_view("bschat.status.muted"));
    EXPECT_EQ(StatusOverlay::i18nKey(AudioStatus::Playing), std::string_view("bschat.status.playing"));
    EXPECT_EQ(StatusOverlay::i18nKey(AudioStatus::Silent), std::string_view("bschat.status.silent"));
}
