#pragma once

#include <functional>

#include "core/protocol/Message.h"

namespace bsc::pipeline {

// 传输层抽象：业务层通过它收发协议消息。
// v1 实现为游戏内自定义包（GamePacketTransport，位于 src/shared）；
// 后续可新增 UDP 通道实现，业务侧零改动。
class ITransport {
public:
    using MessageHandler = std::function<void(const protocol::PlayerId& peerId, const protocol::Message&)>;

    virtual ~ITransport() = default;

    // 向对端发送一条消息。peerId：服务端为玩家 UUID；客户端为服务器会话标识。
    virtual void send(const protocol::PlayerId& peerId, const protocol::Message& message) = 0;

    // 注册收包回调（实现层负责反序列化与分发）
    virtual void setMessageHandler(MessageHandler handler) = 0;

    // Optional lifecycle hook. Implementations with asynchronous delivery must
    // stop invoking the previous owner before that owner is destroyed.
    virtual void clearMessageHandler() {}
};

} // namespace bsc::pipeline
