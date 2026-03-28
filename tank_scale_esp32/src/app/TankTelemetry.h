#pragma once

#include <Arduino.h>
#include <math.h>

enum class TankState : uint8_t {
  UNKNOWN = 0,
  IDLE,
  FILLING,
  DISCHARGING,
  CLEANING
};

inline const char* tankStateName(TankState s) {
  switch (s) {
    case TankState::IDLE: return "idle";
    case TankState::FILLING: return "filling";
    case TankState::DISCHARGING: return "discharging";
    case TankState::CLEANING: return "cleaning";
    default: return "unknown";
  }
}

struct TankTelemetryConfig {
  uint32_t stateEvalPeriodMs = 1000;
  float readingMultiplier = 1.0f;

  // Rate-of-change thresholds in "scale units per second".
  float idleRateThreshold = 0.05f;
  float fillRateThreshold = 0.10f;
  float dischargeRateThreshold = 0.10f;

  // Transition confirmation (anti-noise / anti-flapping).
  uint32_t flowConfirmMs = 8000;   // filling/discharging must persist this long
  uint32_t idleConfirmMs = 12000;  // idle must persist this long
  float flowMinDelta = 2.0f;       // minimum total delta before confirming flow state

  // Cleaning detection (initial heuristic, tune with real data)
  bool cleaningDetectEnabled = true;
  uint32_t cleaningWindowMs = 20000;      // rolling observation window
  uint8_t cleaningMinSignFlips = 4;       // +/- direction changes in window
  float cleaningMinAbsDelta = 3.0f;       // cumulative |delta| in window
  uint32_t cleaningConfirmMs = 10000;     // state confirmation time

  uint32_t telemetryIdleMs = 30000;
  uint32_t telemetryFillingMs = 5000;
  uint32_t telemetryDischargingMs = 2000;
  uint32_t telemetryCleaningMs = 3000;
  uint32_t telemetryUnknownMs = 5000;
  uint32_t heartbeatPeriodMs = 60000;

  uint32_t scaleActiveTimeoutMs = 10000;
};

class TankStateTracker {
public:
  void reset() {
    seeded_ = false;
    state_ = TankState::UNKNOWN;
    pendingState_ = TankState::UNKNOWN;
    pendingStartMs_ = 0;
    pendingStartWeight_ = 0.0f;
    lastEvalMs_ = 0;
    lastWeight_ = 0.0f;
    lastRate_ = 0.0f;
    cleaningWindowStartMs_ = 0;
    cleaningSignFlips_ = 0;
    cleaningAbsDelta_ = 0.0f;
    lastNonIdleSign_ = 0;
  }

  // Returns true if state changed.
  bool update(uint32_t nowMs, const TankTelemetryConfig& cfg, float weight) {
    if (!seeded_) {
      seeded_ = true;
      lastEvalMs_ = nowMs;
      lastWeight_ = weight;
      state_ = TankState::UNKNOWN;
      return false;
    }

    if (nowMs - lastEvalMs_ < cfg.stateEvalPeriodMs) return false;

    const float dt = (nowMs - lastEvalMs_) / 1000.0f;
    if (dt <= 0.0f) return false;

    const float delta = (weight - lastWeight_);
    lastRate_ = delta / dt;
    updateCleaningWindow_(nowMs, cfg, delta);
    lastEvalMs_ = nowMs;
    lastWeight_ = weight;

    TankState candidate = classify_(cfg);
    if (candidate == state_) {
      // Stable in current state; clear pending transition.
      pendingState_ = state_;
      pendingStartMs_ = nowMs;
      pendingStartWeight_ = weight;
      return false;
    }

    if (candidate == TankState::UNKNOWN) {
      return false;
    }

    if (candidate != pendingState_) {
      pendingState_ = candidate;
      pendingStartMs_ = nowMs;
      pendingStartWeight_ = weight;
      return false;
    }

    const uint32_t pendingMs = nowMs - pendingStartMs_;
    const float pendingDeltaAbs = fabsf(weight - pendingStartWeight_);

    if (candidate == TankState::IDLE) {
      if (pendingMs < cfg.idleConfirmMs) return false;
    } else if (candidate == TankState::CLEANING) {
      if (pendingMs < cfg.cleaningConfirmMs) return false;
    } else {
      if (pendingMs < cfg.flowConfirmMs) return false;
      if (pendingDeltaAbs < cfg.flowMinDelta) return false;
    }

    state_ = candidate;
    return true;
  }

  TankState state() const { return state_; }
  float lastRate() const { return lastRate_; }
  bool seeded() const { return seeded_; }

private:
  void updateCleaningWindow_(uint32_t nowMs, const TankTelemetryConfig& cfg, float delta) {
    if (!cfg.cleaningDetectEnabled) {
      cleaningWindowStartMs_ = nowMs;
      cleaningSignFlips_ = 0;
      cleaningAbsDelta_ = 0.0f;
      lastNonIdleSign_ = 0;
      return;
    }

    if (cleaningWindowStartMs_ == 0) {
      cleaningWindowStartMs_ = nowMs;
    }
    if (nowMs - cleaningWindowStartMs_ > cfg.cleaningWindowMs) {
      cleaningWindowStartMs_ = nowMs;
      cleaningSignFlips_ = 0;
      cleaningAbsDelta_ = 0.0f;
      // Keep lastNonIdleSign_ to preserve recent direction context.
    }

    cleaningAbsDelta_ += fabsf(delta);

    int8_t sign = 0;
    if (delta > 0.0f) sign = 1;
    else if (delta < 0.0f) sign = -1;

    if (sign != 0) {
      if (lastNonIdleSign_ != 0 && sign != lastNonIdleSign_) {
        if (cleaningSignFlips_ < 255) cleaningSignFlips_++;
      }
      lastNonIdleSign_ = sign;
    }
  }

  TankState classify_(const TankTelemetryConfig& cfg) const {
    if (fabsf(lastRate_) <= cfg.idleRateThreshold) {
      return TankState::IDLE;
    }
    if (lastRate_ >= cfg.fillRateThreshold) {
      return TankState::FILLING;
    }
    if (lastRate_ <= -cfg.dischargeRateThreshold) {
      return TankState::DISCHARGING;
    }
    if (cfg.cleaningDetectEnabled &&
        cleaningSignFlips_ >= cfg.cleaningMinSignFlips &&
        cleaningAbsDelta_ >= cfg.cleaningMinAbsDelta) {
      return TankState::CLEANING;
    }
    // Deadband between idle and flow thresholds.
    return TankState::UNKNOWN;
  }

  bool seeded_ = false;
  TankState state_ = TankState::UNKNOWN;
  TankState pendingState_ = TankState::UNKNOWN;
  uint32_t pendingStartMs_ = 0;
  float pendingStartWeight_ = 0.0f;
  uint32_t lastEvalMs_ = 0;
  float lastWeight_ = 0.0f;
  float lastRate_ = 0.0f;
  uint32_t cleaningWindowStartMs_ = 0;
  uint8_t cleaningSignFlips_ = 0;
  float cleaningAbsDelta_ = 0.0f;
  int8_t lastNonIdleSign_ = 0;
};

inline uint32_t telemetryPeriodForState(const TankTelemetryConfig& cfg, TankState s, bool scaleActive) {
  if (!scaleActive) return cfg.telemetryUnknownMs;
  switch (s) {
    case TankState::IDLE: return cfg.telemetryIdleMs;
    case TankState::FILLING: return cfg.telemetryFillingMs;
    case TankState::DISCHARGING: return cfg.telemetryDischargingMs;
    case TankState::CLEANING: return cfg.telemetryCleaningMs;
    default: return cfg.telemetryUnknownMs;
  }
}
