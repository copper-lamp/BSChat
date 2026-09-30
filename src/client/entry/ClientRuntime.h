#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include "core/config/Config.h"
#include "core/codec/OpusCodec.h"
#include "core/audio/JitterBuffer.h"
#include "client/audio/AgcProcessor.h"
#include "client/input/Triggers.h"
#include "core/pipeline/ITransport.h"
namespace bsc::client {
using IClientTransport = pipeline::ITransport;
struct Position { float x=0,y=0,z=0; int32_t dimensionId=0; uint8_t envFlags=0; };
class IPlayerState { public: virtual ~IPlayerState()=default; virtual protocol::PlayerId playerId() const=0; virtual Position position() const=0; };
class IClock { public: virtual ~IClock()=default; virtual int64_t nowMs() const=0; };
class ClientRuntime final {
public:
    enum class State { Stopped, Handshaking, Ready, Failed };
    using RenderSink = std::function<void(const float*, std::size_t)>;
    // 服务端命令触发的自检请求回调；只会从主线程（tick）回调，便于直接驱动 UI/日志。
    using SmokeTestRequestHandler = std::function<void()>;
    // 服务端回传的转写文本；从网络线程回调，订阅方需自行保证线程安全。
    using SttTextHandler = std::function<void(protocol::SttTextMessage const&)>;
    // 面板中继消息；从网络线程回调，订阅方需自行保证线程安全。
    using UiFormHandler = std::function<void(protocol::UiFormMessage const&)>;
    ClientRuntime(IClientTransport&, IPlayerState&, IClock&, config::ClientConfig);
    ~ClientRuntime();
    void start(); void stop(); void tick();
    void onMessage(const protocol::PlayerId&, const protocol::Message&);
    void setTalking(bool); void submitAudio(std::vector<uint8_t>); void submitPcm(const float*, std::size_t);
    void submitVadPcm(const float*, std::size_t);
    // PTT 按键入口。自动检测（VAD）模式下说话状态由检测器独占，按键不参与：
    // 否则松键事件会把检测器刚判出的“正在说话”强行关掉，两种模式互相打架。
    // 返回 false 表示本次按键未被采纳（当前是自动检测模式），调用方可据此提示。
    bool setPttPressed(bool down);
    void setRenderSink(RenderSink sink); void setOutputVolume(float volume); void setOutputMuted(bool muted);
    // 面板改配置后把新配置同步进运行时。只接受运行期可热改的字段（说话模式/字幕开关/音量/
    // 握手重试），音频格式与编解码器不在此改动：渲染设备格式在 onJoin 时已固定，会话中无法切换。
    // 必须在切到自动检测（VAD）时调用，否则采集回调会把帧交给 submitVadPcm，而运行时内部
    // 仍持旧 vadEnabled=false 直接丢弃，表现为「面板开了自动检测但完全没反应」。
    void applyConfig(config::ClientConfig const& config);
    // 自检用：把本机合成的 PCM 直接交给渲染 sink（经设备播放），不经过网络与服务端。
    void playLocalPcm(const float* samples, std::size_t count);
    void setSmokeTestRequestHandler(SmokeTestRequestHandler handler);
    void setSttTextHandler(SttTextHandler handler);
    void setUiFormHandler(UiFormHandler handler);
    void reportPosition(); State state() const{return state_;}
    // 本端是否正在按住说话（HUD 状态覆盖层用）。
    bool talking() const { return talking_.load(); }
    // 自动检测是否已开启（面板「自动检测」开关的运行时视角）。
    bool vadActive() const { return vadEnabled_.load(); }
    const config::ClientConfig& config() const { return config_; }
    // Number of MixStream frames handed to the render sink so far.
    uint64_t playedMixFrames() const { return playedMixFrames_; }
    // Number of MixStream messages received from the server so far.
    uint64_t receivedMixFrames() const { return receivedMixFrames_; }
    // Number of encoded voice frames sent upstream so far.
    uint64_t sentAudioFrames() const { return sentAudioFrames_; }
private:
    void sendHello(); void drainPlayback();
    // 按当前 config_.audio 重建编解码器与帧缓冲（首次构造与协商帧长后共用）
    void rebuildCodecs();
    // 采用服务端协商的帧长（服务端是音频格式权威）：重建编解码器与帧缓冲，
    // 让上行/解码/自检节拍与新帧长一致。采样率不在此处改（渲染设备格式在启动时固定）。
    void applyNegotiatedFrameSize(int frameSizeMs);
    IClientTransport& transport_; IPlayerState& player_; IClock& clock_; config::ClientConfig config_;
    // talking_ 由采集（音频）线程（VAD 判定结果）与主线程（PTT 按键、自检、配置切换）共同驱动，
    // 因此是原子量；setTalking 内部用 CAS 保证「只发一次开关」在并发下也成立。
    State state_=State::Stopped; int64_t nextHelloMs_=0; int64_t nextPositionMs_=0; uint64_t seq_=0;
    std::atomic<bool> talking_{false};
    // 能力协商：Hello 时声明本端能力，Welcome 时按服务端支持取交集，字幕仅在协商通过后放行
    uint8_t declaredCapabilities_=protocol::CapabilityNone;
    uint8_t negotiatedCapabilities_=protocol::CapabilityNone;
    bool negotiatedSttEnabled_=false;
    std::unique_ptr<codec::OpusEncoder> encoder_; std::unique_ptr<codec::OpusDecoder> decoder_;
    audio::AgcProcessor agc_; ::bsc::audio::JitterBuffer jitter_; RenderSink renderSink_; float outputVolume_=1.0F; bool outputMuted_=false;
    SmokeTestRequestHandler smokeTestRequestHandler_; std::atomic<bool> smokeTestRequested_{false};
    SttTextHandler sttTextHandler_;
    UiFormHandler uiFormHandler_;
    std::vector<float> pcmFrame_; std::vector<uint8_t> encoded_; std::size_t pcmPending_=0;
    uint64_t playedMixFrames_=0; uint64_t receivedMixFrames_=0; uint64_t sentAudioFrames_=0;
    // 自动检测（VAD）判定器：只被采集（音频）线程驱动，回调同样落在该线程。
    // 独立成对象而不是散在 submitVadPcm 里的常量，便于脱离游戏做单元测试。
    // 主线程不能直接碰它（无锁），改用 vadResetRequested_ 让采集线程在下一次喂帧时重置。
    std::unique_ptr<input::VadTrigger> vad_;
    std::atomic<bool> vadResetRequested_{false};
    // vadEnabled 的原子镜像：采集（音频）线程在 submitVadPcm 里读它，主线程在 applyConfig 里写它。
    // config_ 本体只在主线程读写，音频线程不碰，避免用整份配置做跨线程同步。
    std::atomic<bool> vadEnabled_{false};
    // 说话模式：0=按键说话（PTT 按键直接驱动），1=自动检测（VAD 检测器驱动）。
    // PTT 按键在自动检测模式下不参与，避免松键事件把 VAD 刚判出的“正在说话”强行关掉。
    std::atomic<int> talkMode_{0};
};
}
