#pragma once

#include <algorithm>

// Pink-noise gain/trim stage used before channel source replacement.
class Core1PinkNoiseGainTrim {
public:
    void setGain(float gainLinear) {
        gain = std::max(0.0f, std::min(2.0f, gainLinear));
    }

    float process(float inSample) const {
        return inSample * gain;
    }

private:
    float gain = 1.0f;
};
