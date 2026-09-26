#include "Harness.h"

#include "core/config/Config.h"

using namespace bsc::config;

TEST(server_config_round_trips_advanced_limits) {
    ServerConfig input;
    input.maxSessions = 32;
    input.maxPending = 77;
    input.spatialMode = "radius";
    input.spatialRadius = 18.5f;

    const auto output = serverConfigToJson(input);
    const auto parsed = serverConfigFromJson(output);
    EXPECT_EQ(parsed.maxSessions, 32u);
    EXPECT_EQ(parsed.maxPending, 77u);
    EXPECT_EQ(parsed.spatialMode, "radius");
    EXPECT_NEAR(parsed.spatialRadius, 18.5f, 0.001f);
}

TEST(server_config_invalid_json_uses_defaults) {
    const auto parsed = serverConfigFromJson("not-json");
    EXPECT_EQ(parsed.maxSessions, 128u);
    EXPECT_EQ(parsed.maxPending, 1024u);
    EXPECT_EQ(parsed.spatialMode, "global");
}

TEST(server_config_missing_advanced_fields_preserves_defaults) {
    const auto parsed = serverConfigFromJson("{\"voiceEnabled\":false}");
    EXPECT_FALSE(parsed.voiceEnabled);
    EXPECT_EQ(parsed.maxSessions, 128u);
    EXPECT_EQ(parsed.maxPending, 1024u);
}
