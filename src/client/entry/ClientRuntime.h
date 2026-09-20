#pragma once
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
    ClientRuntime(IClientTransport&, IPlayerState&, IClock&, config::ClientConfig);
    ~ClientRuntime();
    void start(); void stop(); void tick();
    void onMessage(const protocol::PlayerId&, const protocol::Message&);
    void setTalking(bool); void submitAudio(std::vector<uint8_t>); void submitPcm(const float*, std::size_t);
    void setRenderSink(RenderSink sink); void setOutputVolume(float volume); void setOutputMuted(bool muted);
    void reportPosition(); State state() const{return state_;}
private:
    void sendHello(); void drainPlayback();
    IClientTransport& transport_; IPlayerState& player_; IClock& clock_; config::ClientConfig config_;
    State state_=State::Stopped; int64_t nextHelloMs_=0; int64_t nextPositionMs_=0; uint64_t seq_=0; bool talking_=false;
    std::unique_ptr<codec::OpusEncoder> encoder_; std::unique_ptr<codec::OpusDecoder> decoder_;
    audio::AgcProcessor agc_; ::vc::audio::JitterBuffer jitter_; RenderSink renderSink_; float outputVolume_=1.0F; bool outputMuted_=false;
    std::vector<float> pcmFrame_; std::vector<uint8_t> encoded_; std::size_t pcmPending_=0;
};
}
