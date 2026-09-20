#include "Harness.h"
#include "server/admin/AdminPolicy.h"
#include "server/admin/AdminStore.h"

#include <filesystem>

using namespace vc::server::admin;

namespace { PlayerId id(unsigned char v) { PlayerId x{}; x[0] = v; return x; } }

TEST(admin_policy_admission_and_gain) {
    AdminPolicy p; auto a = id(1); auto b = id(2);
    EXPECT_TRUE(p.isAllowed(a)); p.setWhitelistEnabled(true); p.addWhitelist(a); p.addBlacklist(a);
    EXPECT_TRUE(p.isAllowed(b) == false); EXPECT_TRUE(p.isAllowed(a) == false); p.removeBlacklist(a); EXPECT_TRUE(p.isAllowed(a));
    p.setGain(a, 9.0f); EXPECT_NEAR(p.gain(a), 4.0f, 0.001); p.removeGain(a); EXPECT_NEAR(p.gain(a), 1.0f, 0.001);
}

TEST(admin_store_round_trip) {
    auto path = std::filesystem::temp_directory_path() / "betterlanguagechat-admin-test.json";
    std::error_code ec; std::filesystem::remove(path, ec);
    AdminPolicy source; auto a = id(3); source.setWhitelistEnabled(true); source.addWhitelist(a); source.setGain(a, 0.5f);
    AdminStore store(path); std::string error; EXPECT_TRUE(store.save(source, &error));
    AdminPolicy loaded; EXPECT_TRUE(store.load(loaded, &error)); EXPECT_TRUE(loaded.whitelistEnabled()); EXPECT_TRUE(loaded.isWhitelisted(a)); EXPECT_NEAR(loaded.gain(a), 0.5f, 0.001);
    std::filesystem::remove(path, ec);
}
