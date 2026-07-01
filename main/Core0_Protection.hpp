#pragma once

#include <cstdint>

// =============================
// Selector Data Model
// =============================
// Physical selector abstraction:
// - PROTECTED: normal guarded operation.
// - SERVICE_BYPASS: maintenance/debug bypass mode, software-gated by timeout rules.
enum class ProtectionSelectorMode : uint8_t {
    PROTECTED = 0,
    SERVICE_BYPASS = 1
};

struct ProtectionSelectorState {
    // Debounced stable mode consumed by runtime permission checks.
    ProtectionSelectorMode debouncedMode = ProtectionSelectorMode::PROTECTED;
    // Candidate mode currently being observed during debounce window.
    ProtectionSelectorMode candidateMode = ProtectionSelectorMode::PROTECTED;
    // Number of consecutive samples with identical candidate state.
    uint32_t stableCount = 0;
    // True only when differential pair is electrically valid (A != B).
    bool validPair = false;
    // True when candidate has reached requiredStableCount and is promoted to debouncedMode.
    bool isStable = false;
};

struct ProtectionSelectorConfig {
    // Sampling period for selector update loop.
    uint32_t samplePeriodMs = 10;
    // Debounce horizon in milliseconds.
    uint32_t softwareDebounceMs = 500;
};

// =============================
// Decode And Validation
// =============================
// Differential pair is valid only when lines disagree.
// If both lines match, wiring fault/indeterminate state is assumed.
inline bool selectorPairValid(bool lineA, bool lineB) {
    return (lineA != lineB);
}

inline ProtectionSelectorMode decodeSelectorMode(bool lineA, bool lineB) {
    // Service bypass: A=1, B=0. Protected: A=0, B=1.
    return (lineA && !lineB) ? ProtectionSelectorMode::SERVICE_BYPASS : ProtectionSelectorMode::PROTECTED;
}

// =============================
// Debounce State Update
// =============================
inline void updateProtectionSelector(ProtectionSelectorState& state,
                                     const ProtectionSelectorConfig& cfg,
                                     bool lineA,
                                     bool lineB) {
    // Step 1: validate raw electrical state.
    const bool pairIsValid = selectorPairValid(lineA, lineB);
    state.validPair = pairIsValid;

    if (!pairIsValid) {
        // Invalid pair forces fail-safe protected mode and resets debounce history.
        state.debouncedMode = ProtectionSelectorMode::PROTECTED;
        state.candidateMode = ProtectionSelectorMode::PROTECTED;
        state.stableCount = 0;
        state.isStable = false;
        return;
    }

    // Step 2: decode instantaneous state and debounce transitions.
    const ProtectionSelectorMode now = decodeSelectorMode(lineA, lineB);
    if (now != state.candidateMode) {
        // Edge detected: restart debounce counter with new candidate.
        state.candidateMode = now;
        state.stableCount = 1;
        state.isStable = false;
        return;
    }

    // Round up to guarantee requested debounce duration.
    const uint32_t requiredStableCount =
        (cfg.samplePeriodMs == 0) ? 1 : ((cfg.softwareDebounceMs + cfg.samplePeriodMs - 1) / cfg.samplePeriodMs);

    if (state.stableCount < requiredStableCount) {
        ++state.stableCount;
    }

    state.isStable = (state.stableCount >= requiredStableCount);
    if (state.isStable) {
        state.debouncedMode = state.candidateMode;
    }
}

// =============================
// Runtime Policy Gate
// =============================
inline bool protectionBypassPermitted(const ProtectionSelectorState& state,
                                      bool serviceHardwarePresent,
                                      bool serviceTimeoutExpired) {
    // Runtime bypass is permitted only when all hardware and policy checks pass.
    if (!serviceHardwarePresent || serviceTimeoutExpired) {
        return false;
    }
    if (!state.validPair || !state.isStable) {
        return false;
    }
    return state.debouncedMode == ProtectionSelectorMode::SERVICE_BYPASS;
}
