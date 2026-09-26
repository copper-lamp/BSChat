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
#include "core/pipeline/ITransport.h"
namespace vc::client {
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
    ClientRuntime(IClientTransport&, IPlayerState&, IClock&, config::ClientConfig);
    ~ClientRuntime();
    void start(); void stop(); void tick();
    void onMessage(const protocol::PlayerId&, const protocol::Message&);
    void setTalking(bool); void submitAudio(std::vector<uint8_t>); void submitPcm(const float*, std::size_t);
    void setRenderSink(RenderSink sink); void setOutputVolume(float volume); void setOutputMuted(bool muted);
    // 自检用：把本机合成的 PCM 直接交给渲染 sink（经设备播放），不经过网络与服务端。
    void playLocalPcm(const float* samples, std::size_t count);
    void setSmokeTestRequestHandler(SmokeTestRequestHandler handler);
    void setSttTextHandler(SttTextHandler handler);
    void reportPosition(); State state() const{return state_;}
    // 本端是否正在按住说话（HUD 状态覆盖层用）。
    bool talking() const { return talking_; }
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
    State state_=State::Stopped; int64_t nextHelloMs_=0; int64_t nextPositionMs_=0; uint64_t seq_=0; bool talking_=false;
    std::unique_ptr<codec::OpusEncoder> encoder_; std::unique_ptr<codec::OpusDecoder> decoder_;
    audio::AgcProcessor agc_; ::vc::audio::JitterBuffer jitter_; RenderSink renderSink_; float outputVolume_=1.0F; bool outputMuted_=false;
    SmokeTestRequestHandler smokeTestRequestHandler_; std::atomic<bool> smokeTestRequested_{false};
    SttTextHandler sttTextHandler_;
    std::vector<float> pcmFrame_; std::vector<uint8_t> encoded_; std::size_t pcmPending_=0;
    uint64_t playedMixFrames_=0; uint64_t receivedMixFrames_=0; uint64_t sentAudioFrames_=0;
};
}
