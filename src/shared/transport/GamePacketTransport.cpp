#include "shared/transport/GamePacketTransport.h"

#include <chrono>
#include <system_error>
#include <utility>

#include "core/protocol/MessageCodec.h"
#include "mc/deps/core/utility/optional_ref.h"
#include "ll/api/network/packet/Packet.h"
#include "ll/api/network/packet/PacketRegistrar.h"
#include "ll/api/reflection/TypeName.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/utils/HashUtils.h"
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
// vtable emitted by clang-cl for PacketBase. 语义与 SDK 实现保持一致
// （SDK: doHash(getName())），返回 0 会让未覆写该函数的包拿到非法 runtimeId。
ll::network::PacketRuntimeId ll::network::Packet::getRuntimeId() const {
    return ll::hash_utils::doHash(getName());
}

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
                if (auto* player = snh->_getServerPlayer(netId, ::SubClientId::PrimaryClient)) {
                    peerId = playerIdFromUuid(player->getUuid());
                    // 首个包即登记回发目标：PlayerJoinEvent 触发晚于客户端首个 Hello。
                    transport->rememberPlayer(peerId, player);
                } else return;
            } else return;
        }
        transport->onPacketReceived(packet.payload, peerId);
    }
};

namespace {

// 自定义包与处理器的显式注册。
// 不能依赖 PacketBase / PacketHandlerBase 的 `inline static sRegistered` 自注册：
// clang-cl 不会为类模板的静态数据成员实例化定义，注册因此从未发生（产物导入表里只有
// sendToServer/sendToClient，没有 registerPacket/registerHandler）。后果是客户端发出的
// RuntimePacket 在服务端 PacketRegistrar::createPacket 中找不到工厂，BDS 反序列化失败并
// 直接断开客户端，表现为进服后立刻掉线。此处显式注册，保证双端运行时 ID 与处理器一致。
void registerVoiceChatPacket() {
    static bool const registered = [] {
        auto&      registrar = ll::network::PacketRegistrar::getInstance();
        auto const name      = ll::reflection::type_unprefix_name_v<VoiceChatPacket>;
        auto const id        = ll::hash_utils::doHash(name);
        registrar.registerPacket(name, id, []() -> std::unique_ptr<ll::network::Packet> {
            return std::make_unique<VoiceChatPacket>();
        });
        static VoiceChatPacketHandler handler; // 注册表只存引用，处理器需静态存储期
        registrar.registerHandler(name, id, handler);
        return true;
    }();
    (void)registered;
}

} // namespace

GamePacketTransport::GamePacketTransport(TransportMode mode, PlayerResolver resolver) : mode_(mode), resolver_(std::move(resolver)) {
    // 先注册包类型，再对外暴露 transport，避免收到未注册的 runtimeId。
    registerVoiceChatPacket();
    g_transport = this;
}
GamePacketTransport::~GamePacketTransport() { if (g_transport == this) g_transport = nullptr; }
void GamePacketTransport::setMessageHandler(MessageHandler handler) { std::lock_guard lock(handlerMutex_); handler_ = std::move(handler); }
void GamePacketTransport::clearMessageHandler() { std::lock_guard lock(handlerMutex_); handler_ = {}; }
void GamePacketTransport::setLogSink(LogSink sink) { logSink_ = std::move(sink); }

void GamePacketTransport::rememberPlayer(const protocol::PlayerId& peerId, Player* player) {
    if (mode_ != TransportMode::Server || !player) return;
    std::lock_guard lock(playersMutex_);
    peerPlayers_.insert_or_assign(peerId, player);
}

void GamePacketTransport::forgetPlayer(const protocol::PlayerId& peerId) {
    std::lock_guard lock(playersMutex_);
    peerPlayers_.erase(peerId);
}

void GamePacketTransport::clearPlayers() {
    std::lock_guard lock(playersMutex_);
    peerPlayers_.clear();
}

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
    if (player) {
        packet.sendToClient(player->getNetworkIdentifier(), ::SubClientId::PrimaryClient);
        return;
    }
    // Player* 尚未建立（PlayerJoinEvent 之前）时回退到收包时登记的 Player*，避免静默丢包。
    Player* cached = nullptr;
    {
        std::lock_guard lock(playersMutex_);
        if (auto it = peerPlayers_.find(peerId); it != peerPlayers_.end()) cached = it->second;
    }
    if (cached) {
        packet.sendToClient(cached->getNetworkIdentifier(), ::SubClientId::PrimaryClient);
        return;
    }
    if (logSink_) {
        logSink_(true, "transport: downlink dropped, peer has no online player nor recorded network id");
    }
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
