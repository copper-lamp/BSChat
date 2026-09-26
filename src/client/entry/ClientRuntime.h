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
    ClientRuntime(IClientTransport&, IPlayerState&, IClock&, config::ClientConfig);
    ~ClientRuntime();
    void start(); void stop(); void tick();
    void onMessage(const protocol::PlayerId&, const protocol::Message&);
    void setTalking(bool); void submitAudio(std::vector<uint8_t>); void submitPcm(const float*, std::size_t);
    void setRenderSink(RenderSink sink); void setOutputVolume(float volume); void setOutputMuted(bool muted);
    void setSmokeTestRequestHandler(SmokeTestRequestHandler handler);
    void reportPosition(); State state() const{return state_;}
    const config::ClientConfig& config() const { return config_; }
    // Number of MixStream frames handed to the render sink so far.
    uint64_t playedMixFrames() const { return playedMixFrames_; }
    // Number of MixStream messages received from the server so far.
    uint64_t receivedMixFrames() const { return receivedMixFrames_; }
    // Number of encoded voice frames sent upstream so far.
    uint64_t sentAudioFrames() const { return sentAudioFrames_; }
private:
    void sendHello(); void drainPlayback();
    IClientTransport& transport_; IPlayerState& player_; IClock& clock_; config::ClientConfig config_;
    State state_=State::Stopped; int64_t nextHelloMs_=0; int64_t nextPositionMs_=0; uint64_t seq_=0; bool talking_=false;
    std::unique_ptr<codec::OpusEncoder> encoder_; std::unique_ptr<codec::OpusDecoder> decoder_;
    audio::AgcProcessor agc_; ::vc::audio::JitterBuffer jitter_; RenderSink renderSink_; float outputVolume_=1.0F; bool outputMuted_=false;
    SmokeTestRequestHandler smokeTestRequestHandler_; std::atomic<bool> smokeTestRequested_{false};
    std::vector<float> pcmFrame_; std::vector<uint8_t> encoded_; std::size_t pcmPending_=0;
    uint64_t playedMixFrames_=0; uint64_t receivedMixFrames_=0; uint64_t sentAudioFrames_=0;
};
}
