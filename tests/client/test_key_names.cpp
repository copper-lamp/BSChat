#include "Harness.h"

#include <cstdint>
#include <string>

#include "client/input/KeyNames.h"

using namespace vc::client::input;

TEST(key_names_letters_and_digits) {
    EXPECT_EQ(virtualKeyFromName("V").value_or(0), 0x56u);
    EXPECT_EQ(virtualKeyFromName("v").value_or(0), 0x56u); // 大小写不敏感
    EXPECT_EQ(virtualKeyFromName("a").value_or(0), 0x41u);
    EXPECT_EQ(virtualKeyFromName("0").value_or(0), 0x30u);
    EXPECT_EQ(virtualKeyFromName("9").value_or(0), 0x39u);
    EXPECT_EQ(virtualKeyFromName("  j  ").value_or(0), 0x4Au); // 允许两端空白
}

TEST(key_names_function_and_named_keys) {
    EXPECT_EQ(virtualKeyFromName("F1").value_or(0), 0x70u);
    EXPECT_EQ(virtualKeyFromName("f12").value_or(0), 0x7Bu);
    EXPECT_EQ(virtualKeyFromName("SPACE").value_or(0), 0x20u);
    EXPECT_EQ(virtualKeyFromName("escape").value_or(0), 0x1Bu);
    EXPECT_EQ(virtualKeyFromName("ESC").value_or(0), 0x1Bu);
    EXPECT_EQ(virtualKeyFromName("control").value_or(0), 0x11u);
    EXPECT_EQ(virtualKeyFromName("up").value_or(0), 0x26u);
}

TEST(key_names_reject_unknown) {
    EXPECT_FALSE(virtualKeyFromName("").has_value());
    EXPECT_FALSE(virtualKeyFromName("   ").has_value());
    EXPECT_FALSE(virtualKeyFromName("NOTAKEY").has_value());
    EXPECT_FALSE(virtualKeyFromName("VV").has_value());
    EXPECT_FALSE(virtualKeyFromName("F13").has_value());
    EXPECT_FALSE(virtualKeyFromName("F0").has_value());
}

TEST(key_names_reverse_lookup) {
    EXPECT_EQ(nameFromVirtualKey(0x56u), std::string("V"));
    EXPECT_EQ(nameFromVirtualKey(0x4Au), std::string("J"));
    EXPECT_EQ(nameFromVirtualKey(0x20u), std::string("SPACE"));
    EXPECT_EQ(nameFromVirtualKey(0x70u), std::string("F1"));
    // 未收录的键返回空串，调用方据此只显示 placeholder
    EXPECT_EQ(nameFromVirtualKey(0xFFu), std::string());
    EXPECT_EQ(nameFromVirtualKey(0u), std::string());
}

TEST(key_names_roundtrip) {
    for (uint32_t vk = 0x41u; vk <= 0x5Au; ++vk) {
        auto const name = nameFromVirtualKey(vk);
        EXPECT_FALSE(name.empty());
        EXPECT_EQ(virtualKeyFromName(name).value_or(0), vk);
    }
    for (uint32_t vk = 0x70u; vk <= 0x7Bu; ++vk) {
        auto const name = nameFromVirtualKey(vk);
        EXPECT_FALSE(name.empty());
        EXPECT_EQ(virtualKeyFromName(name).value_or(0), vk);
    }
}
