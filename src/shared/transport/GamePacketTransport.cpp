#include "shared/transport/GamePacketTransport.h"

#include <chrono>
#include <system_error>
#include <utility>

#include "core/protocol/MessageCodec.h"
// 26.40.1 的 PacketRegistrar.h 未自带 optional_ref，需在 Packet.h 之前引入
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

namespace vc::shared {

// ---- 自定义游戏包 ----

namespace {

// 当前传输实例：包到达时由 packet handler 转发给它的实例方法。
// 单例语义（每模组进程一份传输），装配阶段赋值、卸载阶段清空。
GamePacketTransport* g_transport = nullptr;

int64_t steadyClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 实时语音帧走不可靠传输（容忍丢失，靠抖动缓冲排序）；其余消息走可靠有序。
bool isRealtimeAudio(const protocol::Message& message) {
    return std::visit(
        [](const auto& m) {
            using M = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<M, protocol::AudioDataMessage> || std::is_same_v<M, protocol::MixStreamMessage>) {
                return true;
            }
            return false;
        },
        message
    );
}

} // namespace

// 包体 = 协议信封字节。名字固定为 vc::shared::VoiceChatPacket（反射取类名做包 ID），
// PacketBase / PacketHandlerBase 的静态锚点会在 DLL 加载期自动向 PacketRegistrar 注册，
// 因此双端（服务端/客户端）使用同一组包 ID，无需手工注册。
class VoiceChatPacket : public ll::network::PacketBase<VoiceChatPacket> {
public:
    std::vector<uint8_t> payload;

    void write(::BinaryStream& bs) const override {
        if (!payload.empty()) {
            bs.write(reinterpret_cast<char const*>(payload.data()), payload.size());
        }
    }

    ::Bedrock::Result<void> read(::ReadOnlyBinaryStream& bs) override {
        auto const remaining = bs.mView.size() - bs.mReadPointer;
        if (remaining > 0) {
            payload.resize(remaining);
            auto res = bs.read(payload.data(), remaining);
            if (!res.has_value()) {
                return nonstd::make_unexpected(
                    ::Bedrock::ErrorInfo{std::make_error_code(std::errc::io_error)}
                );
            }
        }
        return {};
    }
};

class VoiceChatPacketHandler : public ll::network::PacketHandlerBase<VoiceChatPacketHandler, VoiceChatPacket> {
public:
    void handlePacket(
        ::NetworkIdentifier const&   netId,
        ::NetEventCallback&          /*callback*/,
        VoiceChatPacket const&       packet
    ) const {
        auto* transport = g_transport;
        if (!transport) return;

        protocol::PlayerId peerId = kServerPlayerId;
        if (transport->mode() == TransportMode::Server) {
            // 服务端：网络标识 → 玩家 → UUID → 协议 PlayerId；未关联玩家的包忽略
            if (auto snh = ll::service::getServerNetworkHandler()) {
                if (auto* player = snh->_getServerPlayer(netId, ::SubClientId::PrimaryClient)) {
                    peerId = playerIdFromUuid(player->getUuid());
                } else {
                    return;
                }
            } else {
                return;
            }
        }
        transport->onPacketReceived(packet.payload, peerId);
    }
};

// ---- GamePacketTransport ----

GamePacketTransport::GamePacketTransport(TransportMode mode, PlayerResolver resolver)
: mode_(mode), resolver_(std::move(resolver)) {
    g_transport = this;
}

void GamePacketTransport::setMessageHandler(MessageHandler handler) {
    std::lock_guard lock(handlerMutex_);
    handler_ = std::move(handler);
}

void GamePacketTransport::send(const protocol::PlayerId& peerId, const protocol::Message& message) {
    auto bytes = protocol::MessageCodec::pack(message, ++sendSeq_, steadyClockMs());
    if (bytes.empty()) return;

    VoiceChatPacket packet;
    packet.payload = std::move(bytes);
    if (isRealtimeAudio(message)) {
        packet.mPriority     = PacketPriority::ImmediatePriority;
        packet.mReliability  = NetworkPeer::Reliability::Unreliable;
        packet.mCompressible = Compressibility::Incompressible;
    } else {
        packet.mPriority     = PacketPriority::HighPriority;
        packet.mReliability  = NetworkPeer::Reliability::ReliableOrdered;
    }

    if (mode_ == TransportMode::Client) {
        packet.sendToServer();
        return;
    }
    auto* player = resolver_ ? resolver_(peerId) : nullptr;
    if (!player) return; // 玩家已离线或未知 → 静默丢弃
    packet.sendTo(*player);
}

void GamePacketTransport::onPacketReceived(const std::vector<uint8_t>& payload, const protocol::PlayerId& peerId) {
    auto unpacked = protocol::MessageCodec::unpack(payload);
    if (!unpacked) return; // 未知类型 / 数据损坏 → 按协议忽略
    dispatch(peerId, std::move(unpacked->second));
}

void GamePacketTransport::dispatch(const protocol::PlayerId& peerId, const protocol::Message& message) {
    std::lock_guard lock(handlerMutex_);
    if (handler_) handler_(peerId, message);
}

} // namespace vc::shared
