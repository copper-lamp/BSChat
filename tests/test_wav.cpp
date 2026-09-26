#include "Harness.h"
#include "WavBytes.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "core/audio/WavFile.h"
#include "server/mixer/FilePlaybackSource.h"

using namespace bsc::audio;
using bsc::test::buildWavBytes;
using bsc::test::writeTempWav;

TEST(wav_parse_pcm16_mono) {
    std::vector<float> samples{-1.0F, -0.5F, 0.0F, 0.5F, 1.0F};
    auto bytes = buildWavBytes(48000, 1, false, samples);

    WavClip clip;
    std::string error;
    EXPECT_TRUE(parseWav(bytes.data(), bytes.size(), clip, error));
    EXPECT_EQ(clip.sampleRate, 48000);
    EXPECT_EQ(clip.channels, 1);
    EXPECT_EQ(clip.samples.size(), samples.size());
    EXPECT_NEAR(clip.samples[0], -1.0, 0.001);
    EXPECT_NEAR(clip.samples[2], 0.0, 0.001);
    EXPECT_NEAR(clip.samples[4], 1.0, 0.001);
}

TEST(wav_parse_float32_stereo_and_downmix) {
    // L=+0.5 / R=-0.5 交错两帧 → 下混应为 0
    std::vector<float> interleaved{0.5F, -0.5F, 0.25F, 0.25F};
    auto bytes = buildWavBytes(48000, 2, true, interleaved);

    WavClip clip;
    std::string error;
    EXPECT_TRUE(parseWav(bytes.data(), bytes.size(), clip, error));
    EXPECT_EQ(clip.channels, 2);
    EXPECT_EQ(clip.samples.size(), 4u);

    auto mono = toMonoResampled(clip, 48000);
    EXPECT_EQ(mono.size(), 2u);
    EXPECT_NEAR(mono[0], 0.0, 0.0001);
    EXPECT_NEAR(mono[1], 0.25, 0.0001);
}

TEST(wav_resample_linear_changes_length) {
    std::vector<float> samples(100, 0.25F);
    auto bytes = buildWavBytes(24000, 1, true, samples);

    WavClip clip;
    std::string error;
    EXPECT_TRUE(parseWav(bytes.data(), bytes.size(), clip, error));

    auto upsampled = toMonoResampled(clip, 48000);
    EXPECT_EQ(upsampled.size(), 200u);
    for (float v : upsampled) EXPECT_NEAR(v, 0.25, 0.001);

    auto same = toMonoResampled(clip, 24000);
    EXPECT_EQ(same.size(), 100u);
}

TEST(wav_rejects_invalid_stream) {
    const std::vector<uint8_t> garbage{'n', 'o', 'p', 'e', 0, 0, 0, 0, 0, 0, 0, 0};
    WavClip clip;
    std::string error;
    EXPECT_FALSE(parseWav(garbage.data(), garbage.size(), clip, error));
    EXPECT_FALSE(error.empty());
}

TEST(file_playback_source_pads_last_frame_then_finishes) {
    std::vector<float> samples(100, 0.5F);
    auto bytes = buildWavBytes(48000, 1, true, samples);
    auto path = writeTempWav(bytes, "bschat-test-playback.wav");

    bsc::server::FilePlaybackSource source;
    std::string error;
    EXPECT_TRUE(source.load(path, 48000, error));
    EXPECT_TRUE(source.active());
    EXPECT_EQ(source.remainingSamples(), 100u);

    auto first = source.nextFrame(60);
    EXPECT_EQ(first.size(), 60u);
    EXPECT_NEAR(first[0], 0.5, 0.001);
    EXPECT_TRUE(source.active());

    auto second = source.nextFrame(60);
    EXPECT_EQ(second.size(), 60u); // 40 个有效样本 + 20 个静音
    EXPECT_NEAR(second[39], 0.5, 0.001);
    EXPECT_NEAR(second[59], 0.0, 0.001);
    EXPECT_FALSE(source.active());

    EXPECT_TRUE(source.nextFrame(60).empty());

    std::filesystem::remove(path);
}
