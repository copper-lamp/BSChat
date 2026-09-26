#include "server/ui/PanelRelay.h"

#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "ll/api/form/CustomForm.h"
#include "ll/api/form/FormBase.h"
#include "mc/world/actor/player/Player.h"
#include "shared/ui/FormPanelBuilder.h"
#include "shared/ui/FormPlan.h"

namespace bsc::server::ui {

using ::bsc::ui::FormPanelBuilder;
using ::bsc::ui::PanelValue;
using ::bsc::ui::PanelValues;

void PanelRelay::initialize(
    std::filesystem::path const& panelsDir,
    SendFn send,
    PlayerResolver resolve,
    SessionChecker hasSession,
    Clock clock,
    LogSink log
) {
    send_ = std::move(send);
    resolve_ = std::move(resolve);
    hasSession_ = std::move(hasSession);
    clock_ = std::move(clock);
    log_ = std::move(log);
    enabled_ = true;

    std::string error;
    registry_.load(panelsDir, &error);
    if (!error.empty()) logWarn("panel: " + error);
    if (registry_.panels().empty()) {
        logWarn(
            "panel: no panel definitions loaded from " + panelsDir.string()
            + "; the relay and admin panel are unavailable"
        );
    } else {
        logInfo("panel: loaded " + std::to_string(registry_.size()) + " panel definition(s)");
    }
}

void PanelRelay::shutdown() {
    enabled_ = false;
    registry_.clear();
    lastRequestMs_.clear();
    std::lock_guard lock(queueMutex_);
    incoming_.clear();
}

void PanelRelay::handleRequest(protocol::PlayerId const& peerId, protocol::UiFormMessage const& message) {
    if (!enabled_) return;
    if (message.kind != protocol::UiFormKind::Request) return; // 服务端只受理请求，不接受客户端伪造的响应
    if (message.payload.empty()) return;
    if (message.payload.size() > protocol::kUiFormPayloadMaxBytes) {
        logWarn("relay: request payload exceeds the size limit, dropped");
        return;
    }

    std::lock_guard lock(queueMutex_);
    if (incoming_.size() >= kMaxQueuedRequests) return; // 防止异常对端刷爆队列
    incoming_.push_back(PendingRequest{peerId, message.requestId, message.payload});
}

void PanelRelay::tick(int64_t nowMs) {
    if (!enabled_) return;

    std::vector<PendingRequest> incoming;
    {
        std::lock_guard lock(queueMutex_);
        incoming.swap(incoming_);
    }

    for (auto& request : incoming) {
        if (hasSession_ && !hasSession_(request.peerId)) {
            logWarn("relay: request from a player without an established session, dropped");
            continue;
        }
        auto it = lastRequestMs_.find(request.peerId);
        if (it != lastRequestMs_.end() && nowMs - it->second < kRequestThrottleMs) {
            logWarn("relay: request throttled");
            continue;
        }
        lastRequestMs_[request.peerId] = nowMs;
        deliver(std::move(request));
    }
}

void PanelRelay::deliver(PendingRequest request) {
    Player* player = resolve_ ? resolve_(request.peerId) : nullptr;
    if (!player) {
        logWarn("relay: cannot resolve an online player for panel delivery, dropped");
        return;
    }

    protocol::PlayerId const peerId = request.peerId;
    uint32_t const requestId = request.requestId;
    bool const sent = ll::form::Form::sendRawTo(
        *player,
        request.payload,
        [this, peerId, requestId](Player&, std::optional<std::string> result, ll::form::FormCancelReason reason) {
            if (!send_) return;
            protocol::UiFormMessage response;
            response.kind = protocol::UiFormKind::Response;
            response.requestId = requestId;
            response.cancelReason = reason.has_value() ? static_cast<int32_t>(*reason) : -1;
            if (result) response.payload = std::move(*result);
            send_(peerId, response);
        }
    );
    if (!sent) logWarn("relay: failed to deliver the form to the player");
}

bool PanelRelay::openAdminPanel(Player& player, config::ServerConfig& config, std::function<void()> onApplied) {
    auto const* definition = registry_.find("bschat.admin");
    if (!definition) {
        logWarn("admin: panel definition 'bschat.admin' is not loaded");
        return false;
    }

    auto form = FormPanelBuilder::build(
        *definition,
        [&config](std::string const& key) -> PanelValue {
            if (key == "voiceEnabled") return static_cast<uint64_t>(config.voiceEnabled ? 1 : 0);
            return std::monostate{};
        },
        FormPanelBuilder::i18nResolver()
    );
    if (!form) {
        logWarn("admin: failed to build the admin panel");
        return false;
    }

    form->sendTo(
        player,
        [this, &config, onApplied = std::move(onApplied)](
            Player&,
            ll::form::CustomFormResult const& result,
            ll::form::FormCancelReason
        ) {
            if (!result) {
                logInfo("admin: panel closed without changes");
                return;
            }
            PanelValues const values = FormPanelBuilder::toPanelValues(result);
            auto it = values.find("voiceEnabled");
            if (it == values.end()) {
                logWarn("admin: response did not contain voiceEnabled");
                return;
            }

            bool enabled = false;
            if (auto const* flag = std::get_if<uint64_t>(&it->second)) enabled = *flag != 0;
            else if (auto const* number = std::get_if<double>(&it->second)) enabled = *number != 0.0;
            else {
                logWarn("admin: voiceEnabled has an unexpected type");
                return;
            }

            config.voiceEnabled = enabled;
            if (onApplied) onApplied();
            logInfo(std::string("admin: server voice chat ") + (enabled ? "enabled" : "disabled"));
        }
    );
    return true;
}

void PanelRelay::logInfo(std::string const& message) const {
    if (log_) log_(false, message);
}

void PanelRelay::logWarn(std::string const& message) const {
    if (log_) log_(true, message);
}

} // namespace bsc::server::ui
