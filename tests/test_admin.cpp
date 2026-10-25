#include "Harness.h"
#include "server/admin/AdminPolicy.h"
#include "server/admin/AdminStore.h"

#include <filesystem>

using namespace bsc::server::admin;

namespace { PlayerId id(unsigned char v) { PlayerId x{}; x[0] = v; return x; } }

TEST(admin_policy_admission_and_gain) {
    AdminPolicy p; auto a = id(1); auto b = id(2);
    EXPECT_TRUE(p.isAllowed(a)); p.setWhitelistEnabled(true); p.addWhitelist(a); p.addBlacklist(a);
    EXPECT_TRUE(p.isAllowed(b) == false); EXPECT_TRUE(p.isAllowed(a) == false); p.removeBlacklist(a); EXPECT_TRUE(p.isAllowed(a));
    p.setGain(a, 9.0f); EXPECT_NEAR(p.gain(a), 4.0f, 0.001); p.removeGain(a); EXPECT_NEAR(p.gain(a), 1.0f, 0.001);
}

TEST(admin_store_round_trip) {
    // 与 WavBytes::writeTempWav 一致：受限运行环境下系统 temp 目录可能不可写。
    auto path = std::filesystem::current_path() / "bschat-admin-test.json";
    std::error_code ec; std::filesystem::remove(path, ec);
    AdminPolicy source; auto a = id(3); source.setWhitelistEnabled(true); source.addWhitelist(a); source.setGain(a, 0.5f);
    AdminStore store(path); std::string error; EXPECT_TRUE(store.save(source, &error));
    if (!error.empty()) std::fprintf(stderr, "    admin save error: %s\n", error.c_str());
    AdminPolicy loaded; error.clear(); EXPECT_TRUE(store.load(loaded, &error));
    if (!error.empty()) std::fprintf(stderr, "    admin load error: %s\n", error.c_str()); EXPECT_TRUE(loaded.whitelistEnabled()); EXPECT_TRUE(loaded.isWhitelisted(a)); EXPECT_NEAR(loaded.gain(a), 0.5f, 0.001);
    std::filesystem::remove(path, ec);
}
