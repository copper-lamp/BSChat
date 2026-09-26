#include "client/ui/PanelRelay.h"

#include <string>
#include <utility>

#include "shared/ui/FormPanelBuilder.h"
#include "shared/ui/FormPlan.h"

namespace bsc::client::ui {

using ::bsc::ui::FormPanelBuilder;
using ::bsc::ui::parseFormResponse;

void PanelRelay::initialize(
    std::filesystem::path const& panelsDir,
    SendFn send,
    ConfigProvider config,
    ApplyFn apply,
    Clock clock,
    LogSink log
) {
    send_ = std::move(send);
    config_ = std::move(config);
    apply_ = std::move(apply);
    clock_ = std::move(clock);
    log_ = std::move(log);
    enabled_ = true;

    std::string error;
    registry_.load(panelsDir, &error);
    if (!error.empty()) logWarn("panel: " + error);
    if (registry_.panels().empty()) {
        logWarn(
            "panel: no panel definitions loaded from " + panelsDir.string()
            + "; the voice settings panel is unavailable"
        );
    } else {
        logInfo("panel: loaded " + std::to_string(registry_.size()) + " panel definition(s)");
    }
}

void PanelRelay::shutdown() {
    enabled_ = false;
    pendingRequestId_ = 0;
    pendingPanelId_.clear();
    registry_.clear();
    std::lock_guard lock(queueMutex_);
    incoming_.clear();
}

void PanelRelay::requestPanel(std::string const& panelId) {
    if (!enabled_) {
        logWarn("panel: relay is not initialized");
        return;
    }
    if (!send_) {
        logWarn("panel: transport is unavailable");
        return;
    }
    int64_t const now = clock_ ? clock_() : 0;
    if (now - lastRequestMs_ < kRequestThrottleMs) {
        logWarn("panel: request throttled; wait a moment before opening the panel again");
        return;
    }

    auto const* definition = registry_.find(panelId);
    if (!definition) {
        logWarn("panel: definition not found: " + panelId);
        return;
    }

    auto form = FormPanelBuilder::build(
        *definition,
        [this](std::string const& key) -> PanelValue {
            return config_ ? ConfigBinding::read(config_(), key) : PanelValue{};
        },
        FormPanelBuilder::i18nResolver()
    );
    if (!form) {
        logWarn("panel: failed to build the form");
        return;
    }

    std::string formData = form->getFormData();
    if (formData.empty()) {
        logWarn("panel: form data is empty");
        return;
    }
    if (formData.size() > protocol::kUiFormPayloadMaxBytes) {
        logWarn("panel: form payload exceeds the relay limit, request dropped");
        return;
    }

    if (++nextRequestId_ == 0) nextRequestId_ = 1;
    uint32_t const requestId = nextRequestId_;

    pendingRequestId_ = requestId;
    pendingPanelId_ = panelId;
    pendingSinceMs_ = now;
    lastRequestMs_ = now;

    protocol::UiFormMessage request;
    request.kind = protocol::UiFormKind::Request;
    request.requestId = requestId;
    request.cancelReason = -1;
    request.payload = std::move(formData);
    send_(request);
    logInfo("panel: requested " + panelId + " (request id " + std::to_string(requestId) + ")");
}

void PanelRelay::handleMessage(protocol::UiFormMessage const& message) {
    if (!enabled_) return;
    std::lock_guard lock(queueMutex_);
    if (incoming_.size() >= kMaxQueuedMessages) return; // 防止异常对端刷爆队列
    incoming_.push_back(message);
}

void PanelRelay::tick(int64_t nowMs) {
    if (!enabled_) return;

    std::vector<protocol::UiFormMessage> incoming;
    {
        std::lock_guard lock(queueMutex_);
        incoming.swap(incoming_);
    }
    for (auto const& message : incoming) {
        if (message.kind == protocol::UiFormKind::Response) handleResponse(message);
    }

    if (pendingRequestId_ != 0 && nowMs - pendingSinceMs_ > kRequestTimeoutMs) {
        logWarn("panel: request timed out (id " + std::to_string(pendingRequestId_) + ")");
        pendingRequestId_ = 0;
        pendingPanelId_.clear();
    }
}

void PanelRelay::handleResponse(protocol::UiFormMessage const& message) {
    if (message.requestId == 0 || message.requestId != pendingRequestId_) {
        logWarn("panel: ignoring response for unknown request id " + std::to_string(message.requestId));
        return;
    }

    std::string const panelId = pendingPanelId_;
    pendingRequestId_ = 0;
    pendingPanelId_.clear();

    if (message.cancelReason >= 0) {
        logInfo("panel: closed by player without changes (reason " + std::to_string(message.cancelReason) + ")");
        return;
    }

    auto const* definition = registry_.find(panelId);
    if (!definition) {
        logWarn("panel: definition vanished: " + panelId);
        return;
    }

    std::string error;
    PanelValues const values = parseFormResponse(*definition, message.payload, &error);
    if (!error.empty()) {
        logWarn("panel: response rejected (" + error + "), config unchanged");
        return;
    }
    if (values.empty()) {
        logWarn("panel: response contained no usable values, config unchanged");
        return;
    }

    config::ClientConfig config = config_ ? config_() : config::ClientConfig{};
    std::size_t const applied = ConfigBinding::apply(config, values);
    if (applied == 0) {
        logWarn("panel: no valid values were applied, config unchanged");
        return;
    }
    if (apply_) apply_(config);
    logInfo("panel: applied " + std::to_string(applied) + " value(s) from " + panelId);
}

void PanelRelay::logInfo(std::string const& message) const {
    if (log_) log_(false, message);
}

void PanelRelay::logWarn(std::string const& message) const {
    if (log_) log_(true, message);
}

} // namespace bsc::client::ui
