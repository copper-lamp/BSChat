#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "core/config/Config.h"
#include "core/protocol/Message.h"
#include "shared/ui/PanelRegistry.h"

class Player;

namespace vc::server::ui {

using ::vc::ui::PanelRegistry;

// 服务端面板中继：把客户端自建的表单 JSON 用原生表单投递给该玩家本人，再把结果原样送回。
// 服务端不解析 payload（零知识中继），只做长度与频率限制、会话校验。
// 另负责管理员面板：由服务端本地用 FormPanelBuilder 构建并下发给管理员本人。
//
// 线程模型：handleRequest 来自网络线程（只入队）；tick/openAdminPanel 只在游戏主线程调用。
class PanelRelay final {
public:
    using SendFn = std::function<void(protocol::PlayerId const& peerId, protocol::Message const& message)>;
    using PlayerResolver = std::function<Player*(protocol::PlayerId const& peerId)>;
    using SessionChecker = std::function<bool(protocol::PlayerId const& peerId)>;
    using Clock = std::function<int64_t()>;
    using LogSink = std::function<void(bool isError, std::string const& message)>;

    void initialize(
        std::filesystem::path const& panelsDir,
        SendFn send,
        PlayerResolver resolve,
        SessionChecker hasSession,
        Clock clock,
        LogSink log
    );
    void shutdown();

    // 网络线程：入队一个中继请求（仅做长度/类型校验，投递在主线程完成）。
    void handleRequest(protocol::PlayerId const& peerId, protocol::UiFormMessage const& message);

    // 主线程：处理排队的请求并把结果回传给请求者本人。
    void tick(int64_t nowMs);

    // 主线程：给管理员本人下发管理面板；提交后写回 config 并回调 onApplied 持久化。
    bool openAdminPanel(Player& player, config::ServerConfig& config, std::function<void()> onApplied);

    // 同一玩家两次中继请求的最小间隔。
    static constexpr int64_t kRequestThrottleMs = 1000;
    static constexpr std::size_t kMaxQueuedRequests = 16;

private:
    struct PendingRequest {
        protocol::PlayerId peerId{};
        uint32_t requestId = 0;
        std::string payload;
    };

    void deliver(PendingRequest request);
    void logInfo(std::string const& message) const;
    void logWarn(std::string const& message) const;

    PanelRegistry registry_;
    SendFn send_;
    PlayerResolver resolve_;
    SessionChecker hasSession_;
    Clock clock_;
    LogSink log_;
    bool enabled_ = false;

    std::mutex queueMutex_;
    std::vector<PendingRequest> incoming_;
    std::map<protocol::PlayerId, int64_t> lastRequestMs_; // 仅主线程访问
};

} // namespace vc::server::ui
