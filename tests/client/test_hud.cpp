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

SubtitleLine namedLine(uint8_t who, std::string name, std::string text, bool isFinal, uint64_t utteranceId) {
    SubtitleLine l = line(who, std::move(text), isFinal);
    l.speakerName = std::move(name);
    l.utteranceId = utteranceId;
    return l;
}

SubtitleOverlay::Options makeOptions(int maxLines, int64_t fadeMs, int maxCharsPerLine = 25) {
    SubtitleOverlay::Options options;
    options.maxLines = maxLines;
    options.maxCharsPerLine = maxCharsPerLine;
    options.fadeMs = fadeMs;
    options.enabled = true;
    return options;
}

} // namespace

TEST(hud_subtitle_keeps_only_newest_lines) {
    SubtitleOverlay overlay(makeOptions(3, 5000));
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
    SubtitleOverlay overlay(makeOptions(4, 5000));
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
    SubtitleOverlay overlay(makeOptions(4, 5000));
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
    SubtitleOverlay overlay(makeOptions(4, 1000));
    overlay.push(line(1, "hello", true), 1000);

    EXPECT_EQ(overlay.visibleLines(1500).size(), static_cast<std::size_t>(1));
    EXPECT_EQ(overlay.visibleLines(2000).size(), static_cast<std::size_t>(1)); // 恰好 1 秒仍在
    EXPECT_EQ(overlay.visibleLines(2001).size(), static_cast<std::size_t>(0));

    overlay.clear();
    EXPECT_EQ(overlay.visibleLines(1500).size(), static_cast<std::size_t>(0));
}

// 说话过程中的部分结果会不断刷新显示计时，不能因为一句话说得久就被淡出。
TEST(hud_subtitle_partial_updates_keep_line_alive) {
    SubtitleOverlay overlay(makeOptions(4, 1000));
    overlay.push(line(1, "我在", false), 1000);
    overlay.push(line(1, "我在说一句很长的话", false), 1800);

    EXPECT_EQ(overlay.visibleLines(2500).size(), static_cast<std::size_t>(1));
    EXPECT_EQ(overlay.visibleLines(2801).size(), static_cast<std::size_t>(0));
}

TEST(hud_subtitle_wraps_long_text_and_counts_speaker_prefix) {
    // 前缀「Steve：」共 6 个字符，maxChars=10 → 第一段最多再放 4 个正文字符。
    SubtitleLine l = namedLine(1, "Steve", "一二三四五六七八九十", true, 7);
    auto rows = SubtitleOverlay::wrapForDisplay(l, 10);
    EXPECT_TRUE(rows.size() >= 2);
    if (!rows.empty()) EXPECT_EQ(rows[0], std::string("Steve：一二三四"));
    // 所有段落拼起来必须与原文一致，且不能切坏 UTF-8。
    std::string joined;
    for (auto const& row : rows) joined += row;
    EXPECT_EQ(joined, std::string("Steve：一二三四五六七八九十"));
    for (auto const& row : rows) {
        std::size_t chars = 0;
        for (std::size_t i = 0; i < row.size();) {
            unsigned char const lead = static_cast<unsigned char>(row[i]);
            std::size_t const len = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : 4;
            i += std::min(len, row.size() - i);
            ++chars;
        }
        EXPECT_TRUE(chars <= 10);
    }
}

TEST(hud_subtitle_wrap_prefers_breaking_ascii_words) {
    SubtitleLine l = line(2, "hello world foo", true);
    auto rows = SubtitleOverlay::wrapForDisplay(l, 10);
    EXPECT_EQ(rows.size(), static_cast<std::size_t>(2));
    if (rows.size() == 2) {
        EXPECT_EQ(rows[0], std::string("hello"));
        EXPECT_EQ(rows[1], std::string("world foo"));
    }
}

// 一句超长的话换行后超过显示行数时，保留最后（最新）的几段，而不是截掉正在说的话。
TEST(hud_subtitle_keeps_tail_rows_of_long_utterance) {
    SubtitleOverlay overlay(makeOptions(2, 5000, 5));
    overlay.push(line(1, "一二三四五六七八九十甲乙丙丁戊", true), 1000);

    auto visible = overlay.visibleLines(1000);
    EXPECT_EQ(visible.size(), static_cast<std::size_t>(2));
    if (visible.size() == 2) {
        EXPECT_EQ(visible[0].text, std::string("六七八九十"));
        EXPECT_EQ(visible[1].text, std::string("甲乙丙丁戊"));
    }
}

TEST(hud_subtitle_disabled_ignores_pushes) {
    SubtitleOverlay overlay(makeOptions(4, 5000));
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

TEST(hud_subtitle_defaults_match_product_settings) {
    SubtitleOverlay::Options const defaults{};
    EXPECT_EQ(defaults.maxLines, 4);
    EXPECT_EQ(defaults.maxCharsPerLine, 25);
    EXPECT_EQ(defaults.fadeMs, static_cast<int64_t>(1000));
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
