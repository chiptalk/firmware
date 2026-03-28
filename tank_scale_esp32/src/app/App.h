#pragma once
#include <Arduino.h>
#include "AppState.h"
#include "WifiManager.h"
#include "WebServerManager.h"
#include "DisplayManager.h"
#include "Button.h"
#include "ScaleReader.h"
#include "TankTelemetry.h"
#include "MqttOutbox.h"
#include "ClaimManager.h"
#include "iot/AwsIotProvisioner.h"

class App {
public:
  void begin();
  void loop();

private:
  AppState state_;
  WifiManager wifi_;
  WebServerManager web_;
  DisplayManager display_;
  Button resetBtn_;
  ScaleReader scale_;
  
  uint32_t lastUiMs_ = 0;

  // Robustness additions
  uint32_t staStartMs_ = 0;
  bool staFallbackDone_ = false;

  // Button hold tracking (anytime clear)
  uint32_t bootHoldStartMs_ = 0;

  // BOOT short-press cycles the screen; long-press clears Wi-Fi.
  bool prevPressed_ = false;
  uint32_t pressStartMs_ = 0;

  // Serial emergency
  String serialLine_;

  bool timeSynced_ = false; 
  TankTelemetryConfig telemetryCfg_{};
  TankStateTracker tankTracker_{};
  uint32_t lastTelemetryPublishMs_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  uint32_t telemetrySeq_ = 0;
  uint32_t heartbeatSeq_ = 0;
  uint32_t eventSeq_ = 0;
  uint32_t cycleSeq_ = 0;
  MqttOutbox outbox_{};
  MqttOutboxConfig outboxCfg_{};
  bool displaySleepEnabled_ = true;
  uint32_t displaySleepIdleMs_ = 120000; // 2 minutes
  uint32_t lastDisplayActivityMs_ = 0;
  bool lastScaleActive_ = false;
  String lastScaleReadingForWake_;
  bool cycleActive_ = false;
  TankState cycleState_ = TankState::UNKNOWN; // filling/discharging only
  String cycleId_;
  time_t cycleStartTs_ = 0;
  float cycleStartWeight_ = 0.0f;
  ClaimManager claim_;
  ClaimManagerConfig claimCfg_{};
  bool mqttWasReady_ = false;
  uint32_t mqttReconnectBackoffMs_ = 0;
  uint32_t mqttNextReconnectAtMs_ = 0;

  void enterApSetup_();
  void enterStaNormal_();

  void handleSerial_();
  void updateUi_();
  void syncTime_();

  void nextScreen_();
  void noteDisplayActivity_(uint32_t now);
  void updateDisplaySleep_(uint32_t now, bool scaleActive);
  void handleFlowEventsOnStateChange_(TankState newState, float currentWeight);
  void publishEvent_(JsonDocument& doc);
  void processClaimPolling_(uint32_t now);
  float adjustedWeight_(float rawWeight) const;
  String adjustedWeightString_() const;
  String formatSignedWeight_(float weight) const;
  void handleMqttMessage_(const String& topic, const String& payload);
  static void onMqttMessageStatic_(const String& topic, const String& payload, void* ctx);

  AwsIotProvisioner iot_;

};
