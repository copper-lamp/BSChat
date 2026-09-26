#include "Harness.h"

#include <cstdint>
#include <vector>

#include "core/audio/JitterBuffer.h"
#include "core/protocol/Message.h"
#include "server/session/PlayerSession.h"

using namespace bsc::audio;
using namespace bsc::protocol;
using namespace bsc::server;

namespace {

PlayerId makePlayerId(uint8_t value) {
    PlayerId id{};
    id[0] = value;
    return id;
}

AudioDataMessage makeAudio(uint64_t seq) {
    AudioDataMessage message;
    message.seq = seq;
    message.opusData = {1, 2, 3};
    return message;
}

}

TEST(jitter_buffer_reports_duplicate_and_late_frames) {
    JitterBuffer buffer;
    EXPECT_EQ(buffer.push(1, {1}, 0), JitterBuffer::PushResult::Accepted);
    EXPECT_EQ(buffer.push(1, {2}, 1), JitterBuffer::PushResult::Duplicate);
    EXPECT_EQ(buffer.push(0, {3}, 2), JitterBuffer::PushResult::Late);
}

TEST(jitter_buffer_reports_eviction_without_rejecting_new_frame) {
    JitterBuffer::Options options;
    options.maxDepthFrames = 1;
    JitterBuffer buffer(options);
    EXPECT_EQ(buffer.push(1, {1}, 0), JitterBuffer::PushResult::Accepted);
    EXPECT_EQ(buffer.push(2, {2}, 1), JitterBuffer::PushResult::AcceptedWithEviction);
    EXPECT_EQ(buffer.size(), 1u);
}

TEST(player_session_reports_rate_limit_separately) {
    PlayerSession::Options options;
    options.maxFramesPerSecond = 1;
    options.maxDepthFrames = 10;
    PlayerSession session(makePlayerId(9), options);
    EXPECT_EQ(session.pushAudio(makeAudio(1), 1000), PlayerSession::PushResult::Accepted);
    EXPECT_EQ(session.pushAudio(makeAudio(2), 1001), PlayerSession::PushResult::RateLimited);
}
