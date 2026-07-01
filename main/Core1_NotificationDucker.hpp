#pragma once

#include <algorithm>
#include <cmath>

// =============================
// Notification Ducker
// =============================
class Core1NotificationDucker {
public:
    // Envelope follower state from notification path and corresponding music attenuation scalar.
    float envelopeValue = 0.0f;
    float currentMusicGainScalar = 1.0f;

    // Attack/release tuned for fast duck-on and slower recovery.
    const float attackCoef = 1.0f - std::exp(-1.0f / (192000.0f * 0.005f));
    const float releaseCoef = 1.0f - std::exp(-1.0f / (192000.0f * 0.300f));

    // =============================
    // Per-Frame Update
    // =============================
    inline void processNotificationFrame(float notifL,
                                         float notifR,
                                         float inputPreGain,
                                         float& processedL,
                                         float& processedR) {
                        // Apply SFX pre-gain before detection and feed-through.
        processedL = notifL * inputPreGain;
        processedR = notifR * inputPreGain;

                        // Peak detector for notification salience.
        const float currentPeakEnergy = std::max(std::fabs(processedL), std::fabs(processedR));

        if (currentPeakEnergy > envelopeValue) {
            envelopeValue += attackCoef * (currentPeakEnergy - envelopeValue);
        } else {
            envelopeValue += releaseCoef * (currentPeakEnergy - envelopeValue);
        }

        // Translate envelope to attenuation curve with floor to keep music present.
        if (envelopeValue > 0.01f) {
            float targetGain = 1.0f - (envelopeValue * 2.5f);
            targetGain = std::max(0.10f, targetGain);
            currentMusicGainScalar += 0.01f * (targetGain - currentMusicGainScalar);
        } else {
            currentMusicGainScalar += 0.001f * (1.0f - currentMusicGainScalar);
        }
    }
};
