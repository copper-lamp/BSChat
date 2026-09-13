#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace vc::protocol {

// 玩家身份：128 位 UUID（与服务端玩家 UUID 一致）
using PlayerId = std::array<uint8_t, 16>;

// 消息信封魔数 "VC"
inline constexpr uint16_t kMagic = 0x5643;

// 当前协议版本
inline constexpr uint8_t kProtocolVersion = 1;

// 消息类型注册表：新增消息在此登记即可前后兼容（未知类型被忽略）
enum class MessageType : uint8_t {
    Hello      = 1, // C→S 握手：身份/参数/能力
    Welcome    = 2, // S→C 握手确认：协商结果
    AudioData  = 3, // C→S 上行语音帧
    MixStream  = 4, // S→C 下行混合语音帧
    SttText    = 5, // S→C 转写文字（字幕）
    Control    = 6, // 双向控制
};

// 客户端能力位（Hello.capabilities）
enum Capability : uint8_t {
    CapabilityNone      = 0,
    CapabilityPtt       = 1 << 0, // 支持按键说话
    CapabilityVad       = 1 << 1, // 支持自动语音检测（v1 声明但默认关闭）
    CapabilitySubtitle  = 1 << 2, // 支持字幕显示
};

// 控制类型
enum class ControlType : uint8_t {
    PttPressed   = 1, // C→S：PTT 按下
    PttReleased  = 2, // C→S：PTT 松开
    ClientMute   = 3, // C→S：本端静音
    ForceMute    = 4, // S→C：服务器强制静音
    SyncState    = 5, // S→C：状态同步
};

// 语音帧标志（AudioData.flags）
enum AudioFlag : uint8_t {
    AudioFlagNone  = 0,
    AudioFlagStart = 1 << 0, // 一句话的开始帧
    AudioFlagEnd   = 1 << 1, // 一句话的结束帧
};

struct HelloMessage {
    PlayerId playerId{};
    uint8_t protocolVersion = 0;
    uint16_t sampleRate = 0;   // 采集采样率，如 48000
    uint8_t frameSizeMs = 0;   // 帧长，如 60
    uint8_t capabilities = 0;  // Capability 位或
};

struct WelcomeMessage {
    uint8_t protocolVersion = 0;
    uint16_t sampleRate = 0;   // 协商后采样率
    uint8_t frameSizeMs = 0;   // 协商后帧长
    bool sttEnabled = false;
    uint8_t serverCapabilities = 0;
};

struct AudioDataMessage {
    uint64_t seq = 0;          // 发送序号（每会话单调递增，供抖动缓冲排序）
    uint8_t flags = 0;         // AudioFlag 位或
    std::vector<uint8_t> opusData; // 压缩帧；空表示仅控制标志
};

struct MixStreamMessage {
    uint64_t seq = 0;          // 混合帧序号
    std::vector<uint8_t> opusData;
};

struct SttTextMessage {
    PlayerId speakerId{};
    bool isFinal = false;      // true=最终结果，false=部分结果
    std::string text;
};

struct ControlMessage {
    ControlType type = ControlType::SyncState;
    uint8_t value = 0;
};

using Message = std::variant<
    HelloMessage,
    WelcomeMessage,
    AudioDataMessage,
    MixStreamMessage,
    SttTextMessage,
    ControlMessage>;

} // namespace vc::protocol
