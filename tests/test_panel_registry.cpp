#include "Harness.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

#include "shared/ui/FormPlan.h"
#include "shared/ui/PanelRegistry.h"

using namespace vc::ui;

namespace {

constexpr char const* kSettingsPanel = R"({
    "schemaVersion": 1,
    "id": "voicechat.settings.client",
    "title": "voicechat.panel.settings.title",
    "submitButton": "voicechat.panel.settings.submit",
    "elements": [
        { "type": "toggle", "key": "voiceEnabled", "text": "voicechat.opt.voiceEnabled", "default": true }
    ]
})";

constexpr char const* kAdminPanel = R"({
    "schemaVersion": 1,
    "id": "voicechat.admin",
    "title": "voicechat.panel.admin.title",
    "elements": [
        { "type": "toggle", "key": "voiceEnabled", "text": "voicechat.opt.voiceEnabled", "default": true }
    ]
})";

std::filesystem::path makeTempDir(std::string const& name) {
    auto dir = std::filesystem::temp_directory_path() / name;
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
}

void writeFile(std::filesystem::path const& path, std::string const& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

std::optional<PanelDefinition> parseOk(std::string const& json) {
    std::string error;
    auto definition = PanelDefinition::parse(json, error);
    if (!definition) ::vc::test::reportFailure(__FILE__, __LINE__, "parse failed: " + error);
    return definition;
}

std::string prefixed(std::string_view key) { return std::string("R:") + std::string(key); }
PanelTextResolver resolver() { return [](std::string_view key) { return prefixed(key); }; }

} // namespace

TEST(panel_registry_loads_json_and_skips_bad_files) {
    auto const dir = makeTempDir("vc_panel_registry_test");
    writeFile(dir / "voicechat-settings.json", kSettingsPanel);
    writeFile(dir / "voicechat-admin.json", kAdminPanel);
    writeFile(dir / "broken.json", R"({"schemaVersion": 1, "id": "broken",)"); // 截断的 JSON
    writeFile(dir / "notes.txt", "not a panel"); // 非 json 应被忽略

    PanelRegistry registry;
    std::string error;
    registry.load(dir, &error);

    EXPECT_EQ(registry.size(), static_cast<std::size_t>(2));
    EXPECT_FALSE(error.empty()); // 坏文件被跳过并记录，但不影响好文件

    auto const* settings = registry.find("voicechat.settings.client");
    EXPECT_TRUE(settings != nullptr);
    if (settings) EXPECT_EQ(settings->titleKey, std::string("voicechat.panel.settings.title"));
    EXPECT_TRUE(registry.find("voicechat.admin") != nullptr);
    EXPECT_TRUE(registry.find("missing.panel") == nullptr);

    std::error_code code;
    std::filesystem::remove_all(dir, code);
}

TEST(panel_registry_reports_missing_directory) {
    std::string error;
    auto panels = PanelRegistry::loadFromDirectory(
        std::filesystem::temp_directory_path() / "vc_panel_registry_missing_dir",
        &error
    );
    EXPECT_TRUE(panels.empty());
    EXPECT_FALSE(error.empty());
}

TEST(form_plan_maps_definition_to_control_defaults) {
    auto definition = parseOk(R"({
        "schemaVersion": 1,
        "id": "p",
        "title": "t.title",
        "submitButton": "t.submit",
        "elements": [
            { "type": "header", "text": "t.header" },
            { "type": "toggle", "key": "voiceEnabled", "text": "t.voice", "default": false },
            { "type": "dropdown", "key": "talkMode", "text": "t.talk",
              "options": [
                { "value": "disabled", "text": "t.disabled" },
                { "value": "pushToTalk", "text": "t.ptt" }
              ] },
            { "type": "slider", "key": "playbackVolume", "text": "t.volume", "min": 0, "max": 100, "step": 5, "default": 50 },
            { "type": "input", "key": "pttKeyName", "text": "t.key", "placeholder": "V", "default": "X" }
        ]
    })");
    if (!definition) return;

    auto reader = [](std::string const& key) -> PanelValue {
        if (key == "voiceEnabled") return uint64_t{1};
        if (key == "talkMode") return std::string("pushToTalk");
        if (key == "playbackVolume") return 70.0;
        if (key == "pttKeyName") return std::string("V");
        return std::monostate{};
    };

    auto const plan = buildFormPlan(*definition, reader, resolver());
    EXPECT_EQ(plan.size(), static_cast<std::size_t>(5));
    if (plan.size() != 5) return;

    EXPECT_EQ(plan[0].type, PanelElementType::Header);
    EXPECT_EQ(plan[0].text, std::string("R:t.header"));

    EXPECT_EQ(plan[1].type, PanelElementType::Toggle);
    EXPECT_TRUE(plan[1].defaultBool); // 当前值覆盖定义默认值

    EXPECT_EQ(plan[2].type, PanelElementType::Dropdown);
    EXPECT_EQ(plan[2].options.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(plan[2].options[0], std::string("R:t.disabled"));
    EXPECT_EQ(plan[2].defaultIndex, static_cast<std::size_t>(1)); // 由当前值 "pushToTalk" 解析出下标

    EXPECT_EQ(plan[3].type, PanelElementType::Slider);
    EXPECT_NEAR(plan[3].defaultNumber, 70.0, 1e-9);
    EXPECT_NEAR(plan[3].step, 5.0, 1e-9);

    EXPECT_EQ(plan[4].type, PanelElementType::Input);
    EXPECT_EQ(plan[4].name, std::string("pttKeyName"));
    EXPECT_EQ(plan[4].defaultString, std::string("V"));
}

TEST(form_plan_falls_back_to_defined_defaults) {
    auto definition = parseOk(R"({
        "schemaVersion": 1,
        "id": "p",
        "title": "t.title",
        "elements": [
            { "type": "toggle", "key": "hudEnabled", "text": "t.hud", "default": true },
            { "type": "slider", "key": "maxSubtitleLines", "text": "t.lines", "min": 1, "max": 8, "default": 3 }
        ]
    })");
    if (!definition) return;

    auto const plan = buildFormPlan(*definition, [](std::string const&) { return PanelValue{}; }, resolver());
    EXPECT_EQ(plan.size(), static_cast<std::size_t>(2));
    if (plan.size() != 2) return;
    EXPECT_TRUE(plan[0].defaultBool);                     // 定义默认值
    EXPECT_NEAR(plan[1].defaultNumber, 3.0, 1e-9);
    EXPECT_NEAR(plan[1].step, 1.0, 1e-9);                 // step 缺省时收敛为 1，保证 Slider 有效
}

TEST(form_response_parses_custom_form_array) {
    auto definition = parseOk(R"({
        "schemaVersion": 1,
        "id": "p",
        "title": "t.title",
        "elements": [
            { "type": "header", "text": "t.header" },
            { "type": "toggle", "key": "voiceEnabled", "text": "t.voice" },
            { "type": "dropdown", "key": "talkMode", "text": "t.talk",
              "options": [
                { "value": "disabled", "text": "t.disabled" },
                { "value": "pushToTalk", "text": "t.ptt" }
              ] },
            { "type": "slider", "key": "playbackVolume", "text": "t.volume", "min": 0, "max": 100, "step": 5 },
            { "type": "input", "key": "pttKeyName", "text": "t.key" }
        ]
    })");
    if (!definition) return;

    // 响应数组与表单内容一一对应：header 占位、toggle 布尔、dropdown 下标、slider 数值、input 文本
    std::string error;
    auto const values = parseFormResponse(*definition, R"(["", true, 1, 42.5, "V"])", &error);
    EXPECT_TRUE(error.empty());

    auto const voice = values.find("voiceEnabled");
    EXPECT_TRUE(voice != values.end());
    if (voice != values.end()) EXPECT_EQ(std::get<uint64_t>(voice->second), 1ull);

    auto const talk = values.find("talkMode");
    EXPECT_TRUE(talk != values.end());
    if (talk != values.end()) EXPECT_EQ(std::get<std::string>(talk->second), std::string("pushToTalk"));

    auto const volume = values.find("playbackVolume");
    EXPECT_TRUE(volume != values.end());
    if (volume != values.end()) EXPECT_NEAR(std::get<double>(volume->second), 42.5, 1e-9);

    auto const key = values.find("pttKeyName");
    EXPECT_TRUE(key != values.end());
    if (key != values.end()) EXPECT_EQ(std::get<std::string>(key->second), std::string("V"));

    // 非交互元素不产生配置值
    EXPECT_TRUE(values.find("t.header") == values.end());
}

TEST(form_response_rejects_bad_payload) {
    auto definition = parseOk(R"({
        "schemaVersion": 1,
        "id": "p",
        "title": "t.title",
        "elements": [
            { "type": "toggle", "key": "voiceEnabled", "text": "t.voice" }
        ]
    })");
    if (!definition) return;

    std::string error;
    auto const values = parseFormResponse(*definition, "not valid json", &error);
    EXPECT_TRUE(values.empty());
    EXPECT_FALSE(error.empty());

    std::string typeError;
    auto const objectValues = parseFormResponse(*definition, R"({"voiceEnabled": true})", &typeError);
    EXPECT_TRUE(objectValues.empty());
    EXPECT_FALSE(typeError.empty());
}
