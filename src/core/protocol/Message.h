#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace bsc::protocol {

// 玩家身份：128 位 UUID（与服务端玩家 UUID 一致）
using PlayerId = std::array<uint8_t, 16>;

// 消息信封魔数 "BS"
inline constexpr uint16_t kMagic = 0x4253;

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
    PosUpdate  = 7, // C→S 位置/环境更新（空间混音选路输入）
    UiForm     = 8, // 双向：面板表单中继（客户端构表单 JSON，服务端投递并回传结果）
};

// 客户端能力位（Hello.capabilities）
enum Capability : uint8_t {
    CapabilityNone      = 0,
    CapabilityPtt       = 1 << 0, // 支持按键说话
    CapabilityVad       = 1 << 1, // 支持自动语音检测（v1 声明但默认关闭）
    CapabilitySubtitle  = 1 << 2, // 支持字幕显示
};

// 需要服务端协同的能力位：只有这些位参与 Hello→Welcome 的双方协商并在
// Welcome.serverCapabilities 回传。CapabilityVad 是客户端本地行为（不涉及网络
// 约束），服务端不参与，也不在协商结果里回传，避免新版服务端误关旧端 VAD。
inline constexpr uint8_t kServerNegotiableCapabilities =
    static_cast<uint8_t>(CapabilityPtt | CapabilitySubtitle);

// 协商标记：Welcome.serverCapabilities 为 0 表示对端是未参与协商的旧服务端。
// 此时接收端回落到 v1 既有行为（不做可选能力裁剪），保证新客户端连旧服务端时
// 基础功能与字幕不被误关。
inline constexpr uint8_t kCapabilitiesNotNegotiated = 0;

// 双方能力协商：只保留请求方声明且服务端支持的共同能力位。
inline constexpr uint8_t negotiateCapabilities(uint8_t clientCapabilities, uint8_t serverSupportedCapabilities) {
    return static_cast<uint8_t>(clientCapabilities & serverSupportedCapabilities & kServerNegotiableCapabilities);
}

// 判定能力位是否可用。negotiated 为 0 视为旧对端未协商，按 v1 既有行为放行。
inline constexpr bool capabilityEnabled(uint8_t negotiatedCapabilities, uint8_t capability) {
    if (negotiatedCapabilities == kCapabilitiesNotNegotiated) return true;
    return (negotiatedCapabilities & capability) != 0;
}

// 控制类型
enum class ControlType : uint8_t {
    PttPressed   = 1, // C→S：PTT 按下
    PttReleased  = 2, // C→S：PTT 松开
    ClientMute   = 3, // C→S：本端静音
    ForceMute    = 4, // S→C：服务器强制静音
    SyncState    = 5, // S→C：状态同步
    SmokeTest    = 6, // S→C：请求该客户端开始端到端链路自检（服务端命令触发）
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

// 位置/环境更新 <--> 空间混音选路输入；服务端 MixerCore 据此计算逐接收者增益。
enum EnvFlag : uint8_t {
    EnvFlagNone       = 0,
    EnvFlagUnderwater = 1 << 0, // 水下：额外低沉/混响衰减
    EnvFlagCave       = 1 << 1, // 洞穴：回声
    EnvFlagFastMove   = 1 << 2, // 快速移动：选路时可削弱远端
};

struct PosUpdateMessage {
    PlayerId playerId{};
    float x = 0, y = 0, z = 0;      // 当前世界坐标（方块）
    int32_t dimensionId = 0;        // 维度 ID（跨维度隔离判定）
    uint8_t envFlags = 0;           // EnvFlag 位或
    uint64_t sendAtMs = 0;          // 客户端发送时刻（服务器时钟）
};

// 面板表单中继方向。表单本身只能由服务端下发给客户端（Bedrock 表单走
// ModalFormRequest/Response 网络包），因此客户端设置面板经服务端中继。
enum class UiFormKind : uint8_t {
    Request  = 1, // C→S：请求服务端把 payload 里的表单 JSON 投递给本玩家
    Response = 2, // S→C：本玩家对某次 Request 的提交/取消结果
};

// 中继载荷上限：服务端据此拒绝异常大的表单 JSON，避免形成滥用面。
inline constexpr std::size_t kUiFormPayloadMaxBytes = 32 * 1024;

struct UiFormMessage {
    UiFormKind kind = UiFormKind::Request;
    uint32_t requestId = 0;     // 关联 Request/Response，避免并发面板串味
    int32_t cancelReason = -1;  // Response 专用：-1 表示正常提交
    std::string payload;        // Request：表单 JSON；Response：响应 JSON（取消时为空）
};

using Message = std::variant<
    HelloMessage,
    WelcomeMessage,
    AudioDataMessage,
    MixStreamMessage,
    SttTextMessage,
    ControlMessage,
    PosUpdateMessage,
    UiFormMessage>;

} // namespace bsc::protocol
