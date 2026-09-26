#include "Harness.h"

#include <string>

#include "shared/ui/PanelDefinition.h"

using namespace bsc::ui;

namespace {

std::optional<PanelDefinition> parseOk(std::string const& json) {
    std::string error;
    auto definition = PanelDefinition::parse(json, error);
    if (!definition) {
        ::bsc::test::reportFailure(__FILE__, __LINE__, "parse failed: " + error);
    }
    return definition;
}

} // namespace

TEST(ui_panel_definition_parses_elements) {
    auto definition = parseOk(R"({
        "schemaVersion": 1,
        "id": "bschat.settings.client",
        "title": "bschat.panel.settings.title",
        "submitButton": "bschat.panel.submit",
        "elements": [
            { "type": "header", "text": "bschat.panel.settings.audio" },
            { "type": "toggle", "key": "audio.captureEnabled", "text": "bschat.opt.capture", "default": true },
            { "type": "dropdown", "key": "talkMode", "text": "bschat.opt.talkMode",
              "options": [
                { "value": "disabled", "text": "bschat.opt.talkMode.disabled" },
                { "value": "pushToTalk", "text": "bschat.opt.talkMode.ptt" }
              ] },
            { "type": "slider", "key": "audio.playbackVolume", "text": "bschat.opt.volume",
              "min": 0, "max": 100, "step": 5, "default": 80 },
            { "type": "input", "key": "input.pttKeyName", "text": "bschat.opt.pttKey", "placeholder": "V" },
            { "type": "divider" },
            { "type": "label", "text": "bschat.panel.settings.hint" }
        ]
    })");
    EXPECT_TRUE(definition.has_value());
    EXPECT_EQ(definition->schemaVersion, 1);
    EXPECT_EQ(definition->id, std::string("bschat.settings.client"));
    EXPECT_EQ(definition->titleKey, std::string("bschat.panel.settings.title"));
    EXPECT_EQ(definition->submitButtonKey, std::string("bschat.panel.submit"));
    EXPECT_EQ(definition->elements.size(), static_cast<std::size_t>(7));

    EXPECT_EQ(definition->elements[0].type, PanelElementType::Header);
    EXPECT_EQ(definition->elements[0].textKey, std::string("bschat.panel.settings.audio"));

    auto const& toggle = definition->elements[1];
    EXPECT_EQ(toggle.type, PanelElementType::Toggle);
    EXPECT_EQ(toggle.key, std::string("audio.captureEnabled"));
    EXPECT_TRUE(toggle.defaultBool);

    auto const& dropdown = definition->elements[2];
    EXPECT_EQ(dropdown.type, PanelElementType::Dropdown);
    EXPECT_EQ(dropdown.options.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(dropdown.options[0].value, std::string("disabled"));
    EXPECT_EQ(dropdown.options[1].textKey, std::string("bschat.opt.talkMode.ptt"));

    auto const& slider = definition->elements[3];
    EXPECT_EQ(slider.type, PanelElementType::Slider);
    EXPECT_NEAR(slider.min, 0.0, 1e-9);
    EXPECT_NEAR(slider.max, 100.0, 1e-9);
    EXPECT_NEAR(slider.step, 5.0, 1e-9);
    EXPECT_NEAR(slider.defaultNumber, 80.0, 1e-9);

    auto const& input = definition->elements[4];
    EXPECT_EQ(input.type, PanelElementType::Input);
    EXPECT_EQ(input.placeholder, std::string("V"));

    EXPECT_EQ(definition->elements[5].type, PanelElementType::Divider);
    EXPECT_EQ(definition->elements[6].type, PanelElementType::Label);
}

TEST(ui_panel_definition_drops_invalid_elements) {
    auto definition = parseOk(R"({
        "schemaVersion": 1,
        "id": "bschat.settings.client",
        "title": "bschat.panel.settings.title",
        "elements": [
            { "type": "toggle", "text": "bschat.opt.noKey" },
            { "type": "dropdown", "key": "empty", "text": "bschat.opt.empty", "options": [] },
            { "type": "slider", "key": "badRange", "text": "bschat.opt.bad", "min": 10, "max": 1 },
            { "type": "unknownFutureElement", "key": "future", "text": "bschat.opt.future" },
            { "type": "toggle", "key": "subtitle.enabled", "text": "bschat.opt.subtitle", "default": true }
        ]
    })");
    EXPECT_TRUE(definition.has_value());
    // 只有最后一条合法元素被保留：缺 key、空选项、区间反向、未知类型一律丢弃。
    EXPECT_EQ(definition->elements.size(), static_cast<std::size_t>(1));
    if (!definition->elements.empty()) {
        EXPECT_EQ(definition->elements[0].key, std::string("subtitle.enabled"));
    }
}

TEST(ui_panel_definition_rejects_bad_input) {
    std::string error;

    // 不支持的 schemaVersion：整体拒绝，避免按旧语义解析新格式
    auto newer = PanelDefinition::parse(
        R"({"schemaVersion": 99, "id": "x", "title": "t", "elements": []})",
        error
    );
    EXPECT_FALSE(newer.has_value());
    EXPECT_FALSE(error.empty());

    // 缺失必需字段
    EXPECT_FALSE(PanelDefinition::parse(R"({"schemaVersion": 1, "elements": []})", error).has_value());

    // JSON 语法错误
    EXPECT_FALSE(PanelDefinition::parse(R"({"schemaVersion": 1,)", error).has_value());

    // 空元素列表：定义本身无意义，拒绝
    EXPECT_FALSE(
        PanelDefinition::parse(R"({"schemaVersion": 1, "id": "x", "title": "t", "elements": []})", error)
            .has_value()
    );
}
