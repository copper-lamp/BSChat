#include "Harness.h"

#include <cstdint>
#include <string>
#include <variant>

#include "client/ui/ConfigBinding.h"
#include "core/config/Config.h"

using namespace vc::client::ui;

namespace config = vc::config;

TEST(config_binding_reads_current_values) {
    config::ClientConfig c;
    c.voiceEnabled = true;
    c.vadEnabled = false;
    c.captureEnabled = false;
    c.playbackVolume = 0.5f;
    c.subtitleEnabled = false;
    c.maxSubtitleLines = 2;
    c.hudEnabled = true;

    EXPECT_EQ(std::get<uint64_t>(ConfigBinding::read(c, "voiceEnabled")), 1ull);
    EXPECT_EQ(std::get<std::string>(ConfigBinding::read(c, "talkMode")), std::string("pushToTalk"));
    EXPECT_EQ(std::get<uint64_t>(ConfigBinding::read(c, "captureEnabled")), 0ull);
    EXPECT_NEAR(std::get<double>(ConfigBinding::read(c, "playbackVolume")), 50.0, 1e-6);
    EXPECT_EQ(std::get<uint64_t>(ConfigBinding::read(c, "subtitleEnabled")), 0ull);
    EXPECT_NEAR(std::get<double>(ConfigBinding::read(c, "maxSubtitleLines")), 2.0, 1e-9);
    EXPECT_EQ(std::get<uint64_t>(ConfigBinding::read(c, "hudEnabled")), 1ull);

    // 未知键 → monostate，绘制层据此跳过
    EXPECT_TRUE(std::holds_alternative<std::monostate>(ConfigBinding::read(c, "unknownKey")));
}

TEST(config_binding_talk_mode_is_derived) {
    config::ClientConfig c;

    c.voiceEnabled = true;
    c.vadEnabled = true;
    EXPECT_EQ(std::get<std::string>(ConfigBinding::read(c, "talkMode")), std::string("voiceActivity"));

    c.voiceEnabled = false;
    EXPECT_EQ(std::get<std::string>(ConfigBinding::read(c, "talkMode")), std::string("disabled"));

    c.voiceEnabled = true;
    c.vadEnabled = false;
    EXPECT_EQ(std::get<std::string>(ConfigBinding::read(c, "talkMode")), std::string("pushToTalk"));
}

TEST(config_binding_applies_valid_values) {
    config::ClientConfig c;
    PanelValues values;
    values["talkMode"] = std::string("voiceActivity");
    values["playbackVolume"] = 65.0;
    values["maxSubtitleLines"] = 2.0;
    values["hudEnabled"] = uint64_t{0};
    values["captureEnabled"] = uint64_t{1};

    auto applied = ConfigBinding::apply(c, values);

    EXPECT_EQ(applied, static_cast<std::size_t>(5));
    EXPECT_TRUE(c.voiceEnabled);
    EXPECT_TRUE(c.vadEnabled);
    EXPECT_NEAR(c.playbackVolume, 0.65f, 1e-5);
    EXPECT_EQ(c.maxSubtitleLines, 2);
    EXPECT_FALSE(c.hudEnabled);
    EXPECT_TRUE(c.captureEnabled);
}

TEST(config_binding_rejects_unknown_and_invalid) {
    config::ClientConfig c; // 默认：playbackVolume=1.0、maxSubtitleLines=4、hudEnabled=true
    PanelValues values;
    values["unknownKey"] = uint64_t{1};
    values["talkMode"] = std::string("telepathy");  // 未知枚举
    values["playbackVolume"] = 500.0;               // 越界
    values["maxSubtitleLines"] = 0.0;               // 越界（下限 1）
    values["hudEnabled"] = std::string("yes");      // 类型不符

    auto applied = ConfigBinding::apply(c, values);

    EXPECT_EQ(applied, static_cast<std::size_t>(0));
    EXPECT_NEAR(c.playbackVolume, 1.0f, 1e-6);
    EXPECT_EQ(c.maxSubtitleLines, 4);
    EXPECT_TRUE(c.hudEnabled);
    EXPECT_TRUE(c.voiceEnabled);
}

TEST(config_binding_reports_supported_keys) {
    EXPECT_TRUE(ConfigBinding::supported("voiceEnabled"));
    EXPECT_TRUE(ConfigBinding::supported("talkMode"));
    EXPECT_TRUE(ConfigBinding::supported("captureEnabled"));
    EXPECT_TRUE(ConfigBinding::supported("playbackVolume"));
    EXPECT_TRUE(ConfigBinding::supported("subtitleEnabled"));
    EXPECT_TRUE(ConfigBinding::supported("maxSubtitleLines"));
    EXPECT_TRUE(ConfigBinding::supported("hudEnabled"));
    EXPECT_FALSE(ConfigBinding::supported("nope"));
    EXPECT_FALSE(ConfigBinding::supported(""));
}

TEST(config_client_panel_fields_roundtrip) {
    config::ClientConfig c;
    c.captureEnabled = false;
    c.hudEnabled = false;
    c.playbackVolume = 0.25f;

    auto restored = config::clientConfigFromJson(config::clientConfigToJson(c));

    EXPECT_FALSE(restored.captureEnabled);
    EXPECT_FALSE(restored.hudEnabled);
    EXPECT_NEAR(restored.playbackVolume, 0.25f, 1e-6);
}

TEST(config_client_volume_clamped_on_load) {
    // 手改配置文件可能写出非法音量：加载时收敛到 0..1，避免直接送进播放设备。
    EXPECT_NEAR(config::clientConfigFromJson(R"({"playbackVolume": 5.0})").playbackVolume, 1.0f, 1e-6);
    EXPECT_NEAR(config::clientConfigFromJson(R"({"playbackVolume": -3.0})").playbackVolume, 0.0f, 1e-6);
    EXPECT_NEAR(config::clientConfigFromJson(R"({"playbackVolume": 0.4})").playbackVolume, 0.4f, 1e-6);
}
