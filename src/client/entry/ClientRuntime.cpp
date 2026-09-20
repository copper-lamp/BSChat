#include "client/entry/ClientRuntime.h"
#include <utility>
namespace vc::client { ClientRuntime::ClientRuntime(IClientTransport& t,IPlayerState& p,IClock& c,config::ClientConfig cfg):transport_(t),player_(p),clock_(c),config_(std::move(cfg)) { transport_.setMessageHandler([this](auto const& id,auto const& m){onMessage(id,m);}); }
void ClientRuntime::start(){ state_=State::Handshaking; nextHelloMs_=0; sendHello(); }
void ClientRuntime::stop(){state_=State::Stopped; talking_=false;}
void ClientRuntime::sendHello(){ protocol::HelloMessage h; h.playerId=player_.playerId(); h.protocolVersion=protocol::kProtocolVersion; h.sampleRate=uint16_t(config_.audio.sampleRate); h.frameSizeMs=uint8_t(config_.audio.frameSizeMs); h.capabilities=protocol::CapabilityPtt | (config_.vadEnabled?protocol::CapabilityVad:0) | (config_.subtitleEnabled?protocol::CapabilitySubtitle:0); transport_.send({},h); nextHelloMs_=clock_.nowMs()+config_.handshakeRetryMs; }
void ClientRuntime::tick(){if(state_==State::Handshaking && clock_.nowMs()>=nextHelloMs_) sendHello(); if(state_==State::Ready) reportPosition();}
void ClientRuntime::onMessage(const protocol::PlayerId&,const protocol::Message& m){ if(auto w=std::get_if<protocol::WelcomeMessage>(&m)){ if(w->protocolVersion!=protocol::kProtocolVersion){state_=State::Failed;return;} state_=State::Ready; } }
void ClientRuntime::setTalking(bool v){if(v==talking_||state_!=State::Ready)return; talking_=v; transport_.send({},protocol::ControlMessage{v?protocol::ControlType::PttPressed:protocol::ControlType::PttReleased,0});}
void ClientRuntime::submitAudio(std::vector<uint8_t> data){if(state_!=State::Ready||!talking_)return; transport_.send({},protocol::AudioDataMessage{seq_++,protocol::AudioFlagNone,std::move(data)});}
void ClientRuntime::reportPosition(){auto p=player_.position(); protocol::PosUpdateMessage m; m.playerId=player_.playerId();m.x=p.x;m.y=p.y;m.z=p.z;m.dimensionId=p.dimensionId;m.envFlags=p.envFlags;m.sendAtMs=uint64_t(clock_.nowMs());transport_.send({},m);}
}
