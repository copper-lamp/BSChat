#include "shared/transport/GamePacketTransport.h"

#include <chrono>
#include <system_error>
#include <utility>

#include "core/protocol/MessageCodec.h"
#include "mc/deps/core/utility/optional_ref.h"
#include "ll/api/network/packet/Packet.h"
#include "ll/api/service/Bedrock.h"
#include "mc/common/SubClientId.h"
#include "mc/deps/core/utility/BinaryStream.h"
#include "mc/deps/core/utility/ReadOnlyBinaryStream.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "shared/util/PlayerIdUtils.h"

// LeviLamina 26.10.14's public packet header declares this virtual API but its
// package import library does not export the fallback implementation. The
// concrete packet below overrides it; this definition only satisfies the base
// vtable emitted by clang-cl for PacketBase.
ll::network::PacketRuntimeId ll::network::Packet::getRuntimeId() const { return 0; }

namespace vc::shared {

namespace {

GamePacketTransport* g_transport = nullptr;

int64_t steadyClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool isRealtimeAudio(const protocol::Message& message) {
    return std::visit(
        [](const auto& m) {
            using M = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<M, protocol::AudioDataMessage> || std::is_same_v<M, protocol::MixStreamMessage>) return true;
            return false;
        },
        message
    );
}

} // namespace

class VoiceChatPacket : public ll::network::PacketBase<VoiceChatPacket> {
public:
    std::vector<uint8_t> payload;

    void write(::BinaryStream& bs) const override {
        // BinaryStream exposes no raw byte append on either platform variant, but
        // its backing string is writable, which is exactly the payload contract.
        if (!payload.empty()) bs.mBuffer.append(reinterpret_cast<char const*>(payload.data()), payload.size());
    }

    ::Bedrock::Result<void> read(::ReadOnlyBinaryStream& bs) override {
        auto const remaining = bs.mView.size() - bs.mReadPointer;
        if (remaining > 0) {
            payload.resize(remaining);
            auto res = bs.read(payload.data(), remaining);
            if (!res.has_value()) return nonstd::make_unexpected(::Bedrock::ErrorInfo{std::make_error_code(std::errc::io_error)});
        }
        return {};
    }
};

class VoiceChatPacketHandler : public ll::network::PacketHandlerBase<VoiceChatPacketHandler, VoiceChatPacket> {
public:
    void handlePacket(::NetworkIdentifier const& netId, ::NetEventCallback&, VoiceChatPacket const& packet) const {
        auto* transport = g_transport;
        if (!transport) return;
        protocol::PlayerId peerId = kServerPlayerId;
        if (transport->mode() == TransportMode::Server) {
            if (auto snh = ll::service::getServerNetworkHandler()) {
                if (auto* player = snh->_getServerPlayer(netId, ::SubClientId::PrimaryClient)) peerId = playerIdFromUuid(player->getUuid());
                else return;
            } else return;
        }
        transport->onPacketReceived(packet.payload, peerId);
    }
};

GamePacketTransport::GamePacketTransport(TransportMode mode, PlayerResolver resolver) : mode_(mode), resolver_(std::move(resolver)) { g_transport = this; }
GamePacketTransport::~GamePacketTransport() { if (g_transport == this) g_transport = nullptr; }
void GamePacketTransport::setMessageHandler(MessageHandler handler) { std::lock_guard lock(handlerMutex_); handler_ = std::move(handler); }
void GamePacketTransport::clearMessageHandler() { std::lock_guard lock(handlerMutex_); handler_ = {}; }

void GamePacketTransport::send(const protocol::PlayerId& peerId, const protocol::Message& message) {
    auto bytes = protocol::MessageCodec::pack(message, ++sendSeq_, steadyClockMs());
    if (bytes.empty()) return;
    VoiceChatPacket packet;
    packet.payload = std::move(bytes);
    if (isRealtimeAudio(message)) {
        packet.mPriority = PacketPriority::ImmediatePriority;
        packet.mReliability = NetworkPeer::Reliability::Unreliable;
        packet.mCompressible = Compressibility::Incompressible;
    } else {
        packet.mPriority = PacketPriority::HighPriority;
        packet.mReliability = NetworkPeer::Reliability::ReliableOrdered;
    }
    if (mode_ == TransportMode::Client) { packet.sendToServer(); return; }
    auto* player = resolver_ ? resolver_(peerId) : nullptr;
    if (!player) return;
    packet.sendToClient(player->getNetworkIdentifier(), ::SubClientId::PrimaryClient);
}

void GamePacketTransport::onPacketReceived(const std::vector<uint8_t>& payload, const protocol::PlayerId& peerId) {
    auto unpacked = protocol::MessageCodec::unpack(payload);
    if (!unpacked) return;
    dispatch(peerId, std::move(unpacked->second));
}

void GamePacketTransport::dispatch(const protocol::PlayerId& peerId, const protocol::Message& message) {
    MessageHandler handler;
    {
        std::lock_guard lock(handlerMutex_);
        handler = handler_;
    }
    // 不持锁调用用户回调：回调可能 stop/clear handler 或重入 transport。
    if (handler) handler(peerId, message);
}

} // namespace vc::shared
