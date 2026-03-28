#include "ClaimManager.h"
#include <ArduinoJson.h>

static constexpr const char* CLAIM_NS = "claim";
static constexpr const char* CLAIM_KEY_TENANT = "tenantId";

void ClaimManager::begin(const String& deviceId, const ClaimManagerConfig& cfg) {
  deviceId_ = deviceId;
  setConfig(cfg);

  requestTopic_ = String("claim/") + deviceId_ + "/status/request";
  responseTopic_ = String("claim/") + deviceId_ + "/status/response";

  prefsOpen_ = prefs_.begin(CLAIM_NS, false);
  loadTenant_();

  pendingImmediatePoll_ = true;
  awaitingResponse_ = false;
  responseSubscribed_ = false;
  nextPollNotBeforeMs_ = 0;
  nextPeriodicPollMs_ = 0;
  resetBackoff_();
}

void ClaimManager::setConfig(const ClaimManagerConfig& cfg) {
  cfg_ = cfg;
  if (cfg_.periodicPollMs == 0) cfg_.periodicPollMs = 12UL * 60UL * 60UL * 1000UL;
  if (cfg_.responseTimeoutMs == 0) cfg_.responseTimeoutMs = 30000;
  if (cfg_.retryMinMs == 0) cfg_.retryMinMs = 15000;
  if (cfg_.retryMaxMs < cfg_.retryMinMs) cfg_.retryMaxMs = cfg_.retryMinMs;
}

void ClaimManager::onMqttConnected(uint32_t nowMs) {
  responseSubscribed_ = false;
  pendingImmediatePoll_ = true;   // poll on boot/reconnect
  awaitingResponse_ = false;
  nextPollNotBeforeMs_ = nowMs;
  if (cfg_.periodicPollingEnabled) {
    nextPeriodicPollMs_ = nowMs + cfg_.periodicPollMs;
  }
  resetBackoff_();
}

void ClaimManager::onMqttDisconnected() {
  responseSubscribed_ = false;
  awaitingResponse_ = false;
}

void ClaimManager::loop(uint32_t nowMs, bool mqttConnected) {
  if (!mqttConnected) return;

  if (awaitingResponse_ && (nowMs - lastPollSentMs_ > cfg_.responseTimeoutMs)) {
    awaitingResponse_ = false;
    growBackoff_();
    nextPollNotBeforeMs_ = nowMs + currentBackoffMs_;
  }
}

bool ClaimManager::shouldPollNow(uint32_t nowMs, bool mqttConnected) const {
  if (!mqttConnected) return false;
  if (awaitingResponse_) return false;
  if (nowMs < nextPollNotBeforeMs_) return false;
  if (pendingImmediatePoll_) return true;
  if (!cfg_.periodicPollingEnabled) return false;
  return nowMs >= nextPeriodicPollMs_;
}

String ClaimManager::buildPollPayload() const {
  StaticJsonDocument<96> doc;
  doc["ts"] = millis();
  String out;
  serializeJson(doc, out);
  return out;
}

void ClaimManager::notePollSent(uint32_t nowMs, bool sentOk) {
  if (sentOk) {
    awaitingResponse_ = true;
    lastPollSentMs_ = nowMs;
    pendingImmediatePoll_ = false;
    return;
  }

  // Publish failed. Schedule retry with backoff.
  awaitingResponse_ = false;
  pendingImmediatePoll_ = true;
  growBackoff_();
  nextPollNotBeforeMs_ = nowMs + currentBackoffMs_;
}

bool ClaimManager::handleClaimResponse(const String& topic, const String& payload, uint32_t nowMs) {
  if (topic != responseTopic_) return false;

  StaticJsonDocument<256> doc;
  auto err = deserializeJson(doc, payload);
  if (err) return false;

  const String respDeviceId = doc["deviceId"] | "";
  if (respDeviceId.length() && respDeviceId != deviceId_) {
    return false;
  }

  const bool claimed = doc["claimed"] | false;
  String newTenant = "";
  if (claimed) {
    newTenant = doc["tenantId"] | "";
    newTenant.trim();
  }

  bool changed = (newTenant != tenantId_);
  tenantId_ = newTenant;
  if (changed) {
    persistTenant_();
  }

  awaitingResponse_ = false;
  resetBackoff_();
  nextPollNotBeforeMs_ = nowMs + 1000;
  if (cfg_.periodicPollingEnabled) {
    nextPeriodicPollMs_ = nowMs + cfg_.periodicPollMs;
  }

  return changed;
}

void ClaimManager::persistTenant_() {
  if (!prefsOpen_) return;
  prefs_.putString(CLAIM_KEY_TENANT, tenantId_);
}

void ClaimManager::loadTenant_() {
  if (!prefsOpen_) {
    tenantId_.clear();
    return;
  }
  tenantId_ = prefs_.getString(CLAIM_KEY_TENANT, "");
  tenantId_.trim();
}

void ClaimManager::resetBackoff_() {
  currentBackoffMs_ = 0;
}

void ClaimManager::growBackoff_() {
  if (currentBackoffMs_ == 0) currentBackoffMs_ = cfg_.retryMinMs;
  else currentBackoffMs_ = min(cfg_.retryMaxMs, currentBackoffMs_ * 2U);
}
