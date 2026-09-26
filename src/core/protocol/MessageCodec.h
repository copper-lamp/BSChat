#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/protocol/Message.h"

namespace bsc::protocol {

// 信封解包信息
struct EnvelopeInfo {
    uint8_t version = 0;
    MessageType type = MessageType::Hello;
    uint32_t seq = 0;
    int64_t timestampMs = 0;
};

// 协议消息编解码：紧凑二进制，固定 little-endian。
// 扩展规则：新增消息只需在 MessageType 登记并在 deserialize 增加分支；
// 未知类型/损坏数据一律返回 nullopt，由上层按"忽略"处理。
class MessageCodec {
public:
    // 打包一条消息为完整信封字节流
    static std::vector<uint8_t> pack(const Message& message, uint32_t seq, int64_t timestampMs);

    // 解包信封字节流；未知类型/校验失败返回 nullopt
    static std::optional<std::pair<EnvelopeInfo, Message>> unpack(std::span<const uint8_t> data);

private:
    static std::pair<MessageType, std::vector<uint8_t>> serializePayload(const Message& message);
    static std::optional<Message> deserializePayload(MessageType type, std::span<const uint8_t> payload);
};

} // namespace bsc::protocol
