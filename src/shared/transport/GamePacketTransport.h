#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "core/pipeline/ITransport.h"
#include "core/protocol/Message.h"

class Player;

namespace vc::shared {

// 客户端模式对端标识（全 0）：下行混音流/字幕一律来自所连接的服务器。
// 真实玩家 UUID 不可能是全 0（mce::UUID::EMPTY），可安全用作哨兵。
inline constexpr protocol::PlayerId kServerPlayerId{};

// 传输模式：服务端按玩家 UUID 路由；客户端统一发往所连接的服务器。
enum class TransportMode {
    Server,
    Client,
};

// 游戏内自定义包传输（v1）：包体即协议信封字节（core MessageCodec 打包）。
//  - 双端注册同一组自定义包 ID，包到达由 ll::network::PacketRegistrar 分发给本类；
//  - 服务端：send 按 PlayerId 解析到 Player 并 sendTo；包到达按网络标识解析回 PlayerId；
//  - 客户端：send 一律 sendToServer；包到达对端记为 kServerPlayerId。
//  - 实时语音帧（AudioData/MixStream）用不可靠传输，靠抖动缓冲抗乱序/丢包；
//    控制/握手/字幕用可靠传输。
// 线程模型：send 仅允许主线程调用（混音线程写待发队列，主线程排空）；
// 包到达运行在网络线程，dispatch 用互斥锁保护消息回调。
class GamePacketTransport final : public pipeline::ITransport {
public:
    // 服务端注入：PlayerId → Player*（在线校验 + 路由）；客户端模式恒为 null。
    using PlayerResolver = std::function<Player*(const protocol::PlayerId&)>;

    // 诊断日志（与 ctx 一致的签名）；下行找不到收件人时给出明确告警。
    using LogSink = std::function<void(bool isError, const std::string&)>;

    explicit GamePacketTransport(TransportMode mode, PlayerResolver resolver = {});
    ~GamePacketTransport();

    void send(const protocol::PlayerId& peerId, const protocol::Message& message) override;
    void setMessageHandler(MessageHandler handler) override;
    void clearMessageHandler() override;
    void setLogSink(LogSink sink);

    // 服务端路由兜底：PlayerJoinEvent 触发晚于客户端首个 Hello，只靠它填 Player* 会让
    // 握手期的下行被丢弃（实测握手被拖后约 30 秒）。首个数据包到达时自动 rememberPlayer，
    // 玩家离线/模组停用时由 ServerMod 注销。
    void forgetPlayer(const protocol::PlayerId& peerId);
    void clearPlayers();

    TransportMode mode() const { return mode_; }

private:
    friend class VoiceChatPacketHandler;

    void onPacketReceived(const std::vector<uint8_t>& payload, const protocol::PlayerId& peerId);
    void dispatch(const protocol::PlayerId& peerId, const protocol::Message& message);
    void rememberPlayer(const protocol::PlayerId& peerId, Player* player);

    TransportMode mode_;
    PlayerResolver resolver_;
    MessageHandler handler_;
    LogSink logSink_;
    std::mutex handlerMutex_; // 网络线程 dispatch 与主线程装配之间的保护
    std::mutex playersMutex_; // 网络线程写入 / 主线程发送读取
    std::map<protocol::PlayerId, Player*> peerPlayers_;
    uint32_t sendSeq_ = 0;    // 信封级序号（仅主线程访问）
};

} // namespace vc::shared
