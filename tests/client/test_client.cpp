#include "Harness.h"
#include "client/input/Triggers.h"
#include "client/audio/AgcProcessor.h"
#include "client/entry/ClientRuntime.h"
#include <vector>
using namespace vc;
namespace { struct T: pipeline::ITransport { std::vector<protocol::Message> out; MessageHandler h; void send(const protocol::PlayerId&, const protocol::Message& m) override {out.push_back(m);} void setMessageHandler(MessageHandler x) override {h=std::move(x);} }; struct P: client::IPlayerState { protocol::PlayerId playerId()const override{return {};}; client::Position position()const override{return {1,2,3,0,0};} }; struct C:client::IClock{int64_t t=0;int64_t nowMs()const override{return t;}}; }
TEST(PttAndVad) { int n=0; client::input::PttTrigger p(7,[&](bool a,int64_t){n+=a?1:-1;});p.onKey(7,true,1);p.onKey(7,false,2);EXPECT_EQ(n,0); client::input::VadTrigger v(.5f,10,[&](bool a,int64_t){n+=a?2:-2;});v.onLevel(1,0);v.onLevel(0,11);v.onLevel(0,21);EXPECT_EQ(n,0); }
TEST(Agc) { float x[2]={.1f,.1f}; client::audio::AgcProcessor a; a.process(x,2); EXPECT_NEAR(x[0],.18,.01); }
TEST(Handshake) {T t;P p;C c;client::ClientRuntime r(t,p,c,{});r.start();EXPECT_EQ(r.state(),client::ClientRuntime::State::Handshaking);t.h({},protocol::WelcomeMessage{1,48000,60,false,0});EXPECT_EQ(r.state(),client::ClientRuntime::State::Ready);r.setTalking(true);EXPECT_EQ(t.out.size(),2u);}
