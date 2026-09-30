#include "Harness.h"
#include "client/input/Triggers.h"
#include "client/audio/AgcProcessor.h"
#include "client/entry/ClientRuntime.h"
#include <vector>
using namespace bsc;
namespace { struct T: pipeline::ITransport { std::vector<protocol::Message> out; MessageHandler h; void send(const protocol::PlayerId&, const protocol::Message& m) override {out.push_back(m);} void setMessageHandler(MessageHandler x) override {h=std::move(x);} }; struct P: client::IPlayerState { protocol::PlayerId playerId()const override{return {};}; client::Position position()const override{return {1,2,3,0,0};} }; struct C:client::IClock{int64_t t=0;int64_t nowMs()const override{return t;}}; }
TEST(PttAndVad) { int n=0; client::input::PttTrigger p(7,[&](bool a,int64_t){n+=a?1:-1;});p.onKey(7,true,1);p.onKey(7,false,2);EXPECT_EQ(n,0); client::input::VadTrigger v(.5f,10,[&](bool a,int64_t){n+=a?2:-2;});v.onLevel(1,0);v.onLevel(0,11);v.onLevel(0,21);EXPECT_EQ(n,0); }
TEST(Agc) { float x[2]={.1f,.1f}; client::audio::AgcProcessor a; a.process(x,2); EXPECT_NEAR(x[0],.18,.01); }
TEST(Handshake) {T t;P p;C c;client::ClientRuntime r(t,p,c,{});r.start();EXPECT_EQ(r.state(),client::ClientRuntime::State::Handshaking);t.h({},protocol::WelcomeMessage{1,48000,60,false,0});EXPECT_EQ(r.state(),client::ClientRuntime::State::Ready);r.setTalking(true);EXPECT_EQ(t.out.size(),2u);}

// 面板改配置必须同步进运行时：不同步时 submitVadPcm 会因内部 vadEnabled 仍为 false
// 直接丢帧，表现为「面板开了自动检测但完全没反应」。
TEST(ApplyConfigEnablesVad) {
    T t; P p; C c; client::ClientRuntime r(t, p, c, {});
    r.start();
    t.h({}, protocol::WelcomeMessage{1, 48000, 60, true, 0});
    EXPECT_EQ(r.state(), client::ClientRuntime::State::Ready);
    EXPECT_FALSE(r.vadActive());

    config::ClientConfig config;
    config.vadEnabled = true;
    r.applyConfig(config);
    EXPECT_TRUE(r.vadActive());

    // 自动检测已开：持续的能量必须被编码上行（PTT 按键此时不参与）。
    // 帧长以 Welcome 协商的 60ms 为准，一次喂满一帧；时钟同步推进，起始去抖才有时间差。
    std::vector<float> speech(48000 * 60 / 1000, 0.2F);
    std::size_t const before = t.out.size();
    for (int i = 0; i < 10; ++i) {
        c.t += 60;
        r.submitVadPcm(speech.data(), speech.size());
    }
    EXPECT_TRUE(t.out.size() > before);
    EXPECT_TRUE(r.talking());
}

// 未开启自动检测时 submitVadPcm 是空操作：不能靠它偷偷开始上行。
TEST(VadPcmInertWhenDisabled) {
    T t; P p; C c; client::ClientRuntime r(t, p, c, {});
    r.start();
    t.h({}, protocol::WelcomeMessage{1, 48000, 60, true, 0});
    std::size_t const before = t.out.size();
    std::vector<float> speech(48000 * 60 / 1000, 0.5F);
    for (int i = 0; i < 20; ++i) {
        c.t += 60;
        r.submitVadPcm(speech.data(), speech.size());
    }
    EXPECT_EQ(t.out.size(), before);
    EXPECT_FALSE(r.talking());
}

// PTT 按键在自动检测模式下不参与：否则松键事件会把检测器刚判出的「正在说话」强行关掉。
TEST(PttKeyIgnoredInVadMode) {
    T t; P p; C c;
    config::ClientConfig config;
    config.vadEnabled = true;
    client::ClientRuntime vad(t, p, c, config);
    vad.start();
    t.h({}, protocol::WelcomeMessage{1, 48000, 60, true, 0});
    EXPECT_FALSE(vad.setPttPressed(true));
    EXPECT_FALSE(vad.talking());

    client::ClientRuntime ptt(t, p, c, {});
    EXPECT_TRUE(ptt.setPttPressed(true));
}


// 迟到/重复的 Welcome 只能在握手阶段生效，避免会话中或未连接时被伪包拉进 Ready。
TEST(HandshakeIgnoresWelcomeOutsideHandshaking) {
    T t; P p; C c; client::ClientRuntime r(t, p, c, {});
    t.h({}, protocol::WelcomeMessage{1, 48000, 60, true, 0});
    EXPECT_EQ(r.state(), client::ClientRuntime::State::Stopped);
    r.start();
    t.h({}, protocol::WelcomeMessage{1, 48000, 60, true, 0});
    EXPECT_EQ(r.state(), client::ClientRuntime::State::Ready);
    t.h({}, protocol::WelcomeMessage{1, 48000, 60, true, 0}); // 重复 Welcome 不改状态
    EXPECT_EQ(r.state(), client::ClientRuntime::State::Ready);
}

// 握手完成前的业务消息（字幕/下行音频）必须丢弃；完成后正常接收。
TEST(BusinessMessagesIgnoredBeforeReady) {
    T t; P p; C c; client::ClientRuntime r(t, p, c, {});
    int subtitles = 0;
    r.setSttTextHandler([&](const protocol::SttTextMessage&) { ++subtitles; });
    protocol::SttTextMessage stt;
    stt.text = "hi";
    protocol::MixStreamMessage mix;
    mix.seq = 1;
    mix.opusData = {1, 2, 3};

    r.start(); // Handshaking
    t.h({}, stt);
    t.h({}, mix);
    EXPECT_EQ(subtitles, 0);
    EXPECT_EQ(r.receivedMixFrames(), 0u);

    t.h({}, protocol::WelcomeMessage{1, 48000, 60, true, 0});
    t.h({}, mix);
    EXPECT_EQ(r.receivedMixFrames(), 1u);
}

// 字幕按协商结果放行：未协商到字幕位则丢弃；旧服务端（未协商，0）回落 sttEnabled。
TEST(SubtitleFollowsNegotiatedCapability) {
    {
        T t; P p; C c; client::ClientRuntime r(t, p, c, {});
        r.start();
        t.h({}, protocol::WelcomeMessage{1, 48000, 60, true, protocol::CapabilityPtt});
        int subtitles = 0;
        r.setSttTextHandler([&](const protocol::SttTextMessage&) { ++subtitles; });
        protocol::SttTextMessage stt;
        stt.text = "hi";
        t.h({}, stt);
        EXPECT_EQ(subtitles, 0);
    }
    {
        T t; P p; C c; client::ClientRuntime r(t, p, c, {});
        r.start();
        t.h({}, protocol::WelcomeMessage{1, 48000, 60, true,
            protocol::CapabilityPtt | protocol::CapabilitySubtitle});
        int subtitles = 0;
        r.setSttTextHandler([&](const protocol::SttTextMessage&) { ++subtitles; });
        protocol::SttTextMessage stt;
        stt.text = "hi";
        t.h({}, stt);
        EXPECT_EQ(subtitles, 1);
    }
    {
        T t; P p; C c; client::ClientRuntime r(t, p, c, {});
        r.start();
        t.h({}, protocol::WelcomeMessage{1, 48000, 60, true, 0}); // 旧服务端
        int subtitles = 0;
        r.setSttTextHandler([&](const protocol::SttTextMessage&) { ++subtitles; });
        protocol::SttTextMessage stt;
        stt.text = "hi";
        t.h({}, stt);
        EXPECT_EQ(subtitles, 1);
    }
}
