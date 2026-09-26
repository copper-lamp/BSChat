#include "Harness.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "core/audio/AudioTypes.h"
#include "core/protocol/Message.h"
#include "server/stt/SherpaStt.h"

using namespace bsc::server;
using namespace bsc::protocol;
using namespace bsc::pipeline;
using namespace bsc::audio;

namespace {

PlayerId makePlayerId(uint8_t v) {
    PlayerId id{};
    id[0] = v;
    return id;
}

// 假引擎：partial/final 固定文本，用于验证流式管线（周期部分结果 + 最终结果）
class FakeStt : public SherpaStt {
public:
    FakeStt(std::string partial, std::string final, Options options = {})
    : SherpaStt(std::move(options), {}), partial_(std::move(partial)), final_(std::move(final)) {
        available_ = true;
    }

protected:
    std::string transcribePartial(const std::vector<float>&) override { return partial_; }
    std::string transcribeFinal(const std::vector<float>&) override { return final_; }

private:
    std::string partial_;
    std::string final_;
};

// 收集结果回调（worker 线程写，主线程读取前需等待）
struct Sink {
    mutable std::mutex mutex;
    std::vector<SttResult> results;
    std::condition_variable cv;

    void onResult(const SttResult& r) {
        {
            std::lock_guard lock(mutex);
            results.push_back(r);
        }
        cv.notify_all();
    }

    // 等待至少 n 条结果；返回实际条数
    size_t waitFor(size_t n, int timeoutMs = 2000) {
        std::unique_lock lock(mutex);
        cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                    [&] { return results.size() >= n; });
        return results.size();
    }

    size_t count() const {
        std::lock_guard lock(mutex);
        return results.size();
    }
};

} // namespace

TEST(stt_streaming_partial_then_final) {
    FakeStt stt("部分", "最终");
    Sink sink;
    stt.setResultSink([&](const SttResult& r) { sink.onResult(r); });

    stt.beginUtterance(makePlayerId(1));
    // 累积超过 partialIntervalMs（默认 600ms）→ 触发部分结果
    std::vector<float> chunk(static_cast<size_t>(48000 * 0.6), 0.0f); // 0.6s @48k
    stt.feedAudio(makePlayerId(1), chunk);
    EXPECT_TRUE(sink.waitFor(1) >= 1);
    {
        std::lock_guard lock(sink.mutex);
        EXPECT_FALSE(sink.results[0].isFinal);
        EXPECT_EQ(sink.results[0].text, "部分");
    }

    // 再喂一段仍出部分；End → 最终结果
    stt.feedAudio(makePlayerId(1), chunk);
    EXPECT_TRUE(sink.waitFor(2) >= 2);
    stt.endUtterance(makePlayerId(1));
    EXPECT_TRUE(sink.waitFor(3) >= 3);

    std::lock_guard lock(sink.mutex);
    EXPECT_TRUE(sink.results.back().isFinal);
    EXPECT_EQ(sink.results.back().text, "最终");
    EXPECT_EQ(sink.results.back().speakerId, makePlayerId(1));

    stt.shutdown();
}

TEST(stt_max_utterance_forced_split) {
    SherpaStt::Options opts;
    opts.maxUtteranceMs = 1200; // 2 × 0.6s 即超长
    FakeStt stt("", "最终", opts);
    Sink sink;
    stt.setResultSink([&](const SttResult& r) { sink.onResult(r); });

    stt.beginUtterance(makePlayerId(1));
    std::vector<float> chunk(static_cast<size_t>(48000 * 0.6), 0.0f);
    stt.feedAudio(makePlayerId(1), chunk);
    stt.feedAudio(makePlayerId(1), chunk); // 累积 1.2s ≥ 上限 → 强制产出最终并重置

    EXPECT_TRUE(sink.waitFor(1) >= 1);
    {
        std::lock_guard lock(sink.mutex);
        EXPECT_TRUE(sink.results.back().isFinal); // 切分产出的最终结果
        EXPECT_EQ(sink.results.back().text, "最终");
    }

    // 切分后上下文已重置：End 空句不产出新结果
    stt.endUtterance(makePlayerId(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(sink.count(), 1u);

    stt.shutdown();
}

TEST(stt_unavailable_drops_input) {
    SherpaStt::Options options;
    options.libraryPath = "missing-sherpa-onnx.dll";
    SherpaStt stt(options, {});
    EXPECT_FALSE(stt.available());

    std::atomic<bool> done{false};
    stt.setResultSink([&](const SttResult&) { done = true; });

    stt.beginUtterance(makePlayerId(2));
    std::vector<float> chunk(480, 0.0f);
    stt.feedAudio(makePlayerId(2), chunk);
    stt.endUtterance(makePlayerId(2));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(done.load());

    stt.shutdown();
}

TEST(stt_resample_48k_to_16k) {
    std::vector<float> pcm48k(4800, 0.0f); // 0.1s @48k
    for (size_t i = 0; i < pcm48k.size(); ++i) pcm48k[i] = 1.0f;

    auto out = SherpaStt::resample48kTo16k(pcm48k);
    EXPECT_EQ(out.size(), 1600u); // 3:1
    for (float v : out) EXPECT_NEAR(v, 1.0f, 1e-6f);
}

TEST(stt_begin_without_feed_ignores_end) {
    FakeStt stt("", "空句");
    Sink sink;
    stt.setResultSink([&](const SttResult& r) { sink.onResult(r); });

    // 未 begin 直接 end → 忽略（无上下文）
    stt.endUtterance(makePlayerId(3));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(sink.count(), 0u);

    stt.shutdown();
}
