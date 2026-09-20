#pragma once
#include <cstddef>
namespace vc::client::audio {
class AgcProcessor final { public: explicit AgcProcessor(float targetRms=0.18F, float maxGain=8.0F): target_(targetRms), maxGain_(maxGain) {} void process(float* samples, std::size_t count); float gain() const { return gain_; } private: float target_, maxGain_, gain_=1.0F; };
}
