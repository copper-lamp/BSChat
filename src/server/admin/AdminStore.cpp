#include "server/admin/AdminStore.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace vc::server::admin {
namespace {
using json = nlohmann::json;
std::string encode(PlayerId const& id) { std::ostringstream out; for (auto b : id) out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(b); return out.str(); }
bool decode(std::string const& s, PlayerId& id) {
    if (s.size() != 32) return false;
    for (size_t i = 0; i < id.size(); ++i) { unsigned v = 0; std::istringstream in(s.substr(i * 2, 2)); in >> std::hex >> v; if (in.fail()) return false; id[i] = static_cast<std::uint8_t>(v); }
    return true;
}
void fail(std::string* e, std::string const& text) { if (e) *e = text; }
}

AdminStore::AdminStore(std::filesystem::path path) : path_(std::move(path)) {}

bool AdminStore::save(AdminPolicy const& policy, std::string* error) const {
    try {
        json j{{"version", 1}, {"whitelistEnabled", policy.whitelistEnabled()}, {"whitelist", json::array()}, {"blacklist", json::array()}, {"gains", json::object()}};
        for (auto const& id : policy.whitelist()) j["whitelist"].push_back(encode(id));
        for (auto const& id : policy.blacklist()) j["blacklist"].push_back(encode(id));
        for (auto const& [id, gain] : policy.gains()) j["gains"][encode(id)] = gain;
        auto temp = path_; temp += ".tmp";
        { std::ofstream out(temp, std::ios::trunc); if (!out) { fail(error, "cannot open temporary admin file"); return false; } out << j.dump(4) << '\n'; if (!out) { fail(error, "cannot write temporary admin file"); return false; } }
        std::error_code ec; std::filesystem::rename(temp, path_, ec);
        if (ec) { std::filesystem::remove(path_, ec); std::filesystem::rename(temp, path_, ec); }
        if (ec) { fail(error, "cannot replace admin file: " + ec.message()); return false; }
        return true;
    } catch (std::exception const& e) { fail(error, e.what()); return false; }
}

bool AdminStore::load(AdminPolicy& policy, std::string* error) const {
    try {
        std::ifstream in(path_); if (!in) { fail(error, "admin file does not exist or cannot be opened"); return false; }
        json j; in >> j; if (!j.is_object()) { fail(error, "admin file root is not an object"); return false; }
        AdminPolicy next; next.setWhitelistEnabled(j.value("whitelistEnabled", false));
        for (auto const& item : j.value("whitelist", json::array())) { if (!item.is_string()) continue; PlayerId id{}; if (decode(item.get<std::string>(), id)) next.addWhitelist(id); }
        for (auto const& item : j.value("blacklist", json::array())) { if (!item.is_string()) continue; PlayerId id{}; if (decode(item.get<std::string>(), id)) next.addBlacklist(id); }
        auto gains = j.value("gains", json::object()); if (gains.is_object()) for (auto it = gains.begin(); it != gains.end(); ++it) { PlayerId id{}; if (decode(it.key(), id) && it.value().is_number()) next.setGain(id, it.value().get<float>()); }
        policy.clear(); policy.setWhitelistEnabled(next.whitelistEnabled()); for (auto id : next.whitelist()) policy.addWhitelist(id); for (auto id : next.blacklist()) policy.addBlacklist(id); for (auto [id, gain] : next.gains()) policy.setGain(id, gain);
        return true;
    } catch (std::exception const& e) { fail(error, e.what()); return false; }
}
} // namespace vc::server::admin
