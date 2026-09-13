#include "core/protocol/MessageCodec.h"

#include <cstring>

namespace vc::protocol {
namespace {

// 小端写入器
class BufferWriter {
public:
    void u8(uint8_t v) { buf_.push_back(v); }
    void u16(uint16_t v) {
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            buf_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            buf_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }
    void bytes(const uint8_t* data, size_t n) { buf_.insert(buf_.end(), data, data + n); }
    void string(std::string_view s) {
        u16(static_cast<uint16_t>(s.size()));
        bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
    std::vector<uint8_t> take() && { return std::move(buf_); }

private:
    std::vector<uint8_t> buf_;
};

// 小端读取器（带越界检查）
class BufferReader {
public:
    explicit BufferReader(std::span<const uint8_t> data) : data_(data) {}

    bool u8(uint8_t& v) {
        if (pos_ + 1 > data_.size()) return false;
        v = data_[pos_++];
        return true;
    }
    bool u16(uint16_t& v) {
        if (pos_ + 2 > data_.size()) return false;
        v = static_cast<uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
        pos_ += 2;
        return true;
    }
    bool u32(uint32_t& v) {
        if (pos_ + 4 > data_.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(data_[pos_ + i]) << (8 * i);
        pos_ += 4;
        return true;
    }
    bool u64(uint64_t& v) {
        if (pos_ + 8 > data_.size()) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(data_[pos_ + i]) << (8 * i);
        pos_ += 8;
        return true;
    }
    bool i64(int64_t& v) {
        uint64_t raw = 0;
        if (!u64(raw)) return false;
        v = static_cast<int64_t>(raw);
        return true;
    }
    bool bytes(uint8_t* out, size_t n) {
        if (pos_ + n > data_.size()) return false;
        std::memcpy(out, data_.data() + pos_, n);
        pos_ += n;
        return true;
    }
    bool string(std::string& s) {
        uint16_t len = 0;
        if (!u16(len)) return false;
        if (pos_ + len > data_.size()) return false;
        s.assign(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += len;
        return true;
    }
    size_t position() const { return pos_; }
    size_t remaining() const { return data_.size() - pos_; }

private:
    std::span<const uint8_t> data_;
    size_t pos_ = 0;
};

} // namespace

std::vector<uint8_t> MessageCodec::pack(const Message& message, uint32_t seq, int64_t timestampMs) {
    auto [type, payload] = serializePayload(message);

    BufferWriter w;
    w.u16(kMagic);
    w.u8(kProtocolVersion);
    w.u8(static_cast<uint8_t>(type));
    w.u32(seq);
    w.u64(static_cast<uint64_t>(timestampMs));
    w.u16(static_cast<uint16_t>(payload.size()));
    w.bytes(payload.data(), payload.size());
    return std::move(w).take();
}

std::optional<std::pair<EnvelopeInfo, Message>> MessageCodec::unpack(std::span<const uint8_t> data) {
    BufferReader r(data);

    uint16_t magic = 0;
    if (!r.u16(magic) || magic != kMagic) return std::nullopt;

    EnvelopeInfo info;
    if (!r.u8(info.version)) return std::nullopt;
    uint8_t typeRaw = 0;
    if (!r.u8(typeRaw)) return std::nullopt;
    info.type = static_cast<MessageType>(typeRaw);
    if (!r.u32(info.seq)) return std::nullopt;
    if (!r.i64(info.timestampMs)) return std::nullopt;
    uint16_t payloadLen = 0;
    if (!r.u16(payloadLen)) return std::nullopt;
    if (r.remaining() < payloadLen) return std::nullopt;

    auto payload = data.subspan(r.position(), payloadLen);
    auto message = deserializePayload(info.type, payload);
    if (!message) return std::nullopt; // 未知类型或数据损坏 → 忽略

    return std::make_pair(info, std::move(*message));
}

std::pair<MessageType, std::vector<uint8_t>> MessageCodec::serializePayload(const Message& message) {
    return std::visit(
        [](const auto& m) -> std::pair<MessageType, std::vector<uint8_t>> {
            using M = std::decay_t<decltype(m)>;

            if constexpr (std::is_same_v<M, HelloMessage>) {
                BufferWriter w;
                w.bytes(m.playerId.data(), m.playerId.size());
                w.u8(m.protocolVersion);
                w.u16(m.sampleRate);
                w.u8(m.frameSizeMs);
                w.u8(m.capabilities);
                return {MessageType::Hello, std::move(w).take()};

            } else if constexpr (std::is_same_v<M, WelcomeMessage>) {
                BufferWriter w;
                w.u8(m.protocolVersion);
                w.u16(m.sampleRate);
                w.u8(m.frameSizeMs);
                w.u8(m.sttEnabled ? 1 : 0);
                w.u8(m.serverCapabilities);
                return {MessageType::Welcome, std::move(w).take()};

            } else if constexpr (std::is_same_v<M, AudioDataMessage>) {
                BufferWriter w;
                w.u64(m.seq);
                w.u8(m.flags);
                w.u16(static_cast<uint16_t>(m.opusData.size()));
                w.bytes(m.opusData.data(), m.opusData.size());
                return {MessageType::AudioData, std::move(w).take()};

            } else if constexpr (std::is_same_v<M, MixStreamMessage>) {
                BufferWriter w;
                w.u64(m.seq);
                w.u16(static_cast<uint16_t>(m.opusData.size()));
                w.bytes(m.opusData.data(), m.opusData.size());
                return {MessageType::MixStream, std::move(w).take()};

            } else if constexpr (std::is_same_v<M, SttTextMessage>) {
                BufferWriter w;
                w.bytes(m.speakerId.data(), m.speakerId.size());
                w.u8(m.isFinal ? 1 : 0);
                w.string(m.text);
                return {MessageType::SttText, std::move(w).take()};

            } else if constexpr (std::is_same_v<M, ControlMessage>) {
                BufferWriter w;
                w.u8(static_cast<uint8_t>(m.type));
                w.u8(m.value);
                return {MessageType::Control, std::move(w).take()};
            }
        },
        message
    );
}

std::optional<Message> MessageCodec::deserializePayload(MessageType type, std::span<const uint8_t> payload) {
    BufferReader r(payload);

    switch (type) {
    case MessageType::Hello: {
        HelloMessage m;
        if (!r.bytes(m.playerId.data(), m.playerId.size())) return std::nullopt;
        if (!r.u8(m.protocolVersion)) return std::nullopt;
        if (!r.u16(m.sampleRate)) return std::nullopt;
        if (!r.u8(m.frameSizeMs)) return std::nullopt;
        if (!r.u8(m.capabilities)) return std::nullopt;
        return m;
    }
    case MessageType::Welcome: {
        WelcomeMessage m;
        if (!r.u8(m.protocolVersion)) return std::nullopt;
        if (!r.u16(m.sampleRate)) return std::nullopt;
        if (!r.u8(m.frameSizeMs)) return std::nullopt;
        uint8_t stt = 0;
        if (!r.u8(stt)) return std::nullopt;
        m.sttEnabled = stt != 0;
        if (!r.u8(m.serverCapabilities)) return std::nullopt;
        return m;
    }
    case MessageType::AudioData: {
        AudioDataMessage m;
        if (!r.u64(m.seq)) return std::nullopt;
        if (!r.u8(m.flags)) return std::nullopt;
        uint16_t len = 0;
        if (!r.u16(len)) return std::nullopt;
        if (r.remaining() < len) return std::nullopt;
        m.opusData.assign(payload.begin() + static_cast<std::ptrdiff_t>(r.position()),
                          payload.begin() + static_cast<std::ptrdiff_t>(r.position() + len));
        return m;
    }
    case MessageType::MixStream: {
        MixStreamMessage m;
        if (!r.u64(m.seq)) return std::nullopt;
        uint16_t len = 0;
        if (!r.u16(len)) return std::nullopt;
        if (r.remaining() < len) return std::nullopt;
        m.opusData.assign(payload.begin() + static_cast<std::ptrdiff_t>(r.position()),
                          payload.begin() + static_cast<std::ptrdiff_t>(r.position() + len));
        return m;
    }
    case MessageType::SttText: {
        SttTextMessage m;
        if (!r.bytes(m.speakerId.data(), m.speakerId.size())) return std::nullopt;
        uint8_t final = 0;
        if (!r.u8(final)) return std::nullopt;
        m.isFinal = final != 0;
        if (!r.string(m.text)) return std::nullopt;
        return m;
    }
    case MessageType::Control: {
        ControlMessage m;
        uint8_t typeRaw = 0;
        if (!r.u8(typeRaw)) return std::nullopt;
        m.type = static_cast<ControlType>(typeRaw);
        if (!r.u8(m.value)) return std::nullopt;
        return m;
    }
    default:
        return std::nullopt; // 未知类型 → 忽略
    }
}

} // namespace vc::protocol
