#pragma once

#include <filesystem>
#include <string>

#include "server/admin/AdminPolicy.h"

namespace bsc::server::admin {

// JSON persistence adapter. It never exposes the policy's internal locks.
class AdminStore {
public:
    explicit AdminStore(std::filesystem::path path);

    const std::filesystem::path& path() const noexcept { return path_; }
    bool load(AdminPolicy& policy, std::string* error = nullptr) const;
    bool save(AdminPolicy const& policy, std::string* error = nullptr) const;

private:
    std::filesystem::path path_;
};

} // namespace bsc::server::admin
