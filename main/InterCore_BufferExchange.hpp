#pragma once

#include <atomic>

#include "Core0.hpp"

// =============================
// Shared Snapshot Payload
// =============================
struct CoeffAndRouteData {
    // Control-plane generated coefficient/routing image published to real-time core.
    InfrasonicCoefficients infraConfig;
    float eqMatrixCoeffs[6][31][5] = {{{0.0f}}};
    float mixRoutingWeights[6][10] = {{0.0f}};
};

// =============================
// Lock-Free Exchange
// =============================
class InterCoreBufferExchange {
public:
    void init() {
        // Set initial active image and reserve alternate buffer for writes.
        activePointer.store(&buffers[0], std::memory_order_release);
        writePointer = &buffers[1];
    }

    void commitNewParametersFromCore1(const CoeffAndRouteData& newData) {
        // Copy into write buffer then atomically swap active pointer.
        // Core0 readers always see a fully formed snapshot.
        *writePointer = newData;
        CoeffAndRouteData* previouslyActive = activePointer.exchange(writePointer, std::memory_order_acq_rel);
        writePointer = previouslyActive;
    }

    inline const CoeffAndRouteData* getActiveCore0Parameters() {
        // Acquire ensures Core0 reads a coherent image after pointer swap.
        return activePointer.load(std::memory_order_acquire);
    }

private:
    CoeffAndRouteData buffers[3];
    std::atomic<CoeffAndRouteData*> activePointer{nullptr};
    CoeffAndRouteData* writePointer = nullptr;
};
