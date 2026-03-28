// App.cpp

#include "App.h"
#include "Log.h"
#include <time.h>
#include <LittleFS.h>
#include <esp_system.h>

#include "iot/AwsIotProvisioner.h"
#include "iot/IotConfig.h"
#include "iot/Creds.h"
#include <math.h>

#ifndef MQTT_DEBUG_MODE
#define MQTT_DEBUG_MODE 0
#endif

static constexpr size_t MQTT_OUTBOX_CAPACITY_DEFAULT = 40;
static constexpr uint8_t MQTT_OUTBOX_HEARTBEAT_SKIP_PCT = 75;
static constexpr uint32_t MQTT_RECONNECT_MIN_MS = 10000;
static constexpr uint32_t MQTT_RECONNECT_MAX_MS = 60000;
static constexpr uint32_t MQTT_RECONNECT_JITTER_MS = 3000;

static constexpr uint8_t  PIN_BOOT_BUTTON   = 0;     // BOOT is GPIO0 on most ESP32 dev boards
static constexpr uint32_t UI_PERIOD_MS      = 250;
static constexpr uint32_t BOOT_HOLD_CLEARMS = 2000;  // hold 2s to clear creds any time
static constexpr uint32_t STA_TIMEOUT_MS    = 25000; // after 25s fail -> AP setup

// RS232 (via MAX3232) -> use UART2. Adjust to your wiring.
static constexpr int PIN_RS232_RX = 16;  // ESP32 RX2  (connect to MAX3232 R1OUT)
static constexpr int PIN_RS232_TX = 17;  // ESP32 TX2  (connect to MAX3232 T1IN)
static constexpr uint32_t RS232_BAUD = 9600; // match T7E P3 setting

// FIX: do not redeclare inside loop()
static bool ntpStarted = false;
// FIX: must persist across loop() calls
static bool iotStarted = false;

void App::begin() {
  Log::begin(115200);
  Log::i("Booting...");

  if (!LittleFS.begin(true)) {
    Log::e("LittleFS mount failed");
  }

  resetBtn_.begin(PIN_BOOT_BUTTON, true /*pullup*/, true /*activeLow*/);

  display_.begin();
  display_.showBoot("Starting...");

  // Start scale serial reader (non-blocking)
  scale_.begin(Serial2, PIN_RS232_RX, PIN_RS232_TX, RS232_BAUD);
  //scale_.setDebug(&Serial);          // prints "FRAME: =0.0000-"
  scale_.setReverseDigits(true);

  wifi_.begin();
  web_.attachTelemetryConfig(telemetryCfg_);

  // Claim polling policy:
  // - poll on boot
  // - poll on MQTT reconnect
  // - periodic every 12h (can be disabled via periodicPollingEnabled)
  claimCfg_.periodicPollingEnabled = true;
  claimCfg_.periodicPollMs = 12UL * 60UL * 60UL * 1000UL;
  claim_.begin(deviceId(), claimCfg_);
  iot_.setTenantId(claim_.tenantId());

  if (!wifi_.hasCredentials()) {
    enterApSetup_();
  } else {
    enterStaNormal_();
  }

  lastUiMs_ = millis();
  timeSynced_ = false;

  // reset statics on boot (safe even if already false)
  ntpStarted = false;
  iotStarted = false;
  lastTelemetryPublishMs_ = 0;
  lastHeartbeatMs_ = 0;
  telemetrySeq_ = 0;
  heartbeatSeq_ = 0;
  eventSeq_ = 0;
  cycleSeq_ = 0;
  tankTracker_.reset();
  outboxCfg_.capacity = MQTT_OUTBOX_CAPACITY_DEFAULT;
  outboxCfg_.dropOldestWhenFull = true;
  outbox_.configure(outboxCfg_);
  outbox_.reset();
  lastDisplayActivityMs_ = millis();
  lastScaleActive_ = false;
  lastScaleReadingForWake_.clear();
  cycleActive_ = false;
  cycleState_ = TankState::UNKNOWN;
  cycleId_.clear();
  cycleStartTs_ = 0;
  cycleStartWeight_ = 0.0f;
  mqttWasReady_ = false;
  mqttReconnectBackoffMs_ = 0;
  mqttNextReconnectAtMs_ = 0;
  display_.wake();
}

void App::loop() {
  resetBtn_.update();
  handleSerial_();
  wifi_.loop();
  web_.loop();
  scale_.loop();
  iot_.loop();

  // BOOT button:
  // - short click (release): cycle WIFI -> QR -> SCALE
  // - long press: clear Wi-Fi + reboot (anytime)
  const bool pressed = resetBtn_.isPressed();
  const uint32_t now = millis();
  static bool longPressFired = false;

  float currentWeight = 0.0f;
  if (scale_.lastWeightValue(currentWeight)) {
    currentWeight = adjustedWeight_(currentWeight);
    const String& readingNow = scale_.lastPretty();
    if (display_.isSleeping() && readingNow.length() && readingNow != lastScaleReadingForWake_) {
      noteDisplayActivity_(now); // wake on slightest parsed reading change while asleep
    }
    if (readingNow.length()) {
      lastScaleReadingForWake_ = readingNow;
    }

    if (tankTracker_.update(now, telemetryCfg_, currentWeight)) {
      Log::i(String("Tank state: ") + tankStateName(tankTracker_.state()) +
             " rate=" + String(tankTracker_.lastRate(), 3));
      handleFlowEventsOnStateChange_(tankTracker_.state(), currentWeight);
      noteDisplayActivity_(now);
    }
  }

  // ---- NTP sync (non-blocking) ----
  if (state_.mode == AppMode::STA_NORMAL && wifi_.isConnected() && !timeSynced_) {
    if (!ntpStarted) {
      syncTime_();          // calls configTzTime once
      ntpStarted = true;
    }

    time_t tnow = time(nullptr);
    if (tnow > 1700000000) {
      timeSynced_ = true;
      state_.timeSynced = true;
      Log::i(String("Epoch now(valid): ") + (uint32_t)tnow);
    } else {
      static uint32_t lastLog = 0;
      if (millis() - lastLog > 1000) {
        lastLog = millis();
        Log::w(String("Waiting for NTP... epoch=") + (uint32_t)tnow);
      }
    }
  }

  // ---- IoT start (exactly once) ----
  if (state_.mode == AppMode::STA_NORMAL && wifi_.isConnected() && timeSynced_ && !iotStarted) {
    // 1) init store + see if device identity exists
    iot_.begin(AWS_ROOT_CA_PEM, AWS_CLAIM_CERT_PEM, AWS_CLAIM_PRIVATE_KEY_PEM);

    // 2) if already provisioned, connect using device cert; else provision then connect
    if (iot_.isProvisioned()) {
      Log::i("IDENTITY: OK");
      Log::i("AWS IoT connect (DEVICE)");
      bool ok = iot_.connectDeviceMqtt(AWS_IOT_ENDPOINT, AWS_IOT_PORT, deviceId().c_str());
      Log::i(String("DEVICE MQTT: ") + (ok ? "OK" : "FAIL: " + iot_.status()));
      if (!ok) {
        // Recover automatically from stale/invalid stored device certs.
        Log::w("DEVICE MQTT failed with stored identity; clearing identity and retrying provisioning...");
        iot_.clearIdentity();
        bool provOk = iot_.runProvisioning(AWS_IOT_ENDPOINT, AWS_IOT_PORT, deviceId(), PROV_TEMPLATE);
        Log::i(String("Provisioning result: ") + (provOk ? "OK" : "FAIL: " + iot_.status()));
        if (provOk) {
          Log::i("AWS IoT connect (DEVICE)");
          bool ok2 = iot_.connectDeviceMqtt(AWS_IOT_ENDPOINT, AWS_IOT_PORT, deviceId().c_str());
          Log::i(String("DEVICE MQTT: ") + (ok2 ? "OK" : "FAIL: " + iot_.status()));
        }
      }
    } else {
      Log::w("Provisioning...");
      bool ok = iot_.runProvisioning(AWS_IOT_ENDPOINT, AWS_IOT_PORT, deviceId(), PROV_TEMPLATE);
      Log::i(String("Provisioning result: ") + (ok ? "OK" : "FAIL: " + iot_.status()));
      if (ok) {
        Log::i("AWS IoT connect (DEVICE)");
        bool ok2 = iot_.connectDeviceMqtt(AWS_IOT_ENDPOINT, AWS_IOT_PORT, deviceId().c_str());
        Log::i(String("DEVICE MQTT: ") + (ok2 ? "OK" : "FAIL: " + iot_.status()));
      }
    }

    iotStarted = true;
    mqttReconnectBackoffMs_ = 0;
    mqttNextReconnectAtMs_ = 0;
  }

  // ---- MQTT reconnect loop with bounded exponential backoff ----
  if (state_.mode == AppMode::STA_NORMAL && wifi_.isConnected() && timeSynced_ && iotStarted && !iot_.isReady()) {
    if (now >= mqttNextReconnectAtMs_) {
      bool ok = iot_.connectDeviceMqtt(AWS_IOT_ENDPOINT, AWS_IOT_PORT, deviceId().c_str());
#if MQTT_DEBUG_MODE
      Log::i(String("DEVICE MQTT reconnect: ") + (ok ? "OK" : "FAIL: " + iot_.status()));
#endif
      if (ok) {
        mqttReconnectBackoffMs_ = 0;
        mqttNextReconnectAtMs_ = 0;
      } else {
        mqttReconnectBackoffMs_ = (mqttReconnectBackoffMs_ == 0)
            ? MQTT_RECONNECT_MIN_MS
            : min(MQTT_RECONNECT_MAX_MS, mqttReconnectBackoffMs_ * 2U);
        const uint32_t jitter = (uint32_t)(esp_random() % (MQTT_RECONNECT_JITTER_MS + 1U));
        mqttNextReconnectAtMs_ = now + mqttReconnectBackoffMs_ + jitter;
      }
    }
  }

  // ---- Drain local MQTT outbox when connected ----
  if (iot_.isReady() && outbox_.hasItems()) {
    uint8_t drained = 0;
    while (drained < 3 && outbox_.hasItems()) {
      const MqttOutboxItem* item = outbox_.front();
      if (!item) break;
      if (!iot_.publishRaw(item->topic, item->payload)) {
        break; // stop draining if publish fails again
      }
#if MQTT_DEBUG_MODE
      Log::i(String("MQTT outbox sent: ") + item->topic);
#endif
      outbox_.popFront();
      drained++;
    }
  }

  // Aggregate comm activity (HTTP is written directly by WebServerManager, MQTT via iot_).
  if (iot_.lastMqttTxMs() > state_.commTxMs) state_.commTxMs = iot_.lastMqttTxMs();
  if (iot_.lastMqttRxMs() > state_.commRxMs) state_.commRxMs = iot_.lastMqttRxMs();

  const bool mqttReadyNow = iot_.isReady();
  if (mqttReadyNow && !mqttWasReady_) {
    iot_.setMessageHandler(&App::onMqttMessageStatic_, this);
    claim_.onMqttConnected(now);
  } else if (!mqttReadyNow && mqttWasReady_) {
    claim_.onMqttDisconnected();
  }
  mqttWasReady_ = mqttReadyNow;
  processClaimPolling_(now);

  const bool scaleActive =
      scale_.hasReading() && (millis() - scale_.lastRxMs() < telemetryCfg_.scaleActiveTimeoutMs);
  if (scaleActive != lastScaleActive_) {
    lastScaleActive_ = scaleActive;
    noteDisplayActivity_(now);
  }
  updateDisplaySleep_(now, scaleActive);
  const bool telemetryEligible = (state_.mode == AppMode::STA_NORMAL && timeSynced_);

  // ---- Telemetry publish (adaptive by tank state) ----
  const uint32_t telemetryPeriodMs = telemetryPeriodForState(telemetryCfg_, tankTracker_.state(), scaleActive);
  if (telemetryEligible && millis() - lastTelemetryPublishMs_ >= telemetryPeriodMs) {
    lastTelemetryPublishMs_ = millis();

    StaticJsonDocument<384> doc;
    doc["deviceId"] = deviceId();
    doc["ts"] = (uint64_t)time(nullptr);
    doc["weight"] = adjustedWeightString_();
    doc["state"] = tankStateName(tankTracker_.state());
    doc["seq"] = ++telemetrySeq_;
    doc["scaleActive"] = scaleActive;

    String payload;
    serializeJson(doc, payload);
    Log::i(String("MQTT publish: ") + payload);

    bool pubOk = false;
    String topic;
    if (iot_.tenantId().length() == 0) {
      topic = topicUnclaimedTelemetry();
      pubOk = iot_.publishJson(topic, doc);
    } else {
      topic = topicClaimedTelemetry(iot_.tenantId());
      pubOk = iot_.publishJson(topic, doc);
    }

    if (!pubOk) {
      bool queued = outbox_.enqueue(topic, payload, millis());
#if MQTT_DEBUG_MODE
      Log::w(String("MQTT telemetry queued: ") + (queued ? "YES" : "DROP"));
#else
      if (!queued) Log::w("MQTT telemetry dropped (outbox full)");
#endif
    }

#if MQTT_DEBUG_MODE
    Log::i(String("MQTT topic: ") + topic);
    Log::i(String("MQTT publish result: ") + (pubOk ? "OK" : "FAIL"));
    Log::i(String("MQTT telemetry period ms: ") + telemetryPeriodMs);
    Log::i(String("MQTT outbox depth: ") + outbox_.size() + "/" + outbox_.capacity());
#else
    if (!pubOk) {
      Log::w("MQTT publish failed");
    }
#endif
  }

  // ---- Heartbeat publish (slow, includes IP for rough location/support) ----
  if (telemetryEligible && millis() - lastHeartbeatMs_ >= telemetryCfg_.heartbeatPeriodMs) {
    lastHeartbeatMs_ = millis();

    StaticJsonDocument<384> hb;
    hb["deviceId"] = deviceId();
    hb["ts"] = (uint64_t)time(nullptr);
    hb["seq"] = ++heartbeatSeq_;
    hb["uptimeSec"] = millis() / 1000;
    hb["ip"] = wifi_.ipString();
    hb["wifiConnected"] = wifi_.isConnected();
    hb["mqttConnected"] = iot_.isReady();
    hb["state"] = tankStateName(tankTracker_.state());
    hb["rssi"] = WiFi.isConnected() ? WiFi.RSSI() : 0;

    String hbPayload;
    serializeJson(hb, hbPayload);

    bool hbOk = false;
    String hbTopic;
    if (iot_.tenantId().length() == 0) {
      hbTopic = topicUnclaimedHeartbeat();
      hbOk = iot_.publishRaw(hbTopic, hbPayload);
    } else {
      hbTopic = topicClaimedHeartbeat(iot_.tenantId());
      hbOk = iot_.publishRaw(hbTopic, hbPayload);
    }

    if (!hbOk) {
      const bool queueCrowded =
          (outbox_.capacity() > 0) &&
          ((outbox_.size() * 100U) / outbox_.capacity() >= MQTT_OUTBOX_HEARTBEAT_SKIP_PCT);
      bool queued = false;
      if (!queueCrowded) {
        queued = outbox_.enqueue(hbTopic, hbPayload, millis());
      }
#if MQTT_DEBUG_MODE
      Log::w(String("MQTT heartbeat queued: ") + (queued ? "YES" : (queueCrowded ? "SKIP(CROWDED)" : "DROP")));
#else
      if (!queued && !queueCrowded) Log::w("MQTT heartbeat dropped (outbox full)");
#endif
    }

#if MQTT_DEBUG_MODE
    Log::i(String("MQTT heartbeat: ") + hbPayload);
    Log::i(String("MQTT topic: ") + hbTopic);
    Log::i(String("MQTT heartbeat result: ") + (hbOk ? "OK" : "FAIL"));
    Log::i(String("MQTT outbox depth: ") + outbox_.size() + "/" + outbox_.capacity());
#else
    if (!hbOk) {
      Log::w("MQTT heartbeat failed");
    }
#endif
  }

  // ---- BOOT button logic (your original) ----
  static uint32_t pressStartMs_ = 0;
  static bool prevPressed_ = false;

  if (pressed && !prevPressed_) {
    pressStartMs_ = now;
    longPressFired = false;
    noteDisplayActivity_(now);
  }

  if (pressed && pressStartMs_ != 0 && !longPressFired) {
    if (now - pressStartMs_ >= BOOT_HOLD_CLEARMS) {
      longPressFired = true;
      Log::w("BOOT long-press: clearing Wi-Fi credentials...");
      display_.showBoot("Clearing WiFi...");
      wifi_.clearCredentials();
      delay(100);
      ESP.restart();
    }
  }

  if (!pressed && prevPressed_) {
    const uint32_t held = (pressStartMs_ == 0) ? 0 : (now - pressStartMs_);
    pressStartMs_ = 0;

    if (!longPressFired && held >= 30 && held < BOOT_HOLD_CLEARMS) {
      Log::i("BOOT short click");
      noteDisplayActivity_(now);

      nextScreen_();

      if (state_.screen == UiScreen::QR_CODE) {
        const String url = "http://" + wifi_.ipString() + "/";
        display_.showQrUrl(url);
      }
    }
  }

  prevPressed_ = pressed;

  // Ensure mDNS in STA mode
  if (state_.mode == AppMode::STA_NORMAL && wifi_.isConnected()) {
    wifi_.ensureMdnsStarted();
  }

  // Web-requested clear Wi-Fi
  if (state_.requestClearWifi) {
    state_.requestClearWifi = false;
    Log::w("Clearing Wi-Fi credentials (web requested)...");
    display_.showBoot("Clearing WiFi...");
    wifi_.clearCredentials();
    delay(100);
    ESP.restart();
  }

  // UI refresh (non-blocking)
  if (millis() - lastUiMs_ >= UI_PERIOD_MS) {
    lastUiMs_ = millis();
    updateUi_();
  }
}

void App::enterApSetup_() {
  state_.mode = AppMode::AP_SETUP;
  wifi_.startApSetup();
  web_.beginApPortal(wifi_, state_);
  Log::i("AP setup mode: SSID ESP32-SETUP, open 192.168.4.1");
}

void App::enterStaNormal_() {
  state_.mode = AppMode::STA_NORMAL;
  staStartMs_ = millis();
  staFallbackDone_ = false;

  wifi_.startSta();
  web_.beginStaServer(wifi_, state_);
  Log::i("STA normal mode: connecting...");
}

void App::handleSerial_() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      serialLine_.trim();
      if (serialLine_.length()) {
        if (serialLine_.equalsIgnoreCase("clearwifi")) {
          Log::w("Serial command: clearwifi");
          display_.showBoot("Clearing WiFi...");
          wifi_.clearCredentials();
          delay(100);
          ESP.restart();
        } else if (serialLine_.equalsIgnoreCase("cleariot")) {
          Log::w("Serial command: cleariot");
          display_.showBoot("Clearing AWS ID...");
          iot_.clearIdentity();
          delay(100);
          ESP.restart();
        } else if (serialLine_.equalsIgnoreCase("ap")) {
          Log::w("Serial command: ap");
          enterApSetup_();
        } else if (serialLine_.equalsIgnoreCase("sta")) {
          Log::w("Serial command: sta");
          if (wifi_.hasCredentials()) enterStaNormal_();
          else Log::w("No saved creds. Use AP setup first.");
        } else {
          Log::i(String("Unknown command: ") + serialLine_);
          Log::i("Commands: clearwifi | cleariot | ap | sta");
        }
      }
      serialLine_.clear();
    } else {
      if (serialLine_.length() < 80) serialLine_ += c;
    }
  }
}

void App::updateUi_() {
  // If we're on QR screen, don't overwrite it with status updates.
  // If Wi-Fi drops, fall back to info screen.
  if (state_.screen == UiScreen::QR_CODE) {
    if (!(state_.mode == AppMode::STA_NORMAL && wifi_.isConnected() && wifi_.ipString() != "0.0.0.0")) {
      state_.screen = UiScreen::WIFI_INFO;
    } else {
      return;
    }
  }

  state_.wifiConnected = wifi_.isConnected();
  state_.ip = wifi_.ipString();
  state_.hostname = wifi_.hostname();

  if (state_.mode == AppMode::STA_NORMAL) {
    // Scale is "active" if we have a reading received within the last 10s
    bool scaleActive = scale_.hasReading() && (millis() - scale_.lastRxMs() < 10000);
    display_.showMainNormal(state_, adjustedWeightString_(), scaleActive);
  } else {
    display_.showStatus(state_, wifi_);
  }
}

void App::nextScreen_() {
  // Two screens in normal mode: MAIN (WIFI_INFO) <-> QR_CODE
  const bool canQr = (state_.mode == AppMode::STA_NORMAL && wifi_.isConnected() && wifi_.ipString() != "0.0.0.0");

  UiScreen s = state_.screen;
  for (int i = 0; i < 2; i++) {
    s = (s == UiScreen::WIFI_INFO) ? UiScreen::QR_CODE : UiScreen::WIFI_INFO;
    if (s == UiScreen::QR_CODE && !canQr) continue;
    break;
  }

  state_.screen = s;
}

void App::publishEvent_(JsonDocument& doc) {
  const bool eventEligible = (state_.mode == AppMode::STA_NORMAL && timeSynced_);
  if (!eventEligible) return;

  String topic = (iot_.tenantId().length() == 0)
      ? topicUnclaimedEvent()
      : topicClaimedEvent(iot_.tenantId());

  String payload;
  serializeJson(doc, payload);

  bool ok = iot_.publishRaw(topic, payload);
  if (!ok) {
    bool queued = outbox_.enqueue(topic, payload, millis());
#if MQTT_DEBUG_MODE
    Log::w(String("MQTT event queued: ") + (queued ? "YES" : "DROP"));
    Log::i(String("MQTT event topic: ") + topic);
#else
    if (!queued) Log::w("MQTT event dropped (outbox full)");
#endif
  }
#if MQTT_DEBUG_MODE
  else {
    Log::i(String("MQTT event sent: ") + topic);
  }
#endif
}

void App::processClaimPolling_(uint32_t now) {
  claim_.loop(now, iot_.isReady());
  if (!iot_.isReady()) return;

  if (!claim_.isResponseSubscribed()) {
    bool s = iot_.subscribe(claim_.responseTopic());
    if (s) {
      claim_.markResponseSubscribed(true);
#if MQTT_DEBUG_MODE
      Log::i(String("Claim subscribed: ") + claim_.responseTopic());
#endif
    }
  }

  if (!claim_.shouldPollNow(now, iot_.isReady())) return;

  String payload = claim_.buildPollPayload();
  bool ok = iot_.publishRaw(claim_.requestTopic(), payload);
  claim_.notePollSent(now, ok);
#if MQTT_DEBUG_MODE
  Log::i(String("Claim poll -> ") + claim_.requestTopic() + " result=" + (ok ? "OK" : "FAIL"));
#endif
}

float App::adjustedWeight_(float rawWeight) const {
  return rawWeight * telemetryCfg_.readingMultiplier;
}

String App::adjustedWeightString_() const {
  float rawWeight = 0.0f;
  if (!scale_.lastWeightValue(rawWeight)) return "--";
  return formatSignedWeight_(adjustedWeight_(rawWeight));
}

String App::formatSignedWeight_(float weight) const {
  if (!isfinite(weight)) return "--";

  const bool negative = signbit(weight);
  String text = String(fabsf(weight), 4);
  while (text.endsWith("0")) text.remove(text.length() - 1);
  if (text.endsWith(".")) text.remove(text.length() - 1);
  if (text.length() == 0) text = "0";

  return String(negative ? '-' : '+') + text;
}

void App::handleMqttMessage_(const String& topic, const String& payload) {
  bool changed = claim_.handleClaimResponse(topic, payload, millis());
  if (!changed) return;

  iot_.setTenantId(claim_.tenantId());
  if (claim_.tenantId().length() > 0) {
    Log::i(String("CLAIMED: tenantId=") + claim_.tenantId());
  } else {
    Log::w("UNCLAIMED: tenantId cleared by claim response");
  }
}

void App::onMqttMessageStatic_(const String& topic, const String& payload, void* ctx) {
  if (!ctx) return;
  static_cast<App*>(ctx)->handleMqttMessage_(topic, payload);
}

void App::handleFlowEventsOnStateChange_(TankState newState, float currentWeight) {
  const bool nowFlow = (newState == TankState::FILLING || newState == TankState::DISCHARGING);
  const time_t nowTs = time(nullptr);
  const String weightStr = adjustedWeightString_();

  auto emitEnd = [&](TankState endedState) {
    if (!cycleActive_) return;
    StaticJsonDocument<384> doc;
    doc["deviceId"] = deviceId();
    doc["ts"] = (uint64_t)nowTs;
    doc["seq"] = ++eventSeq_;
    doc["provisional"] = true;
    doc["cycleId"] = cycleId_;
    doc["state"] = tankStateName(endedState);
    doc["eventType"] = (endedState == TankState::FILLING) ? "fill_end" : "discharge_end";
    doc["weight"] = weightStr;
    doc["startTs"] = (uint64_t)cycleStartTs_;
    doc["startWeight"] = cycleStartWeight_;
    doc["endWeight"] = currentWeight;
    doc["deltaWeight"] = (currentWeight - cycleStartWeight_);
    if (cycleStartTs_ > 0 && nowTs >= cycleStartTs_) {
      doc["durationSec"] = (uint32_t)(nowTs - cycleStartTs_);
    } else {
      doc["durationSec"] = 0;
    }
    publishEvent_(doc);
  };

  auto emitStart = [&](TankState startedState) {
    cycleActive_ = true;
    cycleState_ = startedState;
    cycleStartTs_ = nowTs;
    cycleStartWeight_ = currentWeight;
    cycleId_ = String(deviceId()) + "-" + String(++cycleSeq_);

    StaticJsonDocument<320> doc;
    doc["deviceId"] = deviceId();
    doc["ts"] = (uint64_t)nowTs;
    doc["seq"] = ++eventSeq_;
    doc["provisional"] = true;
    doc["cycleId"] = cycleId_;
    doc["state"] = tankStateName(startedState);
    doc["eventType"] = (startedState == TankState::FILLING) ? "fill_start" : "discharge_start";
    doc["weight"] = weightStr;
    doc["startWeight"] = currentWeight;
    publishEvent_(doc);
  };

  // Close active cycle when leaving its flow state.
  if (cycleActive_ && newState != cycleState_) {
    emitEnd(cycleState_);
    cycleActive_ = false;
    cycleState_ = TankState::UNKNOWN;
    cycleId_.clear();
    cycleStartTs_ = 0;
    cycleStartWeight_ = 0.0f;
  }

  // Open a new cycle on confirmed flow entry.
  if (nowFlow) {
    emitStart(newState);
  }
}

void App::noteDisplayActivity_(uint32_t now) {
  lastDisplayActivityMs_ = now;
  if (display_.isSleeping()) {
    display_.wake();
  }
}

void App::updateDisplaySleep_(uint32_t now, bool scaleActive) {
  if (!displaySleepEnabled_) {
    if (display_.isSleeping()) display_.wake();
    lastDisplayActivityMs_ = now;
    return;
  }

  const TankState ts = tankTracker_.state();
  const bool flowActive = (ts == TankState::FILLING || ts == TankState::DISCHARGING || ts == TankState::CLEANING);
  if (flowActive) {
    // Keep awake during active process changes.
    if (display_.isSleeping()) display_.wake();
    lastDisplayActivityMs_ = now;
    return;
  }

  // If the scale is disconnected, keep the screen available (operator troubleshooting).
  if (!scaleActive) {
    if (display_.isSleeping()) display_.wake();
    lastDisplayActivityMs_ = now;
    return;
  }

  if (!display_.isSleeping() && (now - lastDisplayActivityMs_ >= displaySleepIdleMs_)) {
    display_.sleep();
  }
}

void App::syncTime_() {
  configTzTime(
    "UTC0",
    "pool.ntp.org",
    "time.nist.gov"
  );
  Log::i("NTP time sync requested");
}
