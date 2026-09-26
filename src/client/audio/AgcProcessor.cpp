#include "client/audio/AgcProcessor.h"
#include <algorithm>
#include <cmath>
namespace bsc::client::audio { void AgcProcessor::process(float* s, std::size_t n) { if(!s||!n) return; double sum=0; for(size_t i=0;i<n;++i) sum += double(s[i])*s[i]; float rms=std::sqrt(float(sum/n)); if(rms > 1e-5F) gain_=std::clamp(target_/rms, 0.1F, maxGain_); for(size_t i=0;i<n;++i) s[i]=std::clamp(s[i]*gain_, -1.0F, 1.0F); } }
