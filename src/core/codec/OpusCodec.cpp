#include "core/codec/OpusCodec.h"

#include <algorithm>
#include <stdexcept>

#include <opus/opus.h>

namespace vc::codec {

struct OpusEncoder::Impl {
    ::OpusEncoder* enc = nullptr; // opus.h 的 C 类型（全局命名空间）
    int frameSamples = 0;
};

OpusEncoder::OpusEncoder(int sampleRate, int channels, int frameSamples, int bitrateKbps, int complexity)
    : impl_(new Impl) {
    int error = OPUS_OK;
    impl_->enc = opus_encoder_create(sampleRate, channels, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK) {
        delete impl_;
        throw std::runtime_error("opus_encoder_create failed: " + std::string(opus_strerror(error)));
    }
    impl_->frameSamples = frameSamples;
    opus_encoder_ctl(impl_->enc, OPUS_SET_BITRATE(bitrateKbps * 1000));
    opus_encoder_ctl(impl_->enc, OPUS_SET_COMPLEXITY(std::clamp(complexity, 0, 10)));
    opus_encoder_ctl(impl_->enc, OPUS_SET_DTX(1));  // 静音时输出超低码率包，配合"静音不推流"
    opus_encoder_ctl(impl_->enc, OPUS_SET_VBR(1));
    opus_encoder_ctl(impl_->enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
}

OpusEncoder::~OpusEncoder() {
    if (impl_) {
        if (impl_->enc) opus_encoder_destroy(impl_->enc);
        delete impl_;
    }
}

int OpusEncoder::encode(const float* pcm, uint8_t* out, size_t outCapacity) {
    int n = opus_encode_float(impl_->enc, pcm, impl_->frameSamples, out, static_cast<opus_int32>(outCapacity));
    if (n < 0) return n; // 负值 = 错误码
    if (n <= 2) return 0; // DTX 静音包（RFC 6716 定义 1~2 字节）→ 视为无有效数据，配合"静音不推流"
    return n;
}

void OpusEncoder::reset() {
    if (impl_ && impl_->enc) opus_encoder_ctl(impl_->enc, OPUS_RESET_STATE);
}

int OpusEncoder::maxPacketSize() const {
    // 1275 字节是 Opus 单帧最大包长（48kHz 下）
    return 1275;
}

struct OpusDecoder::Impl {
    ::OpusDecoder* dec = nullptr; // opus.h 的 C 类型（全局命名空间）
    int frameSamples = 0;
};

OpusDecoder::OpusDecoder(int sampleRate, int channels, int frameSamples)
    : impl_(new Impl) {
    int error = OPUS_OK;
    impl_->dec = opus_decoder_create(sampleRate, channels, &error);
    if (error != OPUS_OK) {
        delete impl_;
        throw std::runtime_error("opus_decoder_create failed: " + std::string(opus_strerror(error)));
    }
    impl_->frameSamples = frameSamples;
}

OpusDecoder::~OpusDecoder() {
    if (impl_) {
        if (impl_->dec) opus_decoder_destroy(impl_->dec);
        delete impl_;
    }
}

int OpusDecoder::decode(const uint8_t* data, size_t size, float* pcm, size_t capacity) {
    if (capacity < static_cast<size_t>(impl_->frameSamples)) return -1;
    int n = opus_decode_float(
        impl_->dec,
        data,
        data ? static_cast<opus_int32>(size) : 0,
        pcm,
        impl_->frameSamples,
        0 // 非 FEC
    );
    return n;
}

void OpusDecoder::reset() {
    if (impl_ && impl_->dec) opus_decoder_ctl(impl_->dec, OPUS_RESET_STATE);
}

} // namespace vc::codec
