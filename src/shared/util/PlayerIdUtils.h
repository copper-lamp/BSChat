#pragma once

#include <cstdint>

#include "core/protocol/Message.h"
#include "mc/platform/UUID.h"

namespace bsc::shared {

// mce::UUID → 协议 PlayerId（16 字节，小端：前 8 字节 = uuid.a，后 8 字节 = uuid.b）。
// 客户端与服务端共用同一换算，保证玩家身份一致。
inline protocol::PlayerId playerIdFromUuid(mce::UUID const& uuid) {
    protocol::PlayerId id{};
    uint64_t const parts[2] = {uuid.a, uuid.b};
    for (int i = 0; i < 16; ++i) {
        id[static_cast<size_t>(i)] = static_cast<uint8_t>((parts[i / 8] >> (8 * (i % 8))) & 0xFF);
    }
    return id;
}

} // namespace bsc::shared
