#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "client/ui/ConfigBinding.h"
#include "core/config/Config.h"
#include "core/protocol/Message.h"
#include "shared/ui/PanelRegistry.h"

namespace vc::client::ui {

using ::vc::ui::PanelRegistry;

// 客户端面板中继：U→S 发送表单请求，S→U 收到结果后写回 ClientConfig。
// 表单只能由服务端下发（Bedrock 表单是网络包），因此客户端把自建的表单 JSON 交给服务端投递。
//
// 线程模型：requestPanel/tick 只在游戏主线程调用；handleMessage 来自网络线程，只入队。
class PanelRelay final {
public:
    using SendFn = std::function<void(protocol::Message const& message)>;
    using ConfigProvider = std::function<config::ClientConfig const&()>;
    using ApplyFn = std::function<void(config::ClientConfig const& config)>;
    using Clock = std::function<int64_t()>;
    using LogSink = std::function<void(bool isError, std::string const& message)>;

    PanelRelay() = default;

    void initialize(
        std::filesystem::path const& panelsDir,
        SendFn send,
        ConfigProvider config,
        ApplyFn apply,
        Clock clock,
        LogSink log
    );
    void shutdown();

    // 主线程：请求打开某个面板（构建表单 JSON 经中继下发）。受节流限制。
    void requestPanel(std::string const& panelId);

    // 网络线程：仅入队，主线程 tick 里处理。
    void handleMessage(protocol::UiFormMessage const& message);

    // 主线程：处理收到的响应与超时清理。
    void tick(int64_t nowMs);

    bool enabled() const { return enabled_; }
    bool available() const { return !registry_.panels().empty(); }
    std::size_t panelCount() const { return registry_.size(); }

    // 发出后等待响应的上限；超时后丢弃挂起状态，避免永久等待。
    static constexpr int64_t kRequestTimeoutMs = 30000;
    // 两次请求的最小间隔，避免刷屏。
    static constexpr int64_t kRequestThrottleMs = 1000;
    static constexpr std::size_t kMaxQueuedMessages = 8;

private:
    void handleResponse(protocol::UiFormMessage const& message);
    void logInfo(std::string const& message) const;
    void logWarn(std::string const& message) const;

    PanelRegistry registry_;
    SendFn send_;
    ConfigProvider config_;
    ApplyFn apply_;
    Clock clock_;
    LogSink log_;
    bool enabled_ = false;

    uint32_t nextRequestId_ = 0;
    uint32_t pendingRequestId_ = 0;
    std::string pendingPanelId_;
    int64_t pendingSinceMs_ = 0;
    int64_t lastRequestMs_ = -kRequestThrottleMs; // 允许首次请求立即发送

    std::mutex queueMutex_;
    std::vector<protocol::UiFormMessage> incoming_;
};

} // namespace vc::client::ui
